// Error reporting that works with and without C++ exceptions (the WebAssembly/WASI build has none).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace gp {
[[noreturn]] inline void fatal(const char* what) {
    std::fprintf(stderr, "gameplan fatal: %s\n", what);
    std::abort();
}
}  // namespace gp

#ifdef GP_NO_EXCEPTIONS
#define GP_THROW(ex) ::gp::fatal((ex).what())
#else
#define GP_THROW(ex) throw ex
#endif
