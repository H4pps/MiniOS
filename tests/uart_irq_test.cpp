#include "fdt_fixture.h"
#include "mini_os/drivers/pl011.h"
#include "mini_os/line_editor.h"
#include "mini_os/monitor.h"
#include "mini_os/serial_queue.h"
#include "mini_os/uart_resources.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>
TEST(ReceiveQueue, OrderedBytesErrorsNulAndWraparound) {
    serial::ReceiveQueue queue;
    EXPECT_EQ(queue.pop().status, serial::ReadStatus::empty);
    queue.push({serial::ReadStatus::empty, 0, 0});
    EXPECT_EQ(queue.size(), 0U);
    for (unsigned round = 0; round < 3; ++round) {
        for (unsigned byte = 0; byte < 256; ++byte)
            queue.push({serial::ReadStatus::byte, static_cast<uint8_t>(byte), 0});
        EXPECT_EQ(queue.size(), 256U);
        for (unsigned byte = 0; byte < 256; ++byte) {
            const auto result = queue.pop();
            ASSERT_EQ(result.status, serial::ReadStatus::byte);
            EXPECT_EQ(result.byte, byte);
        }
    }
    queue.push({serial::ReadStatus::error, 'x', serial::parity});
    EXPECT_EQ(queue.pop().errors, serial::parity);
    EXPECT_EQ(queue.dropped(), 0U);
}
TEST(ReceiveQueue, OverflowCancelsWholeLineAndRecoversAtEnter) {
    serial::ReceiveQueue queue;
    kernel::LineEditor editor;
    EXPECT_EQ(editor.feed('a'), kernel::EditAction::appended);
    for (size_t i = 0; i < serial::ReceiveQueue::capacity; ++i)
        queue.push({serial::ReadStatus::byte, 'x', 0});
    queue.push({serial::ReadStatus::byte, '\n', 0});
    EXPECT_EQ(queue.dropped(), 256U);
    EXPECT_EQ(queue.size(), 2U);
    const auto error = queue.pop();
    ASSERT_EQ(error.status, serial::ReadStatus::error);
    EXPECT_EQ(error.errors, serial::overrun);
    editor.cancel();
    EXPECT_EQ(editor.feed(queue.pop().byte), kernel::EditAction::rejected_receive_error);
    editor.clear();
    queue.push({serial::ReadStatus::byte, 'b', 0});
    EXPECT_EQ(editor.feed(queue.pop().byte), kernel::EditAction::appended);
    EXPECT_EQ(editor.feed('\n'), kernel::EditAction::submitted);
    EXPECT_STREQ(editor.text(), "b");
}
TEST(UartMonitor, ParsingAndExactStatistics) {
    EXPECT_EQ(kernel::parse_command({"uart  ", 6}).kind, kernel::CommandKind::uart);
    EXPECT_EQ(kernel::parse_command({"uart test", 9}).kind, kernel::CommandKind::usage);
    std::string output;
    kernel::TextWriter writer([](void *p, char c) { static_cast<std::string *>(p)->push_back(c); },
                              &output);
    serial::render_uart(writer, {33, UINT64_MAX, 3, 4, 256, 2, 9});
    EXPECT_EQ(output, "uart: mode=irq interrupt=33 interrupts=18446744073709551615 received=3 "
                      "errors=4 dropped=256 queued=2 sleeps=9\n");
}
class UartDiscovery : public testing::Test {
  protected:
    fixture::Node root{"",
                       {{"interrupt-parent", fixture::cells({10})}},
                       {{"intc", {{"phandle", fixture::cells({10})}}, {}},
                        {"uart",
                         {{"compatible", fixture::strings({"arm,pl011", "arm,primecell"})},
                          {"interrupts", fixture::cells({0, 1, 4})}},
                         {}}}};
    uint32_t id = 0;
    const char *discover() {
        const auto bytes = fixture::blob(root);
        fdt::View view;
        EXPECT_EQ(fdt::View::open({bytes.data(), bytes.size()}, view), fdt::Error::none);
        platform::GicResources gic{};
        gic.phandle = 10;
        fdt::Node uart = fdt::invalid_node;
        EXPECT_EQ(view.find_node(fdt::String::literal("/intc"), gic.node), fdt::Error::none);
        EXPECT_EQ(view.find_node(fdt::String::literal("/uart"), uart), fdt::Error::none);
        return platform::discover_uart_interrupt(view, uart, gic, id);
    }
};
TEST_F(UartDiscovery, InheritedOrExtendedSelectedGic) {
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(id, 33U);
    root.children[1].properties[1] = {"interrupts-extended", fixture::cells({10, 0, 987, 4})};
    ASSERT_EQ(discover(), nullptr);
    EXPECT_EQ(id, 1019U);
}
TEST_F(UartDiscovery, RejectsMalformedConflictingDisabledAndDuplicateProperties) {
    auto &uart = root.children[1];
    const auto valid = uart.properties[1];
    for (const auto &bytes :
         {fixture::cells({0, 988, 4}), fixture::cells({1, 1, 4}), fixture::cells({0, 1, 1}),
          fixture::cells({0, 1}), fixture::cells({0, 1, 4, 0, 2, 4})}) {
        uart.properties[1].value = bytes;
        EXPECT_NE(discover(), nullptr);
    }
    uart.properties[1] = valid;
    uart.properties.push_back(valid);
    EXPECT_NE(discover(), nullptr);
    uart.properties.pop_back();
    uart.properties.push_back({"interrupts-extended", fixture::cells({10, 0, 1, 4})});
    EXPECT_NE(discover(), nullptr);
    uart.properties.pop_back();
    uart.properties.push_back({"status", fixture::strings({"disabled"})});
    EXPECT_NE(discover(), nullptr);
    uart.properties.pop_back();
    fixture::property(root, "interrupt-parent") = fixture::cells({11});
    EXPECT_NE(discover(), nullptr);
    fixture::property(root, "interrupt-parent") = fixture::cells({10});
    root.children[0].properties.push_back({"phandle", fixture::cells({10})});
    EXPECT_NE(discover(), nullptr);
}
class ReceiveInterrupt : public testing::Test {
  protected:
    std::vector<uint32_t> fifo;
    std::vector<serial::ReadResult> delivered;
    uint32_t status = 0x10, clear = 0, error_clear = 123, mask = 0, threshold = 123;
    size_t consumed = 0;
    drivers::pl011::Io io{this,
                          [](void *p, uintptr_t address) {
                              auto &s = *static_cast<ReceiveInterrupt *>(p);
                              if (address == 0x1040)
                                  return s.status;
                              if (address == 0x1018)
                                  return s.consumed == s.fifo.size() ? 16U : 0U;
                              if (address == 0x1000)
                                  return s.fifo.at(s.consumed++);
                              return 0U;
                          },
                          [](void *p, uintptr_t address, uint32_t value) {
                              auto &s = *static_cast<ReceiveInterrupt *>(p);
                              if (address == 0x1044)
                                  s.clear = value;
                              if (address == 0x1004)
                                  s.error_clear = value;
                              if (address == 0x1038)
                                  s.mask = value;
                              if (address == 0x1034)
                                  s.threshold = value;
                          }};
    size_t service() {
        return drivers::pl011::service_receive_interrupt(
            0x1000,
            [](void *p, serial::ReadResult result) {
                static_cast<ReceiveInterrupt *>(p)->delivered.push_back(result);
            },
            this, io);
    }
};
TEST_F(ReceiveInterrupt, EnablesOnlyReceiveCausesAndDrainsBytesWithErrors) {
    drivers::pl011::enable_receive_interrupts(0x1000, io);
    EXPECT_EQ(mask, 0x7d0U);
    EXPECT_EQ(threshold, 0U);
    EXPECT_EQ(clear, 0x7ffU);
    fifo = {0, 0x41, 0xf42};
    status = 0x7d0;
    EXPECT_EQ(service(), 3U);
    ASSERT_EQ(delivered.size(), 3U);
    EXPECT_EQ(delivered[0].byte, 0U);
    EXPECT_EQ(delivered[1].byte, 'A');
    EXPECT_EQ(delivered[2].status, serial::ReadStatus::error);
    EXPECT_EQ(delivered[2].errors, 15U);
    EXPECT_EQ(error_clear, 0U);
    EXPECT_EQ(clear, 0x7c0U);
}
TEST_F(ReceiveInterrupt, BoundedDrainLeavesLevelPendingAndIgnoresMaskedCauses) {
    fifo.resize(100, 0x41);
    EXPECT_EQ(service(), 64U);
    EXPECT_EQ(consumed, 64U);
    EXPECT_EQ(clear, 0U);
    EXPECT_EQ(service(), 36U);
    EXPECT_EQ(delivered.size(), 100U);
    status = 0x20;
    EXPECT_EQ(service(), 0U);
    EXPECT_EQ(delivered.size(), 100U);
}
TEST_F(ReceiveInterrupt, EmptyFifoLatchedErrorCancelsInputAndClearsHardwareStatus) {
    status = 0x400;
    EXPECT_EQ(service(), 0U);
    ASSERT_EQ(delivered.size(), 1U);
    EXPECT_EQ(delivered[0].errors, serial::overrun);
    EXPECT_EQ(error_clear, 0U);
    EXPECT_EQ(clear, 0x400U);
}
