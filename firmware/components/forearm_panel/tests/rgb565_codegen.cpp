#include "rgb565_transport.h"

// Keep the real helper observable in an isolated object: a target/host build
// using -fno-builtin-memcpy must not reintroduce two libc calls per pixel pair.
extern "C" void rgb565_transport_codegen(const uint16_t* source, uint16_t* destination,
                                         std::size_t pixels) {
    rgb565_swap_copy(source, destination, pixels);
}
