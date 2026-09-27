#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Core/LoanTypes.h>
#include <VLMS/Core/MetricsRepository.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using VLMS::Clock;
using VLMS::Date;
using namespace VLMS::Test;

namespace {

// localtime_r and tm_gmtoff are POSIX; MinGW has neither, and the Windows
// installer build compiles this file. The offset is the gap between the local
// and the UTC calendar reading of the same instant, which is what tm_gmtoff
// holds.
std::tm brokenDown(std::time_t when, bool local)
{
    std::tm out{};
#ifdef _WIN32
    if (local) {
        localtime_s(&out, &when);
    } else {
        gmtime_s(&out, &when);
    }
#else
    if (local) {
        localtime_r(&when, &out);
    } else {
        gmtime_r(&when, &out);
    }
#endif
    return out;
}

std::int64_t secondsSinceEpochOf(const std::tm& fields)
{
    const std::chrono::year_month_day ymd{std::chrono::year(fields.tm_year + 1900),
                                          std::chrono::month(fields.tm_mon + 1),
                                          std::chrono::day(fields.tm_mday)};
    const std::int64_t days = std::chrono::sys_days(ymd).time_since_epoch().count();
    return days * 86400 + fields.tm_hour * 3600 + fields.tm_min * 60 + fields.tm_sec;
}

int offsetSecondsAt(std::time_t when)
{
    return static_cast<int>(secondsSinceEpochOf(brokenDown(when, true))
                            - secondsSinceEpochOf(brokenDown(when, false)));
}

int localOffsetSeconds()
{
    return offsetSecondsAt(std::time(nullptr));
}

int localOffsetSecondsAtMonthsOffset(int months)
{
    std::tm local = brokenDown(std::time(nullptr), true);
    local.tm_mon += months;
    return offsetSecondsAt(std::mktime(&local));
}

Date dateAtFixedUtcOffset(const std::chrono::system_clock::time_point& utc, int offsetHours)
{
    const auto shifted = utc + std::chrono::hours(offsetHours);
    const auto days = std::chrono::floor<std::chrono::days>(shifted);
    const std::chrono::year_month_day ymd{days};
    return Date(static_cast<int>(ymd.year()),
                static_cast<int>(static_cast<unsigned>(ymd.month())),
                static_cast<int>(static_cast<unsigned>(ymd.day())));
}

std::string suiteContext()
{
    std::ostringstream out;
    out << timeZoneDescription() << " | local date " << Date::todayLocal().toIso() << " | UTC date "
        << utcToday().toIso() << " | "
        << (localDateDiffersFromUtcDate()
                ? "DATES DIFFER -- this entry is the one carrying the proof right now"
                : "dates agree -- the other entry carries the proof right now");
    return out.str();
}

}  // namespace

/**
 * finding 1 -- loan dates are written in LOCAL time and compared in UTC.
 *
 * ==========================================================================
 *  READ THIS BEFORE DELETING ONE OF THE TWO CTEST ENTRIES.
 *
 *  This executable is registered TWICE, under TZ=Pacific/Kiritimati (UTC+14)
 *  and TZ=Pacific/Midway (UTC-11). That is not a copy-paste mistake, and the
 *  duplicate is not flaky.
 *
 *  NEITHER ENTRY PROVES ANYTHING ON ITS OWN. Each one only fails during the
 *  hours when its own local calendar date differs from UTC's, so either one
 *  alone would pass most of the day and look fine. The two zones are 25 hours
 *  apart, so at every real instant AT LEAST ONE of them is on a different
 *  calendar date from UTC -- which is what makes the pair a proof rather than
 *  a coincidence. Deleting one turns a guarantee into a lottery.
 *
 *  Both zones are free of daylight saving, so the offsets never move.
 * ==========================================================================
 *
 * These tests are also deliberately NOT clock-pinned. The bug is a
 * disagreement between the process clock and SQLite's clock; overriding the
 * process clock would hide exactly the thing under test. That is why the fix
 * has to bind Clock::todayIso() into the SQL rather than switch date('now')
 * to date('now','localtime') -- the latter leaves SQLite reading its own
 * clock, and this file would still be a lottery.
 *
 * Gated on UNIX in CMake: Qt 6 on Windows resolves the time zone through the
 * Win32 API and ignores TZ, which would make both entries silently vacuous.
 */
class test_core_Timezone : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        // Emitted once per run so that a CI log shows which of the two entries was
        // the one on a different date from UTC at that moment.
        std::fprintf(stderr, "%s\n", suiteContext().c_str());
    }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<CirculationRepository>(m_db->session());
        m_metrics = std::make_unique<MetricsRepository>(m_db->session());
        m_memberId = 0;
        m_copies.clear();
    }

    void TearDown() override
    {
        EXPECT_FALSE(Clock::isOverridden())
            << "this suite must never pin the clock: the bug IS a clock disagreement";
        m_metrics.reset();
        m_repository.reset();
        m_db.reset();
    }

    void seedMemberAndCopies(int copyCount = 4)
    {
        MemberSeed member = uniqueMemberSeed(1);
        member.status = MemberStatus::kActive;
        m_memberId = seedMember(*m_db, member);
        ASSERT_GT(m_memberId, 0);
        ASSERT_TRUE(rawSetRegisteredAt(*m_db, m_memberId, "2019-01-15"));

        BookSeed book = uniqueBookSeed(1);
        book.initialCopyCount = copyCount;
        const std::int64_t bookId = seedBook(*m_db, book);
        ASSERT_GT(bookId, 0);

        m_copies = copyIdsOf(*m_db, bookId);
        ASSERT_EQ(static_cast<int>(m_copies.size()), copyCount);
    }

    [[nodiscard]] std::int64_t copyAt(int index) const { return m_copies.at(static_cast<std::size_t>(index)); }

    [[nodiscard]] int countWithFilter(const std::string& filter) const
    {
        LoanQuery query;
        query.filters = {filter};
        return VLMS_UNWRAP(m_repository->countLoans(query));
    }

    /// A message naming the zone, the two calendar dates, and the offset.
    [[nodiscard]] static std::string context() { return suiteContext(); }

    // The two calendars that disagree, and which way round they are.
    // Each XFAIL below is armed on a DIRECTION, not merely on "they differ":
    // a positive offset and a negative offset break different rules, and
    // arming on the wrong one produces an XPASS, which Qt Test scores as a
    // failure. Naming the direction is also the clearest statement of what
    // each defect actually does to a librarian in Ksour Essef (UTC+1, so
    // always the utcIsBehindLocal case, for one hour every night).
    [[nodiscard]] [[maybe_unused]] static Date localToday() { return Date::todayLocal(); }
    [[nodiscard]] [[maybe_unused]] static Date sqliteToday() { return utcToday(); }
    [[nodiscard]] [[maybe_unused]] static bool utcIsBehindLocal() { return sqliteToday() < localToday(); }
    [[nodiscard]] [[maybe_unused]] static bool utcIsAheadOfLocal() { return sqliteToday() > localToday(); }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CirculationRepository> m_repository;
    std::unique_ptr<MetricsRepository> m_metrics;
    std::int64_t m_memberId = 0;
    std::vector<std::int64_t> m_copies;
};

// ---------------------------------------------------------------------------
// Preconditions -- if these fail, the rest of the file is meaningless
// ---------------------------------------------------------------------------

TEST_F(test_core_Timezone, TheTwoZonesAreConfiguredAsIntended)
{
    const int offsetSeconds = localOffsetSeconds();
    const int offsetHours = offsetSeconds / 3600;

    // Either +14 or -11. Anything else means TZ did not reach the process and
    // both entries are testing the developer's own time zone twice over.
    ASSERT_TRUE(offsetHours == 14 || offsetHours == -11)
        << "expected UTC+14 or UTC-11, got UTC" << (offsetHours >= 0 ? "+" : "") << offsetHours << " ("
        << timeZoneDescription() << ")";

    // Both zones must hold a constant offset all year, or the 25-hour spread
    // the pair depends on could close. Note this cannot be asked as
    // hasDaylightTime(): that reports whether the zone has EVER observed
    // daylight saving, and Pacific/Midway did historically, so it answers true
    // for a zone that has been a flat UTC-11 for years. Sampling the actual
    // offset across the year is the question we mean to ask.
    EXPECT_EQ(localOffsetSecondsAtMonthsOffset(6), offsetSeconds);
    EXPECT_EQ(localOffsetSecondsAtMonthsOffset(-6), offsetSeconds);
}

TEST_F(test_core_Timezone, AtLeastOneRegisteredZoneDiffersFromUtcRightNow)
{
    // The guarantee the whole two-entry arrangement rests on, checked from
    // inside both processes so it cannot quietly stop being true -- if someone
    // swaps a zone for one only two hours from UTC, this fails immediately
    // instead of the suite going green for the wrong reason.
    const auto instant = std::chrono::system_clock::now();
    const Date utcDate = utcToday();
    const Date kiritimati = dateAtFixedUtcOffset(instant, 14);
    const Date midway = dateAtFixedUtcOffset(instant, -11);

    ASSERT_TRUE(kiritimati != utcDate || midway != utcDate)
        << "both registered zones agree with UTC (" << utcDate.toIso()
        << "); the pair no longer spans a date boundary";

    // 25 hours apart, so they are never on the same date as each other either.
    EXPECT_NE(kiritimati, midway) << "the two zones are on the same calendar date";
}

TEST_F(test_core_Timezone, SqliteDateNowFollowsUtcNotTheProcessTimeZone)
{
    // The mechanism of the whole finding, stated once and directly. SQLite's
    // date('now') is UTC no matter what TZ the process runs in, so at UTC+14
    // and UTC-11 it disagrees with Date::todayLocal() for 14 and 11 hours a
    // day respectively.
    const std::string sqliteNow = m_db->scalar("SELECT date('now')").toString();
    EXPECT_EQ(sqliteNow, utcToday().toIso());

    if (localDateDiffersFromUtcDate()) {
        EXPECT_NE(sqliteNow, Date::todayLocal().toIso()) << context();
    }
}

// ---------------------------------------------------------------------------
// The finding
// ---------------------------------------------------------------------------

TEST_F(test_core_Timezone, LoanDueYesterdayIsOverdueAtEveryHour)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    // Due yesterday, local. A librarian looking at the overdue list this
    // morning expects to see it, in every time zone, at every hour.
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-15).toIso(), today.addDays(-1).toIso()),
              0);

    // Used to break when UTC lagged local -- the UTC+14 entry for fourteen
    // hours a day, and Ksour Essef at UTC+1 for the hour after local midnight.
    // The predicate was `due < date('now')`; with date('now') still on
    // yesterday, yesterday < yesterday is false and the book was quietly not
    // yet overdue. C4 binds :today from Clock::todayIso(), so both sides of
    // the comparison are now local and this holds in both zones at every hour.
    EXPECT_EQ(countWithFilter(LoanFilter::kOverdue), 1) << context();
}

TEST_F(test_core_Timezone, LoanDueTodayIsNotOverdueAtEveryHour)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-14).toIso(), today.toIso()), 0);

    // The mirror image, which used to break when UTC ran ahead of local -- the
    // UTC-11 entry, for eleven hours a day. date('now') was already on
    // tomorrow, so a loan due today was reported overdue before the borrower's
    // day was out. Same C4 fix; note the two directions broke opposite
    // assertions, which is why both zones are registered.
    EXPECT_EQ(countWithFilter(LoanFilter::kOverdue), 0) << context();
}

TEST_F(test_core_Timezone, LoanBorrowedTodayIsCountedInTodaysCheckouts)
{
    seedMemberAndCopies();

    // A loan created right now, through the repository, exactly as the
    // checkout dialog does it. It should appear in today's checkouts
    // immediately -- the librarian is standing at the desk.
    LoanInput input;
    input.memberId = m_memberId;
    input.bookCopyId = copyAt(0);
    ASSERT_TRUE(m_repository->createLoan(input)) << "repository call failed";

    // createLoan used to stamp a local date while the window was
    // `date(borrowed_at) >= date('now')` (UTC). When UTC ran ahead, the loan
    // just created was dated "yesterday" as far as SQLite was concerned and
    // dropped straight out of today's count -- the desk recorded a checkout
    // that today's KPI did not show. C3 resolves the window in C++ from
    // Clock::today(), so both ends now speak local time and this holds in
    // both zones at every hour.
    EXPECT_EQ(VLMS_UNWRAP(m_metrics->fetchMetrics()).today.checkouts, 1) << context();
}

TEST_F(test_core_Timezone, LoanReturnedTodayIsAcceptedAtEveryHour)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    const std::int64_t loanId =
        rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-3).toIso(), today.addDays(11).toIso());
    ASSERT_GT(loanId, 0);

    // returnLoan's future-date guard is pure C++ against Date::todayLocal(),
    // so the acceptance half is already local-consistent and holds in both
    // zones at every hour. It is here as armor: C1 and C4 rewrite the
    // surrounding code, and a return booked today must never start being
    // refused as "in the future".
    ASSERT_TRUE(m_repository->returnLoan(loanId, today.toIso())) << "repository call failed | " << context();
    EXPECT_EQ(m_repository->getLoan(loanId)->returnedAt, today.toIso());
}

TEST_F(test_core_Timezone, LoanReturnedTodayIsCountedInTodaysReturns)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    const std::int64_t loanId =
        rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-3).toIso(), today.addDays(11).toIso());
    ASSERT_GT(loanId, 0);
    const auto mutated = m_repository->returnLoan(loanId, today.toIso());
    ASSERT_TRUE(mutated) << mutated.error().key;

    // The other half of the same operation, and a site the audit did not list:
    // the return was accepted with a local date, then the returns window
    // compared it against UTC, so handing a book back over the desk left
    // today's returns counter unmoved. Fixed by the same C3 change as the
    // checkouts window above.
    EXPECT_EQ(VLMS_UNWRAP(m_metrics->fetchMetrics()).today.returns, 1) << context();
}

TEST_F(test_core_Timezone, OverdueKpiAndOverdueFilterAgreeAtEveryHour)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-15).toIso(), today.addDays(-1).toIso()),
              0);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(1), today.addDays(-14).toIso(), today.toIso()), 0);

    LoanQuery overdueOnly;
    overdueOnly.filters = {LoanFilter::kOverdue};

    // Both sides share the same wrong clock, so they agree today and will
    // still agree after the fix. This is the invariant that must survive C3
    // and C4 rewriting the two predicates independently.
    EXPECT_EQ(VLMS_UNWRAP(m_metrics->fetchMetrics()).overdueLoans,
              VLMS_UNWRAP(m_repository->countLoans(overdueOnly)));

    // Agreeing was never the same as being right. Two open loans, due
    // yesterday and today; the honest answer is 1. When UTC lagged, neither
    // counted and the page showed 0; when UTC led, both counted and it showed
    // 2. Wrong in both directions and consistent about it, which is precisely
    // why nothing in the app could notice -- the two numbers a librarian could
    // have compared were derived from the same wrong clock.
    EXPECT_EQ(VLMS_UNWRAP(m_metrics->fetchMetrics()).overdueLoans, 1) << context();
}
