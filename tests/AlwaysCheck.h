// Always-on assertions for AIVERSE native regression tests.
//
// Standard assert() disappears under NDEBUG/Release. In a test that can hide
// failed invariants, suppress variables used only by assertions, and even
// remove side effects such as fixture creation inside assert(expr).
#pragma once
#include <stdexcept>
#include <string>

namespace aiverse::test_support {
inline void require(bool condition, const char* expression,
                    const char* file, int line) {
    if (!condition)
        throw std::runtime_error(std::string(file) + ":" +
                                 std::to_string(line) +
                                 ": CHECK failed: " + expression);
}
} // namespace aiverse::test_support

// The expression MUST execute exactly once in Debug and Release alike.
#define AIVERSE_TEST_CHECK(expression) \
    ::aiverse::test_support::require( \
        static_cast<bool>(expression), #expression, __FILE__, __LINE__)
