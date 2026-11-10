#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#ifdef FOREARM_UI_TEST_HEAP
extern bool forearm_ui_test_fail_large_allocations;
extern unsigned forearm_ui_test_allocations;
extern unsigned forearm_ui_test_frees;
inline void *heap_caps_malloc(std::size_t n, unsigned) {
    ++forearm_ui_test_allocations;
    return forearm_ui_test_fail_large_allocations && n >= 4096 ? nullptr : std::malloc(n);
}
inline void *heap_caps_calloc(std::size_t n, std::size_t size, unsigned) {
    ++forearm_ui_test_allocations;
    return forearm_ui_test_fail_large_allocations && n * size >= 4096 ? nullptr : std::calloc(n, size);
}
inline void heap_caps_free(void *pointer) { ++forearm_ui_test_frees; std::free(pointer); }
#else
inline void *heap_caps_malloc(std::size_t n, unsigned) { return std::malloc(n); }
inline void *heap_caps_calloc(std::size_t n, std::size_t size, unsigned) { return std::calloc(n, size); }
inline void heap_caps_free(void *pointer) { std::free(pointer); }
#endif
