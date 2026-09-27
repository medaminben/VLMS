#include "TestEnv.h"

#include <VLMS/Core/Clock.h>

#include <gtest/gtest.h>

#include <regex>

using VLMS::Clock;
using VLMS::Date;
using VLMS::DateTime;
using VLMS::ScopedClock;
using namespace VLMS::Test;

class test_core_Clock : public ::testing::Test {
protected:
    void TearDown() override
    {
        EXPECT_FALSE(Clock::isOverridden()) << "a test leaked a clock override";
        Clock::reset();
    }
};

TEST_F(test_core_Clock, DefaultsToTheSystemLocalDate)
{
    EXPECT_FALSE(Clock::isOverridden());
    EXPECT_EQ(Clock::today(), Date::todayLocal());
}

TEST_F(test_core_Clock, SystemDateIsLocalRatherThanUtc)
{
    EXPECT_EQ(Clock::today(), Date::todayLocal());

    if (localDateDiffersFromUtcDate()) {
        EXPECT_NE(Clock::today(), utcToday())
            << "Clock followed UTC instead of local time (" << timeZoneDescription() << ")";
    }
}

TEST_F(test_core_Clock, TodayIsoIsTenCharacterIsoDate)
{
    const std::string iso = Clock::todayIso();
    EXPECT_EQ(iso.size(), 10u);
    EXPECT_TRUE(std::regex_match(iso, std::regex("^\\d{4}-\\d{2}-\\d{2}$"))) << iso;
    EXPECT_EQ(Date::fromIso(iso), Clock::today());
}

TEST_F(test_core_Clock, NowIsoUsesSqliteDatetimeShapeNotIsoT)
{
    const std::string stamp = Clock::nowIso();
    EXPECT_EQ(stamp.size(), 19u);
    EXPECT_EQ(stamp[10], ' ');
    EXPECT_EQ(stamp.find('T'), std::string::npos) << stamp;
    EXPECT_TRUE(std::regex_match(stamp, std::regex("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$")))
        << stamp;
    EXPECT_EQ(stamp.substr(0, Clock::todayIso().size()), Clock::todayIso());
}

TEST_F(test_core_Clock, NowIsoIsLexicographicallyOrderedOverTime)
{
    const ScopedClock earlier(DateTime(Date(2026, 1, 9), 23, 59, 59));
    const std::string first = Clock::nowIso();

    Clock::setFixedDateTime(DateTime(Date(2026, 1, 10), 0, 0, 0));
    const std::string second = Clock::nowIso();

    EXPECT_LT(first, second) << first << " !< " << second;
}

TEST_F(test_core_Clock, SetFixedDatePinsToday)
{
    Clock::setFixedDate(Date(2019, 3, 7));
    EXPECT_EQ(Clock::today(), Date(2019, 3, 7));
    EXPECT_EQ(Clock::todayIso(), "2019-03-07");
    Clock::reset();
}

TEST_F(test_core_Clock, SetFixedDateParksAtMiddayNotMidnight)
{
    const ScopedClock pinned(Date(2019, 3, 7));
    EXPECT_EQ(Clock::now().hour(), 12);
    EXPECT_EQ(Clock::now().minute(), 0);
    EXPECT_EQ(Clock::nowIso(), "2019-03-07 12:00:00");
}

TEST_F(test_core_Clock, SetFixedDateTimePinsTheTimeOfDay)
{
    const ScopedClock pinned(DateTime(Date(2026, 2, 1), 23, 30, 15));
    EXPECT_EQ(Clock::today(), Date(2026, 2, 1));
    EXPECT_EQ(Clock::nowIso(), "2026-02-01 23:30:15");
}

TEST_F(test_core_Clock, InvalidFixedDateIsRefusedRatherThanInstalled)
{
    Clock::setFixedDate(Date());
    EXPECT_FALSE(Clock::isOverridden()) << "an invalid date installed an invalid 'now'";
    EXPECT_EQ(Clock::today(), Date::todayLocal());
}

TEST_F(test_core_Clock, ResetRestoresTheSystemClock)
{
    Clock::setFixedDate(Date(1999, 12, 31));
    EXPECT_EQ(Clock::today(), Date(1999, 12, 31));

    Clock::reset();
    EXPECT_FALSE(Clock::isOverridden());
    EXPECT_EQ(Clock::today(), Date::todayLocal());
}

TEST_F(test_core_Clock, IsOverriddenTracksTheOverride)
{
    EXPECT_FALSE(Clock::isOverridden());
    {
        const ScopedClock pinned(Date(2020, 6, 1));
        EXPECT_TRUE(Clock::isOverridden());
    }
    EXPECT_FALSE(Clock::isOverridden());
}

TEST_F(test_core_Clock, ScopedClockRestoresOnDestruction)
{
    const Date real = Date::todayLocal();
    {
        const ScopedClock pinned(Date(2001, 9, 11));
        EXPECT_EQ(Clock::today(), Date(2001, 9, 11));
    }
    EXPECT_EQ(Clock::today(), real);
}

TEST_F(test_core_Clock, ScopedClockNests)
{
    const ScopedClock outer(Date(2020, 1, 1));
    EXPECT_EQ(Clock::today(), Date(2020, 1, 1));
    {
        const ScopedClock inner(Date(2021, 2, 2));
        EXPECT_EQ(Clock::today(), Date(2021, 2, 2));
    }
    EXPECT_EQ(Clock::today(), Date(2020, 1, 1));
}

TEST_F(test_core_Clock, ScopedClockRestoresAbsenceNotJustValue)
{
    EXPECT_FALSE(Clock::isOverridden());
    {
        const ScopedClock pinned(Date(2020, 1, 1));
        (void)pinned;
    }
    EXPECT_FALSE(Clock::isOverridden()) << "ScopedClock restored a value where there was none";
}

TEST_F(test_core_Clock, TodayAndTodayIsoAgreeUnderAnOverride)
{
    const ScopedClock pinned(DateTime(Date(2026, 12, 31), 23, 59, 59));
    EXPECT_EQ(Date::fromIso(Clock::todayIso()), Clock::today());
    EXPECT_EQ(Clock::todayIso(), "2026-12-31");
}
