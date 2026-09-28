#pragma once

#include <VLMS/Core/Date.h>
#include <VLMS/Core/Result.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <string_view>

namespace Test {

/// True when the process local calendar date differs from UTC today.
[[nodiscard]] bool localDateDiffersFromUtcDate();

/// Human-readable description of the active time zone, for failure messages.
[[nodiscard]] std::string timeZoneDescription();

/// UTC calendar date (system_clock), for timezone assertions.
[[nodiscard]] VLMS::Date utcToday();

/**
 * Sets or restores an environment variable for the lifetime of the object.
 * Restores the previous value (or unsets) on destruction.
 */
class ScopedEnv {
public:
    ScopedEnv(const char* name, const std::string& value);
    ~ScopedEnv();

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    std::string m_name;
    std::string m_previous;
    bool m_hadPrevious = false;
};

template<typename T>
[[nodiscard]] T unwrapResult(const VLMS::Result<T>& result, const char* file, int line)
{
    if (!result) {
        ADD_FAILURE_AT(file, line) << (result.error().key.empty() ? "Result failed"
                                                                  : result.error().key);
        return T{};
    }
    return result.value();
}

/// Unwrap a repository Result in tests. Fails the test when the call failed.
#define VLMS_UNWRAP(result) (::Test::unwrapResult((result), __FILE__, __LINE__))

}  // namespace Test
