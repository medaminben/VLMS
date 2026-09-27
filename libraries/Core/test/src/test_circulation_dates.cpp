#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Core/LoanTypes.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using VLMS::Clock;
using VLMS::Date;
using VLMS::ScopedClock;
using namespace VLMS::Test;

/**
 * Date coherence in the circulation repository.
 *
 * Every test marked [XF] carries a QEXPECT_FAIL naming the finding it
 * documents. Qt Test reports an XPASS as a failure, so the moment Phase C
 * lands a fix, the suite mechanically demands the marker's removal -- which is
 * the whole reason these are written as XFAILs rather than as tests asserting
 * that the current, wrong behaviour is correct.
 *
 * The tests that are NOT marked are the ones that pass today and must keep
 * passing through the refactor. They use comfortable offsets (30 days, not one
 * day) so that the UTC-versus-local boundary cannot make them flaky; that
 * boundary is proved separately and deliberately in test_circulation_timezone.
 */
class test_core_CirculationDates : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<CirculationRepository>(m_db->session());
        m_memberId = 0;
        m_copies.clear();
    }

    void TearDown() override
    {
        EXPECT_FALSE(Clock::isOverridden()) << "a test leaked a clock override";
        m_repository.reset();
        m_db.reset();
    }

    /// A borrowable member and a run of available copies.
    void seedMemberAndCopies(int copyCount = 3)
    {
        MemberSeed member = uniqueMemberSeed(1);
        member.status = MemberStatus::kActive;
        m_memberId = seedMember(*m_db, member);
        ASSERT_GT(m_memberId, 0);

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

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CirculationRepository> m_repository;
    std::int64_t m_memberId = 0;
    std::vector<std::int64_t> m_copies;
};

// ---------------------------------------------------------------------------
// The Clock seam (C1)
// ---------------------------------------------------------------------------
//
// C1 replaced every QDate::currentDate() in the repositories and the loan
// dialogs with Clock::today(). That is a pure refactor, so the whole suite
// stays green either way -- which means without these three tests, reverting
// C1 would break nothing and the seam would rot. They are the only assertions
// that a pinned clock actually reaches production code.
//
// The dates are deliberately nowhere near the real today, so a test that
// silently fell back to the system clock could not pass by coincidence.

TEST_F(test_core_CirculationDates, CreateLoanDefaultsFollowThePinnedClock)
{
    seedMemberAndCopies();

    const ScopedClock pinned(Date(2021, 6, 7));

    LoanInput input;
    input.memberId = m_memberId;
    input.bookCopyId = copyAt(0);
    // Both dates blank: createLoan fills them from the clock.

    std::int64_t loanId = 0;
    const auto created = m_repository->createLoan(input);
    ASSERT_TRUE(created) << created.error().key;
    loanId = created.value();

    const auto loan = m_repository->getLoan(loanId);
    ASSERT_TRUE(loan.has_value());
    EXPECT_EQ(loan->borrowedAt, "2021-06-07");
    EXPECT_EQ(loan->dueAt, "2021-06-21");  // +14, the default policy
}

TEST_F(test_core_CirculationDates, ReturnLoanDefaultsToThePinnedClocksToday)
{
    seedMemberAndCopies();

    LoanInput input;
    input.memberId = m_memberId;
    input.bookCopyId = copyAt(0);
    input.borrowedAt = "2021-06-07";
    input.dueAt = "2021-06-21";

    std::int64_t loanId = 0;
    const auto created = m_repository->createLoan(input);
    ASSERT_TRUE(created) << created.error().key;
    loanId = created.value();

    const ScopedClock pinned(Date(2021, 6, 14));
    const auto mutated = m_repository->returnLoan(loanId);
    ASSERT_TRUE(mutated) << mutated.error().key;

    const auto loan = m_repository->getLoan(loanId);
    ASSERT_TRUE(loan.has_value());
    EXPECT_EQ(loan->returnedAt, "2021-06-14");
}

TEST_F(test_core_CirculationDates, ReturnLoanMeasuresTheFutureAgainstThePinnedClock)
{
    seedMemberAndCopies();

    LoanInput input;
    input.memberId = m_memberId;
    input.bookCopyId = copyAt(0);
    input.borrowedAt = "2021-06-07";
    input.dueAt = "2021-06-21";

    std::int64_t loanId = 0;
    const auto created = m_repository->createLoan(input);
    ASSERT_TRUE(created) << created.error().key;
    loanId = created.value();

    // 2021-06-15 is in the past by the wall clock but in the future by the
    // pinned one. The guard has to follow the pinned clock, not the wall.
    const ScopedClock pinned(Date(2021, 6, 14));
    const auto failed = m_repository->returnLoan(loanId, "2021-06-15");
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());

    // ... and the day itself is still accepted.
    const auto mutated = m_repository->returnLoan(loanId, "2021-06-14");
    ASSERT_TRUE(mutated) << mutated.error().key;
}

// ---------------------------------------------------------------------------
// Overdue is decided by the bound :today (C4)
// ---------------------------------------------------------------------------
//
// The overdue predicate used to be `date(due_at) < date('now')`, resolved by
// SQLite in UTC while every due_at is a local date. C4 replaced date('now')
// with a :today bound from the Clock at all five circulation sites.
//
// isOverdueFlagIsCorrectWithNoFiltersApplied above is the tripwire for
// forgetting the bind on the unfiltered path. These are the other half: that
// the value bound is the Clock's, and that it decides the boundary exactly.

TEST_F(test_core_CirculationDates, OverdueFlagFollowsThePinnedClock)
{
    seedMemberAndCopies();

    const std::int64_t dueOn15th =
        rawInsertLoan(*m_db, m_memberId, copyAt(0), "2021-06-01", "2021-06-15");
    ASSERT_GT(dueOn15th, 0);

    {
        // On the due date itself the loan is not yet overdue. This is the
        // assertion the UTC-ahead direction used to break.
        const ScopedClock onTheDueDate(Date(2021, 6, 15));
        EXPECT_EQ(m_repository->getLoan(dueOn15th)->isOverdue, false);
        EXPECT_EQ(VLMS_UNWRAP(m_repository->listLoans(LoanQuery{})).at(0).isOverdue, false);
    }
    {
        // One day later it is. This is the assertion the UTC-behind direction
        // used to break -- opposite direction, opposite failure.
        const ScopedClock theDayAfter(Date(2021, 6, 16));
        EXPECT_EQ(m_repository->getLoan(dueOn15th)->isOverdue, true);
        EXPECT_EQ(VLMS_UNWRAP(m_repository->listLoans(LoanQuery{})).at(0).isOverdue, true);
    }
}

TEST_F(test_core_CirculationDates, OverdueFilterCountFollowsThePinnedClock)
{
    seedMemberAndCopies();

    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), "2021-06-01", "2021-06-14"), 0);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(1), "2021-06-01", "2021-06-15"), 0);

    LoanQuery overdueOnly;
    overdueOnly.filters = {LoanFilter::kOverdue};

    const ScopedClock pinned(Date(2021, 6, 15));

    // Due the 14th is overdue, due the 15th is not. listLoans and countLoans
    // build their filter clauses separately, so both are checked: a bind
    // missing from one of them shows up as the two disagreeing.
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(overdueOnly)), 1);
    EXPECT_EQ(static_cast<int>(VLMS_UNWRAP(m_repository->listLoans(overdueOnly)).size()), 1);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listLoans(overdueOnly)).at(0).dueAt, "2021-06-14");
}

TEST_F(test_core_CirculationDates, AnUnreturnedLoanIsOpenUntilItsDueDateThenOverdue)
{
    seedMemberAndCopies();

    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), "2021-06-01", "2021-06-14"), 0);

    // Open, Overdue, and Returned are three states, not nested ones: a loan
    // past its due date is Overdue and no longer Open. Only the unfiltered
    // count mentions no :today and must not care what day it is.
    LoanQuery openOnly;
    openOnly.filters = {LoanFilter::kOpen};
    LoanQuery overdueOnly;
    overdueOnly.filters = {LoanFilter::kOverdue};

    {
        const ScopedClock before(Date(2021, 6, 1));
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(openOnly)), 1);
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(overdueOnly)), 0);
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(LoanQuery{})), 1);
    }
    {
        const ScopedClock dueDay(Date(2021, 6, 14));
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(openOnly)), 1);
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(overdueOnly)), 0);
    }
    {
        const ScopedClock longAfter(Date(2031, 1, 1));
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(openOnly)), 0);
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(overdueOnly)), 1);
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(LoanQuery{})), 1);
    }
}

// ---------------------------------------------------------------------------
// Correct today
// ---------------------------------------------------------------------------

TEST_F(test_core_CirculationDates, CreateLoanDefaultsToTodayPlusFourteenDays)
{
    seedMemberAndCopies();

    LoanInput input;
    input.memberId = m_memberId;
    input.bookCopyId = copyAt(0);
    // borrowedAt and dueAt left blank on purpose: this pins the default policy.

    std::int64_t loanId = 0;
    const auto created = m_repository->createLoan(input);
    ASSERT_TRUE(created) << created.error().key;
    loanId = created.value();

    const auto loan = m_repository->getLoan(loanId);
    ASSERT_TRUE(loan.has_value());
    EXPECT_EQ(loan->borrowedAt, Date::todayLocal().toIso());
    EXPECT_EQ(loan->dueAt, Date::todayLocal().addDays(14).toIso());
}

TEST_F(test_core_CirculationDates, CreateLoanRejectsDueBeforeBorrowed)
{
    seedMemberAndCopies();

    LoanInput input;
    input.memberId = m_memberId;
    input.bookCopyId = copyAt(0);
    input.borrowedAt = "2026-03-10";
    input.dueAt = "2026-03-09";

    const auto failed = m_repository->createLoan(input);
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_EQ(m_db->count("loans"), 0);
}

TEST_F(test_core_CirculationDates, CreateLoanRejectsACopyAlreadyOnLoan)
{
    seedMemberAndCopies();

    LoanInput first;
    first.memberId = m_memberId;
    first.bookCopyId = copyAt(0);
    ASSERT_TRUE(m_repository->createLoan(first));

    LoanInput second = first;
    EXPECT_FALSE(m_repository->createLoan(second)) << "a second open loan on the same copy was accepted";
    EXPECT_EQ(m_db->count("loans"), 1);
}

TEST_F(test_core_CirculationDates, LoanDueTodayIsNotOverdue)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-3).toIso(), today.toIso()), 0);

    EXPECT_EQ(countWithFilter(LoanFilter::kOverdue), 0);
    EXPECT_EQ(countWithFilter(LoanFilter::kOpen), 1);
}

TEST_F(test_core_CirculationDates, LoanDueThirtyDaysAgoIsOverdue)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-44).toIso(),
                            today.addDays(-30).toIso()),
              0);

    EXPECT_EQ(countWithFilter(LoanFilter::kOverdue), 1);
    EXPECT_EQ(countWithFilter(LoanFilter::kOpen), 0);
}

TEST_F(test_core_CirculationDates, ReturnedLoanIsNeverOverdue)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-60).toIso(),
                            today.addDays(-46).toIso(), today.addDays(-40).toIso()),
              0);

    EXPECT_EQ(countWithFilter(LoanFilter::kOverdue), 0);
    EXPECT_EQ(countWithFilter(LoanFilter::kReturned), 1);
}

TEST_F(test_core_CirculationDates, IsOverdueFlagIsCorrectWithNoFiltersApplied)
{
    // C4 binds :today into listLoans. The placeholder appears in the SELECT
    // CASE and in ORDER BY as well as in the filter clause, so binding it
    // inside the `if (!filterClauses.isEmpty())` branch would leave it NULL on
    // this path -- and QSQLITE binds an unmentioned placeholder as NULL
    // WITHOUT erroring, silently turning every is_overdue into 0. This test is
    // the tripwire for exactly that mistake.
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-44).toIso(),
                            today.addDays(-30).toIso()),
              0);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(1), today.toIso(), today.addDays(14).toIso()), 0);

    const LoanQuery unfiltered;  // no filters at all
    const auto loans = VLMS_UNWRAP(m_repository->listLoans(unfiltered));
    ASSERT_EQ(static_cast<int>(loans.size()), 2);

    int overdue = 0;
    for (const LoanRecord& loan : loans) {
        if (loan.isOverdue) {
            ++overdue;
        }
    }
    EXPECT_EQ(overdue, 1);
}

TEST_F(test_core_CirculationDates, ReturnLoanRejectsFutureReturnDate)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    const std::int64_t loanId =
        rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-3).toIso(), today.addDays(11).toIso());
    ASSERT_GT(loanId, 0);

    const auto failed = m_repository->returnLoan(loanId, today.addDays(1).toIso());
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_TRUE(m_repository->getLoan(loanId)->returnedAt.empty());
}

TEST_F(test_core_CirculationDates, ReturnLoanAcceptsToday)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    const std::int64_t loanId =
        rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-3).toIso(), today.addDays(11).toIso());
    ASSERT_GT(loanId, 0);

    const auto mutated = m_repository->returnLoan(loanId, today.toIso());
    ASSERT_TRUE(mutated) << mutated.error().key;
    EXPECT_EQ(m_repository->getLoan(loanId)->returnedAt, today.toIso());
}

TEST_F(test_core_CirculationDates, ReturnLoanRejectsDateBeforeBorrowed)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    const std::int64_t loanId =
        rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-3).toIso(), today.addDays(11).toIso());
    ASSERT_GT(loanId, 0);

    EXPECT_FALSE(m_repository->returnLoan(loanId, today.addDays(-10).toIso()));
    EXPECT_TRUE(m_repository->getLoan(loanId)->returnedAt.empty());
}

TEST_F(test_core_CirculationDates, ExtendLoanRejectsNewDueEqualToCurrentDue)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();
    const std::string due = today.addDays(11).toIso();

    const std::int64_t loanId = rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-3).toIso(), due);
    ASSERT_GT(loanId, 0);

    EXPECT_FALSE(m_repository->extendLoan(loanId, due));
    EXPECT_EQ(m_repository->getLoan(loanId)->dueAt, due);
}

TEST_F(test_core_CirculationDates, ExtendLoanRejectsNewDueBeforeCurrentDue)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();
    const std::string due = today.addDays(11).toIso();

    const std::int64_t loanId = rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-3).toIso(), due);
    ASSERT_GT(loanId, 0);

    EXPECT_FALSE(m_repository->extendLoan(loanId, today.addDays(5).toIso()));
    EXPECT_EQ(m_repository->getLoan(loanId)->dueAt, due);
}

TEST_F(test_core_CirculationDates, ExtendLoanRejectsUnparseableNewDue)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();
    const std::string due = today.addDays(11).toIso();

    const std::int64_t loanId = rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-3).toIso(), due);
    ASSERT_GT(loanId, 0);

    EXPECT_FALSE(m_repository->extendLoan(loanId, "next Tuesday"));
    EXPECT_EQ(m_repository->getLoan(loanId)->dueAt, due);
}

// ---------------------------------------------------------------------------
// SQLite behaviour the Phase D CHECK constraints are designed around
// ---------------------------------------------------------------------------

TEST_F(test_core_CirculationDates, BareYearIsParsedBySqliteAsAJulianDayNotAYear)
{
    // `CHECK (date(x) IS NOT NULL)` is not a validity test. SQLite reads a
    // bare number as a Julian day and returns a real -- and absurd -- date.
    const SqlValue parsed = m_db->scalar("SELECT date('2014')");
    ASSERT_FALSE(parsed.isNull()) << "date('2014') returned NULL; the Julian-day trap is gone";
    EXPECT_EQ(parsed.toString(), "-4707-05-30");

    // Nor does date() reject an impossible calendar date: within a month it
    // ROLLS OVER rather than failing. Both of these are stored verbatim by a
    // CHECK that only asks whether date() returned something.
    EXPECT_EQ(m_db->scalar("SELECT date('2024-02-30')").toString(), "2024-03-01");
    EXPECT_EQ(m_db->scalar("SELECT date('1900-02-29')").toString(), "1900-03-01");

    // It does reject an out-of-range month or a day above 31, which is why
    // the rollover is easy to miss.
    EXPECT_TRUE(m_db->scalar("SELECT date('2024-13-01')").isNull());
    EXPECT_TRUE(m_db->scalar("SELECT date('2024-01-32')").isNull());
}

TEST_F(test_core_CirculationDates, RoundTripIsTheOnlySoundDateGuard)
{
    // The guard Phase D installs on every date column. Two things make it
    // work where the obvious alternatives do not:
    //
    //   * it compares date(x) back to x, so a value SQLite silently rewrote
    //     -- a Julian day, a rolled-over 30 February -- cannot pass; and
    //   * it uses IS rather than =, because a SQLite CHECK is violated only by
    //     a FALSE result. `date('1999-09') = '1999-09'` is NULL, and NULL
    //     PASSES a CHECK. `IS` yields 0 there, which fails as intended.
    struct Row {
        const char* id;
        SqlValue value;
        bool globAndNotNullAccepts;
        bool roundTripAccepts;
    };

    const Row rows[] = {
        {"iso", "2003-03-18", true, true},
        {"maxDate", "9999-12-31", true, true},
        {"bareYear", "2014", false, false},
        {"yearMonth", "1999-09", false, false},
        {"european", "14/08/2020", false, false},
        {"emptyString", "", false, false},
        // A genuine NULL passes both, which is the behaviour a nullable date
        // column wants: returned_at and date_of_birth need no `IS NULL OR ...`
        // wrapper, and a NOT NULL column is already covered by its own NOT NULL.
        {"null", SqlValue::null(), false, true},
        {"unpaddedParts", "2024-1-5", false, false},
        {"monthThirteen", "2024-13-01", false, false},
        // The two that separate the guards: shaped like ISO, accepted by date(),
        // and not real days.
        {"thirtiethOfFebruary", "2024-02-30", true, false},
        {"nonLeapYearFeb29", "1900-02-29", true, false},
    };

    for (const Row& row : rows) {
        SCOPED_TRACE(row.id);

        const SqlBinds binds{{"value", row.value}};

        const bool globAndNotNull =
            m_db->scalar("SELECT (:value GLOB "
                         "'[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]') "
                         "AND date(:value) IS NOT NULL",
                         binds)
                .toInt()
            != 0;
        const bool roundTrip = m_db->scalar("SELECT date(:value) IS :value", binds).toInt() != 0;

        EXPECT_EQ(globAndNotNull, row.globAndNotNullAccepts);
        EXPECT_EQ(roundTrip, row.roundTripAccepts);

        // For any actual text, the round trip is never weaker than the pair, so it
        // fully replaces it. (NULL is the one deliberate exception above.)
        if (!row.value.isNull()) {
            EXPECT_FALSE(roundTrip && !globAndNotNull)
                << "round trip accepted '" << row.value.toString() << "' that the GLOB pair rejected";
        }
    }
}

TEST_F(test_core_CirculationDates, UnparseableDateMakesSqliteDateReturnNull)
{
    EXPECT_TRUE(m_db->scalar("SELECT date('14/08/2020')").isNull());
    EXPECT_TRUE(m_db->scalar("SELECT date('1999-09')").isNull());

    // And NULL loses every comparison, which is the mechanism behind the rows
    // that silently disappear from the overdue filter below.
    const SqlValue comparison = m_db->scalar("SELECT date('14/08/2020') < date('2030-01-01')");
    EXPECT_TRUE(comparison.isNull()) << "a NULL date comparison produced a boolean";
}

// ---------------------------------------------------------------------------
// createLoan's date parse guards (was findings 2, 4, 7)
// ---------------------------------------------------------------------------

TEST_F(test_core_CirculationDates, CreateLoanRejectsUnparseableBorrowedAt)
{
    seedMemberAndCopies();

    LoanInput input;
    input.memberId = m_memberId;
    input.bookCopyId = copyAt(0);
    input.borrowedAt = "14/08/2026";  // dd/MM/yyyy, not ISO
    input.dueAt = "2026-08-28";

    // An invalid QDate carries jd = min(qint64), so it sorts below every real
    // date. The guard `dueDate < borrowedDate` therefore evaluated
    // valid < invalid == false and waved the row straight through. C6 parses
    // both dates first and never compares an unparsed one.
    EXPECT_FALSE(m_repository->createLoan(input));

    EXPECT_EQ(m_db->count("loans"), 0);
}

TEST_F(test_core_CirculationDates, CreateLoanReportsUnparseableDueAtAsAParseError)
{
    seedMemberAndCopies();

    LoanInput input;
    input.memberId = m_memberId;
    input.bookCopyId = copyAt(0);
    input.borrowedAt = "2026-08-14";
    input.dueAt = "28 August 2026";

    // This one was rejected even before C6 -- but only as a side effect of an
    // invalid date sorting lowest, so the librarian was told the due date was
    // before the borrow date when the real problem was that it could not be
    // read at all. Being rejected was never the same as being diagnosed.
    const auto unreadable = m_repository->createLoan(input);
    EXPECT_FALSE(unreadable);
    EXPECT_NE(unreadable.error().key, "error.loan.dueNotAfterBorrow") << unreadable.error().key;
}

TEST_F(test_core_CirculationDates, CreateLoanRejectsDueEqualToBorrowed)
{
    seedMemberAndCopies();

    LoanInput input;
    input.memberId = m_memberId;
    input.bookCopyId = copyAt(0);
    input.borrowedAt = "2026-03-10";
    input.dueAt = "2026-03-10";

    // The guard is a strict `<`, so a zero-day loan -- due the instant it is
    // borrowed, and overdue tomorrow -- is accepted.
    EXPECT_FALSE(m_repository->createLoan(input));

    EXPECT_EQ(m_db->count("loans"), 0);
}

TEST_F(test_core_CirculationDates, CreateLoanRejectsFutureBorrowDate)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    LoanInput input;
    input.memberId = m_memberId;
    input.bookCopyId = copyAt(0);
    input.borrowedAt = today.addDays(30).toIso();
    input.dueAt = today.addDays(44).toIso();

    // A future borrow date is not just odd: with no upper bound on any metrics
    // window, the row then counts toward today, this week AND this month for
    // as long as it exists.
    EXPECT_FALSE(m_repository->createLoan(input));

    EXPECT_EQ(m_db->count("loans"), 0);
}

// ---------------------------------------------------------------------------
// Bad stored dates are refused, not compounded (was finding 2b)
// ---------------------------------------------------------------------------

TEST_F(test_core_CirculationDates, ReturnLoanRejectsWhenStoredBorrowedAtIsUnparseable)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    // '14/08/2026' sorts below an ISO date as a raw string, so the pre-v1
    // `returned_at >= borrowed_at` CHECK -- itself a string compare -- does
    // not catch this either. D3's constraints do, which is why the table has
    // to be put back to the shape a database with such a row actually has.
    ASSERT_TRUE(degradeLoansToPreV1Shape(*m_db));

    const std::int64_t loanId =
        rawInsertLoan(*m_db, m_memberId, copyAt(0), "14/08/2026", today.addDays(14).toIso());
    ASSERT_GT(loanId, 0);

    EXPECT_FALSE(m_repository->returnLoan(loanId, today.toIso()));

    EXPECT_TRUE(m_repository->getLoan(loanId)->returnedAt.empty());
}

TEST_F(test_core_CirculationDates, ExtendLoanRejectsWhenStoredDueAtIsUnparseable)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    ASSERT_TRUE(degradeLoansToPreV1Shape(*m_db));

    const std::int64_t loanId =
        rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-3).toIso(), "sometime next month");
    ASSERT_GT(loanId, 0);

    // `currentDueDate.isValid() && newDueDate <= currentDueDate` short-circuits
    // to false when the stored date is garbage, disabling the monotonicity
    // check entirely. Note that merely deleting the isValid() guard does not
    // fix it: invalid sorts lowest, so `valid <= invalid` is false too. The
    // fix has to reject an unreadable stored due date explicitly.
    //
    // The new date here is deliberately in the FUTURE. C5 gave extendLoan a
    // floor at LoanPolicy::minimumExtensionDate, which rejects a past date
    // whatever is stored -- so the past-date version of this test would now
    // pass without finding 5b being fixed at all. Extending forward is the
    // case the floor cannot see: the repository has no idea what it is
    // extending from, and says yes anyway.
    EXPECT_FALSE(m_repository->extendLoan(loanId, today.addDays(30).toIso()));

    EXPECT_EQ(m_repository->getLoan(loanId)->dueAt, "sometime next month");
}

TEST_F(test_core_CirculationDates, ExtendLoanRejectsAPastDateEvenWhenTheStoredDueAtIsUnparseable)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    ASSERT_TRUE(degradeLoansToPreV1Shape(*m_db));

    const std::int64_t loanId =
        rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-3).toIso(), "sometime next month");
    ASSERT_GT(loanId, 0);

    // The half of the above that C5 does close, kept separate so the two
    // reasons cannot be confused. minimumExtensionDate falls back to today
    // when the current due date is unreadable, so an extension into the past
    // is refused even though the monotonicity check is still disabled.
    const auto failed = m_repository->extendLoan(loanId, "2020-01-01");
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_EQ(m_repository->getLoan(loanId)->dueAt, "sometime next month");
}

// ---------------------------------------------------------------------------
// [XF] rows with bad dates vanish instead of surfacing
// ---------------------------------------------------------------------------

TEST_F(test_core_CirculationDates, LoanWithUnparseableDueDateIsNotSilentlyExcludedFromOverdueFilter)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    // Borrowed two months ago, due date unreadable. Whatever it means, the
    // book has been out for sixty days and somebody needs to look at it.
    // Only a database the pre-flight refused to migrate can hold this row --
    // which is exactly the database that needs the predicate to notice it.
    ASSERT_TRUE(degradeLoansToPreV1Shape(*m_db));
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-60).toIso(), "14/08/2020"), 0);

    // It is not Open -- nobody can say it is still within its due date...
    EXPECT_EQ(countWithFilter(LoanFilter::kOpen), 0);

    // ...and it used to stop there: date('14/08/2020') is NULL, NULL < :today
    // is NULL, and the WHERE clause dropped it, so the loan was invisible to
    // the overdue filter for ever -- strictly worse than an error, because
    // nobody found out. The predicate now counts an unreadable due date as
    // overdue rather than as fine.
    EXPECT_EQ(countWithFilter(LoanFilter::kOverdue), 1);
}

TEST_F(test_core_CirculationDates, LoanWithUnparseableDueDateIsFlaggedForAttention)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    ASSERT_TRUE(degradeLoansToPreV1Shape(*m_db));
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-60).toIso(), "14/08/2020"), 0);

    const LoanQuery unfiltered;
    const auto loans = VLMS_UNWRAP(m_repository->listLoans(unfiltered));
    ASSERT_EQ(static_cast<int>(loans.size()), 1);

    // Same defect seen from the list rather than the count: the CASE fell
    // through to ELSE 0, so the row rendered as a perfectly healthy loan.
    EXPECT_TRUE(loans.front().isOverdue);
}

// ---------------------------------------------------------------------------
// extendLoan's floor at today (was finding 5)
// ---------------------------------------------------------------------------

TEST_F(test_core_CirculationDates, ExtendLoanRejectsNewDueBeforeToday)
{
    seedMemberAndCopies();

    // A long-overdue open loan.
    const std::int64_t loanId = rawInsertLoan(*m_db, m_memberId, copyAt(0), "2019-12-18", "2020-01-01");
    ASSERT_GT(loanId, 0);

    // LoanExtendDialog clamped its minimum date to today; the repository only
    // checked that the new date was after the current one, so Core accepted an
    // "extension" still years in the past -- a loan overdue the instant it was
    // granted. The two rules disagreed and only one of them was enforced
    // anywhere but the UI. Both now call LoanPolicy::minimumExtensionDate.
    EXPECT_FALSE(m_repository->extendLoan(loanId, "2020-01-02"));

    EXPECT_EQ(m_repository->getLoan(loanId)->dueAt, "2020-01-01");
}

// ---------------------------------------------------------------------------
// [XF] nothing stops two open loans on one copy
// ---------------------------------------------------------------------------

TEST_F(test_core_CirculationDates, TwoOpenLoansOnOneCopyAreImpossible)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.toIso(), today.addDays(14).toIso()), 0);

    // createLoan's copyIsAvailable() check catches this -- see
    // CreateLoanRejectsACopyAlreadyOnLoan above -- but it was a check-then-act
    // with nothing underneath it, so anything writing to loans without going
    // through the repository, and any future concurrent writer, could put one
    // copy on loan twice. The insert below bypasses the repository entirely,
    // which is the whole point: what stops it now is the database.
    const std::int64_t second =
        rawInsertLoan(*m_db, m_memberId, copyAt(0), today.toIso(), today.addDays(14).toIso());

    EXPECT_EQ(second, std::int64_t(0));
    EXPECT_EQ(m_db->count("loans"), 1);
}

TEST_F(test_core_CirculationDates, AReturnedCopyCanBeBorrowedAgain)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    const std::int64_t first = rawInsertLoan(*m_db, m_memberId, copyAt(0), today.addDays(-30).toIso(),
                                             today.addDays(-16).toIso(), today.addDays(-20).toIso());
    ASSERT_GT(first, 0);

    // The index is unique only WHERE returned_at IS NULL. A plain unique index
    // on book_copy_id would have made a copy borrowable exactly once in the
    // life of the library, so this is the half of finding 8's fix that says
    // the constraint is the right one and not merely a strict one.
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.toIso(), today.addDays(14).toIso()), 0);
    EXPECT_EQ(m_db->count("loans"), 2);
}

// ---------------------------------------------------------------------------
// The date CHECK constraints (D3)
// ---------------------------------------------------------------------------

TEST_F(test_core_CirculationDates, ABareYearIsRejectedAsALoanDate)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    // The trap the whole round-trip design exists for. SQLite reads a bare
    // number as a Julian day, so date('2014') is '-4707-05-30' -- not NULL,
    // and a `date(x) IS NOT NULL` guard would have waved it through.
    EXPECT_EQ(rawInsertLoan(*m_db, m_memberId, copyAt(0), "2014", today.addDays(14).toIso()),
              std::int64_t(0));
    EXPECT_EQ(m_db->count("loans"), 0);
}

TEST_F(test_core_CirculationDates, AnImpossibleCalendarDateIsRejectedAsALoanDate)
{
    seedMemberAndCopies();

    // The second trap: date('2024-02-30') is not NULL either, it is
    // '2024-03-01'. SQLite rejects a month above 12 or a day above 31 and
    // silently rolls over everything else, which is what makes this easy to
    // miss. Comparing date(x) back to x is what catches it.
    EXPECT_EQ(rawInsertLoan(*m_db, m_memberId, copyAt(0), "2024-02-30", "2024-03-15"), std::int64_t(0));
    EXPECT_EQ(rawInsertLoan(*m_db, m_memberId, copyAt(0), "1900-02-29", "1900-03-15"), std::int64_t(0));
    EXPECT_EQ(m_db->count("loans"), 0);
}

TEST_F(test_core_CirculationDates, APartialDateIsRejectedAsALoanDate)
{
    seedMemberAndCopies();

    // date('1999-09') is NULL, and `NULL = '1999-09'` is NULL -- which a CHECK
    // treats as a pass, because only FALSE violates it. This is why the
    // constraint is written with IS rather than =.
    EXPECT_EQ(rawInsertLoan(*m_db, m_memberId, copyAt(0), "1999-09", "1999-10-01"), std::int64_t(0));
    EXPECT_EQ(m_db->count("loans"), 0);
}

TEST_F(test_core_CirculationDates, AZeroDayLoanIsRejectedByTheDatabase)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    // C6 already refuses this in createLoan. The constraint is what makes it
    // true of the table rather than of one code path.
    EXPECT_EQ(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.toIso(), today.toIso()), std::int64_t(0));
    EXPECT_EQ(m_db->count("loans"), 0);
}

TEST_F(test_core_CirculationDates, ANullReturnDateStillPassesTheRoundTrip)
{
    seedMemberAndCopies();
    const Date today = Date::todayLocal();

    // date(NULL) is NULL and NULL IS NULL is true, so a nullable date column
    // needs no `IS NULL OR ...` wrapper. If it did, no open loan could exist
    // at all -- which is the failure mode this pins.
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyAt(0), today.toIso(), today.addDays(14).toIso()), 0);
    EXPECT_EQ(m_db->count("loans"), 1);
}
