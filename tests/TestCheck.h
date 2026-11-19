#pragma once
// Test assertions must execute in Release builds compiled with -DNDEBUG.
// Unlike assert(), this preserves both evaluation and unused-variable checks.
#include <cstdio>
#include <cstdlib>

#define AIVERSE_TEST_CHECK(condition) do {                                   \
    if (!(condition)) {                                                       \
        std::fprintf(stderr, "AIVERSE test check failed: %s (%s:%d)\\n",     \
                     #condition, __FILE__, __LINE__);                        \
        std::abort();                                                         \
    }                                                                         \
} while (false)
