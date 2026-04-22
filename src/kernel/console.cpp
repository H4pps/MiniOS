#include "mini_os/console.h"

#include "mini_os/line_editor.h"
#include "mini_os/platform.h"

namespace kernel {
[[noreturn]] void run_console() {
    LineEditor editor;
    platform::early_write("mini-os: uart ready\nmini-os> ");
    for (;;) {
        const auto input = platform::early_read();
        if (input.status == serial::ReadStatus::empty) {
            continue; // Receive interrupts are disabled; WFI would stall input.
        }
        if (input.status == serial::ReadStatus::error) {
            editor.cancel();
            continue;
        }
        switch (editor.feed(input.byte)) {
        case EditAction::ignored:
            continue;
        case EditAction::appended:
            platform::early_putc(static_cast<char>(input.byte));
            continue;
        case EditAction::erased:
            platform::early_write("\b \b");
            continue;
        case EditAction::overflow:
            platform::early_putc('\a');
            continue;
        case EditAction::submitted:
            platform::early_putc('\n');
            if (editor.length() != 0) {
                platform::early_write("echo: ");
                platform::early_write(editor.text());
                platform::early_putc('\n');
            }
            break;
        case EditAction::rejected_too_long:
            platform::early_write("\nmini-os: line too long\n");
            break;
        case EditAction::rejected_receive_error:
            platform::early_write("\nmini-os: uart RX error\n");
            break;
        }
        editor.clear();
        platform::early_write("mini-os> ");
    }
}
} // namespace kernel
