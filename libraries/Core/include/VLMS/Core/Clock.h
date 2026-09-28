#pragma once

#include <VLMS/Core/Date.h>

#include <optional>
#include <string>

namespace VLMS::Core {

/**
 * The single source of "now" for the whole application.
 *
 * Two consumers depend on it and must never disagree:
 *   - C++ guards          -> Clock::today()
 *   - SQL date predicates -> Clock::todayIso(), bound as :today
 *
 * Everything here is *local* calendar time. The library sits in Ksour Essef
 * (UTC+1); a loan due today has to stop being "due" at local midnight, not at
 * 01:00 the next morning, which is what SQLite's date('now') -- UTC -- gives.
 *
 * Static rather than injected. The loan dialogs are constructed inline from
 * page code and take no repository, so there is nowhere to thread a clock
 * through to them. A static is a drop-in in both worlds.
 *
 * Nothing outside this class may call the system clock directly; a
 * `grep -rn "todayLocal\\|nowLocal\\|currentDate" libraries applications`
 * that returns anything but Date.cpp / Clock.cpp is a bug.
 *
 * The override is a plain static, deliberately NOT thread_local.
 */
class Clock {
public:
    [[nodiscard]] static Date today();
    [[nodiscard]] static DateTime now();
    [[nodiscard]] static std::string todayIso();
    [[nodiscard]] static std::string nowIso();

    static void setFixedDate(const Date& date);
    static void setFixedDateTime(const DateTime& dateTime);
    static void reset();

    [[nodiscard]] static bool isOverridden();
    [[nodiscard]] static std::optional<DateTime> fixedDateTime();
};

class ScopedClock {
public:
    explicit ScopedClock(const Date& date);
    explicit ScopedClock(const DateTime& dateTime);
    ~ScopedClock();

    ScopedClock(const ScopedClock&) = delete;
    ScopedClock& operator=(const ScopedClock&) = delete;
    ScopedClock(ScopedClock&&) = delete;
    ScopedClock& operator=(ScopedClock&&) = delete;

private:
    std::optional<DateTime> m_previous;
};

}  // namespace VLMS::Core
