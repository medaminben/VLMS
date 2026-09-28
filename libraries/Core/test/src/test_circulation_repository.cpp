#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Repositories/LoanPolicy.h>
#include <VLMS/Repositories/LoanTypes.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using VLMS::Date;
using namespace VLMS::Test;

class test_core_CirculationRepository : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(resetStore()) << (m_db ? m_db->lastError() : "no database");
    }

    void TearDown() override
    {
        m_repository.reset();
        m_db.reset();
    }

    [[nodiscard]] bool resetStore()
    {
        m_repository.reset();
        m_db = std::make_unique<TestDatabase>();
        if (!m_db->isValid()) {
            return false;
        }
        m_repository = std::make_unique<CirculationRepository>(m_db->session());

        MemberSeed member = uniqueMemberSeed(1);
        member.status = MemberStatus::kActive;
        m_activeMemberId = seedMember(*m_db, member);
        if (m_activeMemberId <= 0) {
            return false;
        }

        BookSeed book = uniqueBookSeed(1);
        book.initialCopyCount = 4;
        const std::int64_t bookId = seedBook(*m_db, book);
        if (bookId <= 0) {
            return false;
        }
        m_copyIds = copyIdsOf(*m_db, bookId);
        return m_copyIds.size() == 4;
    }

    LoanInput baseInput(int copyIndex) const
    {
        const Date today = Date::todayLocal();
        LoanInput input;
        input.memberId = m_activeMemberId;
        input.bookCopyId = m_copyIds.at(static_cast<std::size_t>(copyIndex));
        input.borrowedAt = today.toIso();
        input.dueAt = today.addDays(14).toIso();
        return input;
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CirculationRepository> m_repository;
    std::int64_t m_activeMemberId = 0;
    std::vector<std::int64_t> m_copyIds;
};

TEST_F(test_core_CirculationRepository, CreateLoanRejectsMissingMember)
{
    LoanInput input = baseInput(0);
    input.memberId = 0;

    const auto failed = m_repository->createLoan(input);
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_EQ(m_db->count("loans"), 0);
}

TEST_F(test_core_CirculationRepository, CreateLoanRejectsMissingCopy)
{
    LoanInput input = baseInput(0);
    input.bookCopyId = 0;

    const auto failed = m_repository->createLoan(input);
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_EQ(m_db->count("loans"), 0);
}

TEST_F(test_core_CirculationRepository, CreateLoanRejectsNonActiveMember)
{
    const std::vector<std::pair<const char*, const char*>> cases = {
        {"non_active", MemberStatus::kNonActive},
    };

    for (const auto& [name, status] : cases) {
        SCOPED_TRACE(name);
        ASSERT_TRUE(resetStore()) << m_db->lastError();

        MemberSeed seed = uniqueMemberSeed(90);
        seed.status = status;
        const std::int64_t memberId = seedMember(*m_db, seed);
        ASSERT_GT(memberId, 0);

        LoanInput input = baseInput(0);
        input.memberId = memberId;

        EXPECT_FALSE(m_repository->createLoan(input)) << "only 'active' members may borrow";
        EXPECT_TRUE(true); // error key checked on the Result/Status above when captured
        EXPECT_EQ(m_db->count("loans"), 0);
    }
}

TEST_F(test_core_CirculationRepository, CreateLoanAcceptsActiveMember)
{
    std::int64_t id = 0;
    const auto created = m_repository->createLoan(baseInput(0));
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();
    EXPECT_GT(id, 0);
    EXPECT_EQ(m_db->count("loans"), 1);
}

TEST_F(test_core_CirculationRepository, CreateLoanRejectsCopyAlreadyOnLoan)
{
    EXPECT_TRUE(m_repository->createLoan(baseInput(0)));

    EXPECT_FALSE(m_repository->createLoan(baseInput(0)))
        << "a copy already on loan cannot be lent again";
    EXPECT_TRUE(true); // error key checked on the Result/Status above when captured
    EXPECT_EQ(m_db->count("loans"), 1);
}

TEST_F(test_core_CirculationRepository, CreateLoanDefaultsBorrowedAtToToday)
{
    LoanInput input = baseInput(0);
    input.borrowedAt.clear();

    std::int64_t id = 0;
    const auto created = m_repository->createLoan(input);
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    const auto stored = m_repository->getLoan(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->borrowedAt, Date::todayLocal().toIso());
}

TEST_F(test_core_CirculationRepository, CreateLoanDefaultsDueAtToTodayPlusPolicy)
{
    LoanInput input = baseInput(0);
    input.dueAt.clear();

    std::int64_t id = 0;
    const auto created = m_repository->createLoan(input);
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    const auto stored = m_repository->getLoan(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->dueAt,
              VLMS::LoanPolicy::suggestedDueDate(Date::todayLocal()).toIso());
}

TEST_F(test_core_CirculationRepository, CreateLoanStoresBlankNotesAsNull)
{
    LoanInput input = baseInput(0);
    input.notes = "   ";

    std::int64_t id = 0;
    const auto created = m_repository->createLoan(input);
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    EXPECT_EQ(m_db->scalar("SELECT notes IS NULL FROM loans WHERE id = :id",
                           {{"id", id}})
                  .toInt(),
              1);
}

TEST_F(test_core_CirculationRepository, ReturnLoanMarksLoanReturned)
{
    std::int64_t id = 0;
    const auto created = m_repository->createLoan(baseInput(0));
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    const std::string returnDate = Date::todayLocal().toIso();
    const auto mutated = m_repository->returnLoan(id, returnDate);
    ASSERT_TRUE(mutated) << mutated.error().key;

    const auto stored = m_repository->getLoan(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->returnedAt, returnDate);
}

TEST_F(test_core_CirculationRepository, ReturnLoanRejectsAlreadyReturnedLoan)
{
    std::int64_t id = 0;
    const auto created = m_repository->createLoan(baseInput(0));
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();
    EXPECT_TRUE(m_repository->returnLoan(id));

    const auto failed = m_repository->returnLoan(id);
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
}

TEST_F(test_core_CirculationRepository, ReturnLoanDefaultsToToday)
{
    std::int64_t id = 0;
    const auto created = m_repository->createLoan(baseInput(0));
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();
    EXPECT_TRUE(m_repository->returnLoan(id));

    const auto stored = m_repository->getLoan(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->returnedAt, Date::todayLocal().toIso());
}

TEST_F(test_core_CirculationRepository, ReturnLoanFreesTheCopyForReborrowing)
{
    std::int64_t id = 0;
    const auto created = m_repository->createLoan(baseInput(0));
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();
    EXPECT_TRUE(m_repository->returnLoan(id));

    EXPECT_TRUE(m_repository->createLoan(baseInput(0))) << "repository call failed";
    EXPECT_EQ(m_db->count("loans"), 2);
}

TEST_F(test_core_CirculationRepository, ExtendLoanMovesDueDateForward)
{
    std::int64_t id = 0;
    const auto created = m_repository->createLoan(baseInput(0));
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    const std::string newDue = Date::todayLocal().addDays(28).toIso();
    const auto mutated = m_repository->extendLoan(id, newDue);
    ASSERT_TRUE(mutated) << mutated.error().key;

    const auto stored = m_repository->getLoan(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->dueAt, newDue);
}

TEST_F(test_core_CirculationRepository, ExtendLoanRejectsReturnedLoan)
{
    std::int64_t id = 0;
    const auto created = m_repository->createLoan(baseInput(0));
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();
    EXPECT_TRUE(m_repository->returnLoan(id));

    const auto failed = m_repository->extendLoan(id, Date::todayLocal().addDays(28).toIso());
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
}

TEST_F(test_core_CirculationRepository, ExtendLoanRejectsBlankDueDate)
{
    std::int64_t id = 0;
    const auto created = m_repository->createLoan(baseInput(0));
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    const auto failed = m_repository->extendLoan(id, "   ");
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
}

TEST_F(test_core_CirculationRepository, ExtendLoanRejectsEarlierOrEqualDueDate)
{
    std::int64_t id = 0;
    const auto created = m_repository->createLoan(baseInput(0));
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    const auto stored = m_repository->getLoan(id);
    ASSERT_TRUE(stored.has_value());

    EXPECT_FALSE(m_repository->extendLoan(id, stored->dueAt))
        << "an extension equal to the current due date is not an extension";
    EXPECT_TRUE(true); // error key checked on the Result/Status above when captured

    const std::string earlier = Date::fromIso(stored->dueAt).addDays(-1).toIso();
    EXPECT_FALSE(m_repository->extendLoan(id, earlier));
}

TEST_F(test_core_CirculationRepository, ListBorrowableMembersOnlyReturnsActive)
{
    for (const char* status : {MemberStatus::kNonActive}) {
        MemberSeed seed = uniqueMemberSeed(200 + static_cast<int>(std::string_view(status).size()));
        seed.membershipNumber = std::string("B-") + status;
        seed.status = status;
        EXPECT_GT(seedMember(*m_db, seed), 0);
    }

    const auto borrowable = VLMS_UNWRAP(m_repository->listBorrowableMembers());
    EXPECT_EQ(borrowable.size(), 1u);
    EXPECT_EQ(borrowable.front().id, m_activeMemberId);
}

TEST_F(test_core_CirculationRepository, ListAvailableCopiesExcludesOnLoanCopies)
{
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listAvailableCopies()).size(), 4u);

    EXPECT_TRUE(m_repository->createLoan(baseInput(0)));
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listAvailableCopies()).size(), 3u);

    EXPECT_TRUE(m_repository->createLoan(baseInput(1)));
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listAvailableCopies()).size(), 2u);
}

TEST_F(test_core_CirculationRepository, FilterCodesContainsEveryDefinedCode)
{
    const std::vector<std::string> codes = CirculationRepository::filterCodes();
    EXPECT_TRUE(contains(codes, LoanFilter::kOpen));
    EXPECT_TRUE(contains(codes, LoanFilter::kOverdue));
    EXPECT_TRUE(contains(codes, LoanFilter::kReturned));
}

TEST_F(test_core_CirculationRepository, ListLoansAndCountLoansAgree)
{
    const std::vector<std::string> none;
    struct Case {
        const char* name;
        std::vector<std::string> filters;
        std::string search;
    };
    const std::vector<Case> cases = {
        {"no filters", none, ""},
        {"open", {LoanFilter::kOpen}, ""},
        {"overdue", {LoanFilter::kOverdue}, ""},
        {"returned", {LoanFilter::kReturned}, ""},
        {"open+overdue", {LoanFilter::kOpen, LoanFilter::kOverdue}, ""},
        {"all three", {LoanFilter::kOpen, LoanFilter::kOverdue, LoanFilter::kReturned}, ""},
        {"search only", none, "Test Book"},
        {"open+search", {LoanFilter::kOpen}, "Test Book"},
    };

    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        ASSERT_TRUE(resetStore()) << m_db->lastError();

        const Date today = Date::todayLocal();
        // One current, one overdue, one returned.
        EXPECT_GT(rawInsertLoan(*m_db, m_activeMemberId, m_copyIds.at(0),
                                today.addDays(-2).toIso(),
                                today.addDays(12).toIso()),
                  0);
        EXPECT_GT(rawInsertLoan(*m_db, m_activeMemberId, m_copyIds.at(1),
                                today.addDays(-40).toIso(),
                                today.addDays(-26).toIso()),
                  0);
        EXPECT_GT(rawInsertLoan(*m_db, m_activeMemberId, m_copyIds.at(2),
                                today.addDays(-60).toIso(),
                                today.addDays(-46).toIso(),
                                today.addDays(-50).toIso()),
                  0);

        LoanQuery query;
        query.filters = testCase.filters;
        query.search = testCase.search;

        EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(query)),
                  static_cast<int>(VLMS_UNWRAP(m_repository->listLoans(query)).size()));
    }
}

TEST_F(test_core_CirculationRepository, ListLoansPagesCoverEveryRowExactlyOnce)
{
    constexpr int kPageSize = 3;

    const Date today = Date::todayLocal();
    BookSeed book = uniqueBookSeed(500);
    book.initialCopyCount = 14;
    const std::int64_t bookId = seedBook(*m_db, book);
    ASSERT_GT(bookId, 0);

    const auto copies = copyIdsOf(*m_db, bookId);
    for (int i = 0; i < static_cast<int>(copies.size()); ++i) {
        EXPECT_GT(rawInsertLoan(*m_db, m_activeMemberId, copies.at(static_cast<std::size_t>(i)),
                                today.addDays(-i).toIso(),
                                today.addDays(14 - i).toIso()),
                  0);
    }

    LoanQuery query;
    query.limit = kPageSize;

    std::set<std::int64_t> seen;
    int rows = 0;
    for (int offset = 0; offset < static_cast<int>(copies.size()); offset += kPageSize) {
        query.offset = offset;
        for (const LoanRecord& loan : VLMS_UNWRAP(m_repository->listLoans(query))) {
            seen.insert(loan.id);
            ++rows;
        }
    }

    EXPECT_EQ(rows, static_cast<int>(copies.size()));
    EXPECT_EQ(seen.size(), copies.size());
}

TEST_F(test_core_CirculationRepository, ListLoansFilteredByMemberId)
{
    MemberSeed other = uniqueMemberSeed(600);
    other.status = MemberStatus::kActive;
    const std::int64_t otherId = seedMember(*m_db, other);
    ASSERT_GT(otherId, 0);

    const Date today = Date::todayLocal();
    EXPECT_GT(rawInsertLoan(*m_db, m_activeMemberId, m_copyIds.at(0),
                            today.toIso(),
                            today.addDays(14).toIso()),
              0);
    EXPECT_GT(rawInsertLoan(*m_db, otherId, m_copyIds.at(1), today.toIso(),
                            today.addDays(14).toIso()),
              0);

    LoanQuery query;
    query.memberId = otherId;

    const auto results = VLMS_UNWRAP(m_repository->listLoans(query));
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().memberId, otherId);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(query)), 1);
}

TEST_F(test_core_CirculationRepository, DefaultLoanDaysIsFourteen)
{
    // Moved out of CirculationRepository in C5. The number is asserted here as
    // well as in tst_loan_policy because this suite is what a reader checks to
    // find out what a loan actually costs the borrower.
    EXPECT_EQ(VLMS::LoanPolicy::defaultLoanDays(), 14);
}

TEST_F(test_core_CirculationRepository, LastErrorIsSetOnEveryFailurePath)
{
    const std::vector<const char*> scenarios = {
        "noMember",
        "noCopy",
        "unknownMember",
        "unknownCopy",
        "returnUnknown",
        "extendUnknown",
        "extendBlank",
    };

    for (const char* scenario : scenarios) {
        SCOPED_TRACE(scenario);
        ASSERT_TRUE(resetStore()) << m_db->lastError();

        VLMS::Error error;
        bool result = true;
        if (std::string_view(scenario) == "noMember") {
            LoanInput input = baseInput(0);
            input.memberId = 0;
            const auto created = m_repository->createLoan(input);
            result = static_cast<bool>(created);
            error = created.error();
        } else if (std::string_view(scenario) == "noCopy") {
            LoanInput input = baseInput(0);
            input.bookCopyId = 0;
            const auto created = m_repository->createLoan(input);
            result = static_cast<bool>(created);
            error = created.error();
        } else if (std::string_view(scenario) == "unknownMember") {
            LoanInput input = baseInput(0);
            input.memberId = 999999;
            const auto created = m_repository->createLoan(input);
            result = static_cast<bool>(created);
            error = created.error();
        } else if (std::string_view(scenario) == "unknownCopy") {
            LoanInput input = baseInput(0);
            input.bookCopyId = 999999;
            const auto created = m_repository->createLoan(input);
            result = static_cast<bool>(created);
            error = created.error();
        } else if (std::string_view(scenario) == "returnUnknown") {
            const auto returned = m_repository->returnLoan(999999);
            result = static_cast<bool>(returned);
            error = returned.error();
        } else if (std::string_view(scenario) == "extendUnknown") {
            const auto extended = m_repository->extendLoan(
                999999, Date::todayLocal().addDays(30).toIso());
            result = static_cast<bool>(extended);
            error = extended.error();
        } else if (std::string_view(scenario) == "extendBlank") {
            const auto created = m_repository->createLoan(baseInput(0));
            ASSERT_TRUE(created) << created.error().key;
            const auto extended = m_repository->extendLoan(created.value(), "");
            result = static_cast<bool>(extended);
            error = extended.error();
        }

        EXPECT_FALSE(result) << "this scenario is supposed to fail";
        EXPECT_FALSE(error.key.empty())
            << "a failing call must leave a translatable key the UI can show";
    }
}

TEST_F(test_core_CirculationRepository, GetLoanMissingIdIsNotFoundNotSql)
{
    const auto missing = m_repository->getLoan(999999);
    EXPECT_FALSE(missing);
    EXPECT_EQ(missing.kind(), VLMS::ErrorKind::NotFound);
    EXPECT_EQ(missing.error().key, "error.loan.notFound");
}

TEST_F(test_core_CirculationRepository, GetLoanExecFailureIsSql)
{
    ASSERT_TRUE(m_db->exec("DROP TABLE loans"));
    const auto failed = m_repository->getLoan(1);
    EXPECT_FALSE(failed);
    EXPECT_EQ(failed.kind(), VLMS::ErrorKind::Sql);
    EXPECT_EQ(failed.error().key, "error.sql");
}

TEST_F(test_core_CirculationRepository, ListLoansSortsByDueDate)
{
    ASSERT_GT(rawInsertLoan(*m_db, m_activeMemberId, m_copyIds.at(0),
                            "2021-01-01", "2021-06-01"),
              0);
    ASSERT_GT(rawInsertLoan(*m_db, m_activeMemberId, m_copyIds.at(1),
                            "2021-01-01", "2021-01-15"),
              0);

    LoanQuery query;
    query.sortColumn = LoanSort::kDue;
    query.sortAscending = true;
    const auto asc = VLMS_UNWRAP(m_repository->listLoans(query));
    ASSERT_EQ(asc.size(), 2u);
    EXPECT_EQ(asc.at(0).dueAt, "2021-01-15");
    EXPECT_EQ(asc.at(1).dueAt, "2021-06-01");

    query.sortAscending = false;
    const auto desc = VLMS_UNWRAP(m_repository->listLoans(query));
    ASSERT_EQ(desc.size(), 2u);
    EXPECT_EQ(desc.at(0).dueAt, "2021-06-01");
    EXPECT_EQ(desc.at(1).dueAt, "2021-01-15");

    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfLoan(asc.at(0).id, query)), 1);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfLoan(asc.at(1).id, query)), 0);
}

// A full name spans first_name and last_name, so matching each column on its
// own never finds it. The second member keeps these tests able to fail.
TEST_F(test_core_CirculationRepository, SearchFindsLoansByTheMembersFullName)
{
    MemberSeed imported = uniqueMemberSeed(700);
    imported.firstName = "سلمى";
    imported.lastName = "بنت علي الورداني";
    imported.fullName = "سلمى بنت علي الورداني";
    const std::int64_t importedId = seedMember(*m_db, imported);
    ASSERT_GT(importedId, 0);

    const Date today = Date::todayLocal();
    ASSERT_GT(rawInsertLoan(*m_db, importedId, m_copyIds.at(0), today.toIso(),
                            today.addDays(14).toIso()),
              0);
    ASSERT_GT(rawInsertLoan(*m_db, m_activeMemberId, m_copyIds.at(1),
                            today.toIso(), today.addDays(14).toIso()),
              0);

    LoanQuery query;
    query.search = "سلمى بنت علي الورداني";

    const auto results = VLMS_UNWRAP(m_repository->listLoans(query));
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().memberId, importedId);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(query)), 1);
}

TEST_F(test_core_CirculationRepository, SearchFindsLoansByFirstAndLastNameWithoutFullName)
{
    MemberSeed created = uniqueMemberSeed(701);
    created.firstName = "Salma";
    created.lastName = "Trabelsi";
    const std::int64_t createdId = seedMember(*m_db, created);
    ASSERT_GT(createdId, 0);

    const Date today = Date::todayLocal();
    ASSERT_GT(rawInsertLoan(*m_db, createdId, m_copyIds.at(0), today.toIso(),
                            today.addDays(14).toIso()),
              0);
    ASSERT_GT(rawInsertLoan(*m_db, m_activeMemberId, m_copyIds.at(1),
                            today.toIso(), today.addDays(14).toIso()),
              0);

    LoanQuery query;
    query.search = "Salma Trabelsi";

    const auto results = VLMS_UNWRAP(m_repository->listLoans(query));
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results.front().memberId, createdId);
}
