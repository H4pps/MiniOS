#include "mini_os/drivers/pl011.h"

#include <gtest/gtest.h>

class Pl011Test : public testing::Test {
  protected:
    uint32_t registers[0x4c / sizeof(uint32_t)]{};
    uintptr_t base() { return reinterpret_cast<uintptr_t>(registers); }
    uint32_t &at(size_t offset) { return registers[offset / sizeof(uint32_t)]; }
};

TEST_F(Pl011Test, EmptyFifoDoesNotConsumeErroredDataOrClearErrors) {
    at(0x18) = 1U << 4;
    at(0) = 0xf41;
    at(4) = 0xdead;
    const auto result = drivers::pl011::try_read(base());
    EXPECT_EQ(result.status, serial::ReadStatus::empty);
    EXPECT_EQ(result.errors, 0);
    EXPECT_EQ(at(0), 0xf41U);
    EXPECT_EQ(at(4), 0xdeadU);
}

TEST_F(Pl011Test, AllValidBytesIncludingNulPreserveErrorClearRegister) {
    at(4) = 0xdead;
    for (uint32_t value = 0; value <= 255; ++value) {
        at(0) = value;
        const auto result = drivers::pl011::try_read(base());
        EXPECT_EQ(result.status, serial::ReadStatus::byte);
        EXPECT_EQ(result.byte, value);
        EXPECT_EQ(result.errors, 0);
        EXPECT_EQ(at(4), 0xdeadU);
    }
}

TEST_F(Pl011Test, EachErrorAndEveryCombinationPreserveByteAndClearStatus) {
    for (uint32_t errors = 1; errors <= 15; ++errors) {
        at(4) = 0xdead;
        at(0) = (errors << 8) | 0x41;
        const auto result = drivers::pl011::try_read(base());
        EXPECT_EQ(result.status, serial::ReadStatus::error);
        EXPECT_EQ(result.byte, 0x41);
        EXPECT_EQ(result.errors, errors);
        EXPECT_EQ(at(4), 0U);
    }
    EXPECT_EQ(serial::framing, 1);
    EXPECT_EQ(serial::parity, 2);
    EXPECT_EQ(serial::brk, 4);
    EXPECT_EQ(serial::overrun, 8);
}

TEST_F(Pl011Test, InitializationEnablesPollingRxTxAt115200And8N1) {
    at(4) = at(0x38) = at(0x48) = 0xffffffff;
    ASSERT_TRUE(drivers::pl011::initialize({base(), 24000000, 115200}));
    EXPECT_EQ(at(0x30), 0x301U);
    EXPECT_EQ(at(0x2c), 0x70U);
    EXPECT_EQ(at(0x24), 13U);
    EXPECT_EQ(at(0x28), 1U);
    EXPECT_EQ(at(4), 0U);
    EXPECT_EQ(at(0x38), 0U);
    EXPECT_EQ(at(0x44), 0x7ffU);
    EXPECT_EQ(at(0x48), 0U);
}

TEST_F(Pl011Test, InvalidConfigurationDoesNotAccessRegisters) {
    at(0x30) = 0xdead;
    EXPECT_FALSE(drivers::pl011::initialize({0, 24000000, 115200}));
    EXPECT_FALSE(drivers::pl011::initialize({base(), 0, 115200}));
    EXPECT_FALSE(drivers::pl011::initialize({base(), 24000000, 0}));
    EXPECT_FALSE(drivers::pl011::initialize({base(), 1, 115200}));
    EXPECT_FALSE(drivers::pl011::initialize({base(), 24000000, 1}));
    EXPECT_EQ(at(0x30), 0xdeadU);
}
