#include "TestEnv.h"

#include <VLMS/Core/Clock.h>

#include <gtest/gtest.h>

#include <regex>

using namespace VLMS;
using namespace Test;

class test_core_Clock : public ::testing::Test {
protected:
    void TearDown() override
    {
        EXPECT_FALSE(Core::Clock::isOverridden()) << "a test leaked a clock override";
        Core::Clock::reset();
    }
};

TEST_F(test_core_Clock, DefaultsToTheSystemLocalDate)
{
    EXPECT_FALSE(Core::Clock::isOverridden());
    EXPECT_EQ(Core::Clock::today(), Core::Date::todayLocal());
}

TEST_F(test_core_Clock, SystemDateIsLocalRatherThanUtc)
{
    EXPECT_EQ(Core::Clock::today(), Core::Date::todayLocal());

    if (localDateDiffersFromUtcDate()) {
        EXPECT_NE(Core::Clock::today(), utcToday())
            << "Clock followed UTC instead of local time (" << timeZoneDescription() << ")";
    }
}

TEST_F(test_core_Clock, TodayIsoIsTenCharacterIsoDate)
{
    const std::string iso = Core::Clock::todayIso();
    EXPECT_EQ(iso.size(), 10u);
    EXPECT_TRUE(std::regex_match(iso, std::regex("^\\d{4}-\\d{2}-\\d{2}$"))) << iso;
    EXPECT_EQ(Core::Date::fromIso(iso), Core::Clock::today());
}

TEST_F(test_core_Clock, NowIsoUsesSqliteDatetimeShapeNotIsoT)
{
    const std::string stamp = Core::Clock::nowIso();
    EXPECT_EQ(stamp.size(), 19u);
    EXPECT_EQ(stamp[10], ' ');
    EXPECT_EQ(stamp.find('T'), std::string::npos) << stamp;
    EXPECT_TRUE(std::regex_match(stamp, std::regex("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$")))
        << stamp;
    EXPECT_EQ(stamp.substr(0, Core::Clock::todayIso().size()), Core::Clock::todayIso());
}

TEST_F(test_core_Clock, NowIsoIsLexicographicallyOrderedOverTime)
{
    const Core::ScopedClock earlier(Core::DateTime(Core::Date(2026, 1, 9), 23, 59, 59));
    const std::string first = Core::Clock::nowIso();

    Core::Clock::setFixedDateTime(Core::DateTime(Core::Date(2026, 1, 10), 0, 0, 0));
    const std::string second = Core::Clock::nowIso();

    EXPECT_LT(first, second) << first << " !< " << second;
}

TEST_F(test_core_Clock, SetFixedDatePinsToday)
{
    Core::Clock::setFixedDate(Core::Date(2019, 3, 7));
    EXPECT_EQ(Core::Clock::today(), Core::Date(2019, 3, 7));
    EXPECT_EQ(Core::Clock::todayIso(), "2019-03-07");
    Core::Clock::reset();
}

TEST_F(test_core_Clock, SetFixedDateParksAtMiddayNotMidnight)
{
    const Core::ScopedClock pinned(Core::Date(2019, 3, 7));
    EXPECT_EQ(Core::Clock::now().hour(), 12);
    EXPECT_EQ(Core::Clock::now().minute(), 0);
    EXPECT_EQ(Core::Clock::nowIso(), "2019-03-07 12:00:00");
}

TEST_F(test_core_Clock, SetFixedDateTimePinsTheTimeOfDay)
{
    const Core::ScopedClock pinned(Core::DateTime(Core::Date(2026, 2, 1), 23, 30, 15));
    EXPECT_EQ(Core::Clock::today(), Core::Date(2026, 2, 1));
    EXPECT_EQ(Core::Clock::nowIso(), "2026-02-01 23:30:15");
}

TEST_F(test_core_Clock, InvalidFixedDateIsRefusedRatherThanInstalled)
{
    Core::Clock::setFixedDate(Core::Date());
    EXPECT_FALSE(Core::Clock::isOverridden()) << "an invalid date installed an invalid 'now'";
    EXPECT_EQ(Core::Clock::today(), Core::Date::todayLocal());
}

TEST_F(test_core_Clock, ResetRestoresTheSystemClock)
{
    Core::Clock::setFixedDate(Core::Date(1999, 12, 31));
    EXPECT_EQ(Core::Clock::today(), Core::Date(1999, 12, 31));

    Core::Clock::reset();
    EXPECT_FALSE(Core::Clock::isOverridden());
    EXPECT_EQ(Core::Clock::today(), Core::Date::todayLocal());
}

TEST_F(test_core_Clock, IsOverriddenTracksTheOverride)
{
    EXPECT_FALSE(Core::Clock::isOverridden());
    {
        const Core::ScopedClock pinned(Core::Date(2020, 6, 1));
        EXPECT_TRUE(Core::Clock::isOverridden());
    }
    EXPECT_FALSE(Core::Clock::isOverridden());
}

TEST_F(test_core_Clock, ScopedClockRestoresOnDestruction)
{
    const Core::Date real = Core::Date::todayLocal();
    {
        const Core::ScopedClock pinned(Core::Date(2001, 9, 11));
        EXPECT_EQ(Core::Clock::today(), Core::Date(2001, 9, 11));
    }
    EXPECT_EQ(Core::Clock::today(), real);
}

TEST_F(test_core_Clock, ScopedClockNests)
{
    const Core::ScopedClock outer(Core::Date(2020, 1, 1));
    EXPECT_EQ(Core::Clock::today(), Core::Date(2020, 1, 1));
    {
        const Core::ScopedClock inner(Core::Date(2021, 2, 2));
        EXPECT_EQ(Core::Clock::today(), Core::Date(2021, 2, 2));
    }
    EXPECT_EQ(Core::Clock::today(), Core::Date(2020, 1, 1));
}

TEST_F(test_core_Clock, ScopedClockRestoresAbsenceNotJustValue)
{
    EXPECT_FALSE(Core::Clock::isOverridden());
    {
        const Core::ScopedClock pinned(Core::Date(2020, 1, 1));
        (void)pinned;
    }
    EXPECT_FALSE(Core::Clock::isOverridden()) << "ScopedClock restored a value where there was none";
}

TEST_F(test_core_Clock, TodayAndTodayIsoAgreeUnderAnOverride)
{
    const Core::ScopedClock pinned(Core::DateTime(Core::Date(2026, 12, 31), 23, 59, 59));
    EXPECT_EQ(Core::Date::fromIso(Core::Clock::todayIso()), Core::Clock::today());
    EXPECT_EQ(Core::Clock::todayIso(), "2026-12-31");
}
