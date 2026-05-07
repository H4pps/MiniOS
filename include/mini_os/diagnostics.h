#ifndef MINI_OS_DIAGNOSTICS_H
#define MINI_OS_DIAGNOSTICS_H

#include "mini_os/exception.h"
#include "mini_os/text_writer.h"

namespace kernel {
void render_exception(TextWriter &writer, const arch::ExceptionFrame &frame);
}
#endif
