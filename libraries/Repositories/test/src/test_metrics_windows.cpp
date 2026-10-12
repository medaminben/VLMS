#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Repositories/LoanTypes.h>
#include <VLMS/Repositories/MetricsRepository.h>
#include <VLMS/Repositories/MetricsTypes.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace VLMS;
using namespace Test;

/**
 * The three metrics windows -- today, this week, this month.
 *
 * Two jobs. First, pin the *semantics*, which are inconsistent with each other
 * on purpose: the week is a rolling seven days ('-6 days') while the month is
 * a calendar month ('start of month'). C3 rewrites these windows to be
 * computed in C++ and bound, and nothing else would notice if the meaning
 * quietly changed on the way.
 *
 * Second, document finding 4: no window has an upper bound, so a row dated in
 * the future counts toward today AND this week AND this month, and keeps doing
 * so until the calendar catches up with it.
 */
class test_core_MetricsWindows : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_metrics = std::make_unique<Repositories::MetricsRepository>(m_db->session());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_memberId = 0;
        m_copies.clear();
    }

    void TearDown() override
    {
        EXPECT_FALSE(Core::Clock::isOverridden()) << "a test leaked a clock override";
        m_circulation.reset();
        m_metrics.reset();
        m_db.reset();
    }

    void seedMemberAndCopies(int copyCount = 6)
    {
        MemberSeed member = uniqueMemberSeed(1);
        member.status = Repositories::MemberStatus::kActive;
        m_memberId = seedMember(*m_db, member);
        ASSERT_GT(m_memberId, 0);

        // registered_at is stamped from Clock on create; park it far in the past so a
        // seeded member never lands inside a window being measured.
        ASSERT_TRUE(rawSetRegisteredAt(*m_db, m_memberId, "2019-01-15"));

        BookSeed book = uniqueBookSeed(1);
        book.initialCopyCount = copyCount;
        const std::int64_t bookId = seedBook(*m_db, book);
        ASSERT_GT(bookId, 0);

        m_copies = copyIdsOf(*m_db, bookId);
        ASSERT_EQ(static_cast<int>(m_copies.size()), copyCount);
    }

    [[nodiscard]] std::int64_t copyAt(int index) const { return m_copies.at(static_cast<std::size_t>(index)); }

    [[nodiscard]] Repositories::LibraryMetrics metrics() const { return VLMS_UNWRAP(m_metrics->fetchMetrics()); }

    bool borrowOn(int copyIndex, const Core::Date& borrowed, const Core::Date& returned = {})
    {
        return rawInsertLoan(*m_db, m_memberId, copyAt(copyIndex), borrowed.toIso(),
                             borrowed.addDays(14).toIso(), returned.isValid() ? returned.toIso() : std::string())
            > 0;
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MetricsRepository> m_metrics;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::int64_t m_memberId = 0;
    std::vector<std::int64_t> m_copies;
};

// ---------------------------------------------------------------------------
// Window semantics
// ---------------------------------------------------------------------------

TEST_F(test_core_MetricsWindows, TodayWindowCountsALoanBorrowedToday)
{
    seedMemberAndCopies();
    ASSERT_TRUE(borrowOn(0, Core::Date::todayLocal()));

    EXPECT_EQ(metrics().today.checkouts, 1);
}

TEST_F(test_core_MetricsWindows, TodayWindowExcludesALoanBorrowedAWeekAgo)
{
    seedMemberAndCopies();
    ASSERT_TRUE(borrowOn(0, Core::Date::todayLocal().addDays(-7)));

    EXPECT_EQ(metrics().today.checkouts, 0);
    EXPECT_EQ(metrics().thisWeek.checkouts, 0);
}

TEST_F(test_core_MetricsWindows, WeekWindowIsRollingSevenDaysNotCalendarWeek)
{
    seedMemberAndCopies();
    const Core::Date today = Core::Date::todayLocal();

    // Six days back is inside a rolling seven-day window on every day of the
    // week. A calendar week starting Monday would drop it whenever today is
    // earlier in the week than the row -- which is the distinction being
    // pinned, since C3 recomputes this boundary in C++.
    ASSERT_TRUE(borrowOn(0, today.addDays(-6)));
    ASSERT_TRUE(borrowOn(1, today.addDays(-10)));

    const Repositories::LibraryMetrics m = metrics();
    EXPECT_EQ(m.thisWeek.checkouts, 1);
    EXPECT_EQ(m.today.checkouts, 0);
}

TEST_F(test_core_MetricsWindows, WeekWindowIncludesRowExactlySixDaysAgo)
{
    seedMemberAndCopies();
    ASSERT_TRUE(borrowOn(0, Core::Date::todayLocal().addDays(-6)));

    // The window is '-6 days', i.e. seven days inclusive of today. Off by one
    // in either direction and this row moves.
    EXPECT_EQ(metrics().thisWeek.checkouts, 1);
}

TEST_F(test_core_MetricsWindows, MonthWindowIsCalendarMonthNotRollingThirtyDays)
{
    seedMemberAndCopies();
    const Core::Date today = Core::Date::todayLocal();
    const Core::Date firstOfMonth(today.year(), today.month(), 1);

    ASSERT_TRUE(borrowOn(0, firstOfMonth));
    ASSERT_TRUE(borrowOn(1, firstOfMonth.addDays(-1)));  // last day of the previous month

    const Repositories::LibraryMetrics m = metrics();
    ASSERT_GE(m.thisMonth.checkouts, 1) << "the first of this month fell outside the month window";

    // The exclusion is the half that actually distinguishes a calendar month
    // from a rolling thirty days. It used to be guarded by a
    // !localDateDiffersFromUtcDate() check, because finding 1 broke it for one
    // hour on the first of the month: SQLite's date('now') was UTC, so between
    // 00:00 and 01:00 local on the 1st, 'start of month' resolved to the first
    // of the PREVIOUS month and pulled the row in. C3 computes the window
    // start in C++ from Clock::today(), so the assertion is unconditional now
    // -- and being unconditional is the point, since the guarded version could
    // not have failed if C3 had got the boundary wrong.
    EXPECT_EQ(m.thisMonth.checkouts, 1);
    EXPECT_NE(m.thisMonth.checkouts, 2) << "the month window reached back into the previous month";
}

TEST_F(test_core_MetricsWindows, ReturnsWindowCountsTheReturnDateNotTheBorrowDate)
{
    seedMemberAndCopies();
    const Core::Date today = Core::Date::todayLocal();

    // Borrowed two months ago, returned today.
    ASSERT_TRUE(borrowOn(0, today.addDays(-60), today));

    const Repositories::LibraryMetrics m = metrics();
    EXPECT_EQ(m.today.returns, 1);
    EXPECT_EQ(m.today.checkouts, 0);
    EXPECT_EQ(m.thisWeek.checkouts, 0);
}

TEST_F(test_core_MetricsWindows, NewMembersWindowCountsRegisteredAt)
{
    seedMemberAndCopies();

    MemberSeed fresh = uniqueMemberSeed(2);
    const std::int64_t freshId = seedMember(*m_db, fresh);
    ASSERT_GT(freshId, 0);
    ASSERT_TRUE(rawSetRegisteredAt(*m_db, freshId, Core::Date::todayLocal().toIso()));

    const Repositories::LibraryMetrics m = metrics();
    EXPECT_EQ(m.totalMembers, 2);
    EXPECT_EQ(m.today.newMembers, 1);  // the 2019 member from seedMemberAndCopies is excluded
}

TEST_F(test_core_MetricsWindows, WindowsNestSoTodayNeverExceedsTheMonth)
{
    seedMemberAndCopies();
    const Core::Date today = Core::Date::todayLocal();
    const Core::Date firstOfMonth(today.year(), today.month(), 1);

    ASSERT_TRUE(borrowOn(0, today));
    ASSERT_TRUE(borrowOn(1, today.addDays(-3)));
    ASSERT_TRUE(borrowOn(2, firstOfMonth));
    ASSERT_TRUE(borrowOn(3, today.addDays(-200)));

    const Repositories::LibraryMetrics m = metrics();

    // Today nests inside both wider windows: its start is the latest of the
    // three. Note that week and month do NOT nest in each other -- early in a
    // month the rolling week reaches back past the 1st, which is a direct
    // consequence of the two windows using different rules on purpose.
    EXPECT_LE(m.today.checkouts, m.thisWeek.checkouts) << "today exceeded this week";
    EXPECT_LE(m.today.checkouts, m.thisMonth.checkouts) << "today exceeded this month";
    // On the 1st, the first-of-month loan is also today's.
    EXPECT_EQ(m.today.checkouts, firstOfMonth == today ? 2 : 1);
}

TEST_F(test_core_MetricsWindows, OverdueKpiAgreesWithTheOverdueFilter)
{
    seedMemberAndCopies();
    const Core::Date today = Core::Date::todayLocal();

    ASSERT_TRUE(borrowOn(0, today.addDays(-44)));                    // due 30 days ago
    ASSERT_TRUE(borrowOn(1, today.addDays(-40)));                    // due 26 days ago
    ASSERT_TRUE(borrowOn(2, today));                                 // due in 14 days
    ASSERT_TRUE(borrowOn(3, today.addDays(-60), today.addDays(-1))); // returned

    Repositories::LoanQuery overdueOnly;
    overdueOnly.filters = {Repositories::LoanFilter::kOverdue};

    // The KPI lives in Repositories::MetricsRepository and the filter in
    // Repositories::CirculationRepository, and C3 and C4 rewrite them separately. They are
    // meant to be the same predicate; this is what says so.
    EXPECT_EQ(metrics().overdueLoans, VLMS_UNWRAP(m_circulation->countLoans(overdueOnly)));
    EXPECT_EQ(metrics().overdueLoans, 2);
}

// ---------------------------------------------------------------------------
// [XF] finding 4 -- no upper bound on any window
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The windows come from the Clock (C3)
// ---------------------------------------------------------------------------
//
// Before C3 the three windows were SQL fragments -- "'now'",
// "'now', '-6 days'", "'now', 'start of month'" -- interpolated into the query
// and resolved by SQLite in UTC. Every test above can only check the window
// relative to whatever day the suite happens to run on, which means none of
// them can reach a month boundary on purpose. These pin the clock instead, so
// the boundaries are exercised at a date chosen for being awkward.
//
// 2021-06-15 is a Tuesday in a 30-day month preceded by a 31-day one.

TEST_F(test_core_MetricsWindows, EveryWindowFollowsThePinnedClock)
{
    seedMemberAndCopies();

    const Core::ScopedClock pinned(Core::Date(2021, 6, 15));

    ASSERT_TRUE(borrowOn(0, Core::Date(2021, 6, 15)));  // the pinned today
    ASSERT_TRUE(borrowOn(1, Core::Date(2021, 6, 9)));   // six days back: last day of the week window
    ASSERT_TRUE(borrowOn(2, Core::Date(2021, 6, 8)));   // seven days back: outside it
    ASSERT_TRUE(borrowOn(3, Core::Date(2021, 6, 1)));   // first of the pinned month

    const Repositories::LibraryMetrics m = metrics();
    EXPECT_EQ(m.today.checkouts, 1);
    EXPECT_EQ(m.thisWeek.checkouts, 2);   // the 15th and the 9th, not the 8th
    EXPECT_EQ(m.thisMonth.checkouts, 4);  // everything from the 1st onward
}

TEST_F(test_core_MetricsWindows, MonthWindowStartsOnTheFirstOfThePinnedMonth)
{
    seedMemberAndCopies();

    const Core::ScopedClock pinned(Core::Date(2021, 6, 15));

    ASSERT_TRUE(borrowOn(0, Core::Date(2021, 5, 31)));  // May has 31 days; this is the day before
    ASSERT_TRUE(borrowOn(1, Core::Date(2021, 6, 1)));

    // The exclusion, at a real month boundary rather than whichever one the
    // calendar happens to offer. The previous month's last day is not in the
    // window even though it is 15 days ago -- a rolling thirty days would
    // include it.
    EXPECT_EQ(metrics().thisMonth.checkouts, 1);
}

TEST_F(test_core_MetricsWindows, WindowsAreClosedIntervalsEndingOnThePinnedToday)
{
    seedMemberAndCopies();

    const Core::ScopedClock pinned(Core::Date(2021, 6, 15));

    ASSERT_TRUE(borrowOn(0, Core::Date(2021, 6, 15)));  // the last included day
    ASSERT_TRUE(borrowOn(1, Core::Date(2021, 6, 16)));  // one day past the end

    const Repositories::LibraryMetrics m = metrics();
    EXPECT_EQ(m.today.checkouts, 1);
    EXPECT_EQ(m.thisWeek.checkouts, 1);
    EXPECT_EQ(m.thisMonth.checkouts, 1);
}

// ---------------------------------------------------------------------------
// Window upper bound (was finding 4)
// ---------------------------------------------------------------------------

TEST_F(test_core_MetricsWindows, FutureDatedLoanIsNotCountedInAllThreeWindowsSimultaneously)
{
    seedMemberAndCopies();
    ASSERT_TRUE(borrowOn(0, Core::Date::todayLocal().addDays(30)));

    const Repositories::LibraryMetrics m = metrics();

    // Every window used to be `date(borrowed_at) >= <start>` with no upper
    // bound, so one row a month in the future was reported as a checkout
    // today, this week and this month at once -- and stayed that way for the
    // next thirty days. C3 made every window a closed interval ending today.
    //
    // Note this is still reachable input: LoanCheckoutDialog sets no
    // setMaximumDate(), so the calendar popup offers next month. C6 closes
    // that; until then the metrics simply refuse to count it.
    EXPECT_EQ(m.today.checkouts, 0);
    EXPECT_EQ(m.thisWeek.checkouts, 0);
    EXPECT_EQ(m.thisMonth.checkouts, 0);
}

TEST_F(test_core_MetricsWindows, FutureReturnDateIsNotCountedInTodaysReturns)
{
    seedMemberAndCopies();
    const Core::Date today = Core::Date::todayLocal();

    ASSERT_TRUE(borrowOn(0, today.addDays(-10), today.addDays(5)));

    EXPECT_EQ(metrics().today.returns, 0);
}

TEST_F(test_core_MetricsWindows, FutureRegisteredMemberIsNotCountedInTodaysNewMembers)
{
    seedMemberAndCopies();

    const std::int64_t freshId = seedMember(*m_db, uniqueMemberSeed(3));
    ASSERT_GT(freshId, 0);
    ASSERT_TRUE(rawSetRegisteredAt(*m_db, freshId, Core::Date::todayLocal().addDays(45).toIso()));

    EXPECT_EQ(metrics().today.newMembers, 0);
}

// ---------------------------------------------------------------------------
// [XF] finding 3 -- rows with unreadable dates vanish
// ---------------------------------------------------------------------------

TEST_F(test_core_MetricsWindows, OverdueKpiCountsLoanWithUnparseableDueDate)
{
    seedMemberAndCopies();
    const Core::Date today = Core::Date::todayLocal();

    // Only a database whose constraint migration the pre-flight declined can
    // hold this row -- and that is the database whose KPI has to show it.
    ASSERT_TRUE(degradeLoansToPreV1Shape(*m_db));
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-90).toIso(), "14/08/2020"), 0);

    // The book has been out for three months. date('14/08/2020') is NULL, the
    // comparison was NULL, and the KPI on the Metrics page read zero -- so the
    // one number a librarian would use to notice this problem was the number
    // the problem hid from.
    EXPECT_EQ(metrics().overdueLoans, 1);
}
