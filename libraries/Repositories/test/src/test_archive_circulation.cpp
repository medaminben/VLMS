#include "TestDatabase.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using VLMS::Date;
using VLMS::DateTime;
using VLMS::ScopedClock;
using namespace VLMS;
using namespace Test;

class test_core_ArchiveCirculation : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_GT(m_memberId, 0);
    }

    void TearDown() override
    {
        m_repository.reset();
        m_db.reset();
    }

    std::int64_t firstCopyOfNewBook(int index)
    {
        const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(index));
        return copyIdsOf(*m_db, bookId).front();
    }

    std::int64_t returnedLoan(int index)
    {
        return rawInsertLoan(*m_db, m_memberId, firstCopyOfNewBook(index), "2026-09-01",
                             "2026-09-15", "2026-09-10");
    }

    void rawArchive(const char* table, std::int64_t id)
    {
        ASSERT_TRUE(m_db->exec(std::string("UPDATE ") + table
                               + " SET archived_at = '2026-09-19 10:00:00' WHERE id = "
                               + std::to_string(id)));
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CirculationRepository> m_repository;
    std::int64_t m_memberId = 0;
};

TEST_F(test_core_ArchiveCirculation, ArchivingAReturnedLoanMovesItOutOfCirculation)
{
    const std::int64_t loanId = returnedLoan(1);
    const ScopedClock pinned(DateTime(Date(2026, 9, 19), 10, 0, 0));
    ASSERT_TRUE(m_repository->archiveLoan(loanId));

    Repositories::LoanQuery live;
    live.filters = {Repositories::LoanFilter::kAll};
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(live)), 0);

    Repositories::LoanQuery archive;
    archive.archive = Repositories::ArchiveScope::Archived;
    const auto rows = VLMS_UNWRAP(m_repository->listLoans(archive));
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows.front().id, loanId);
    EXPECT_EQ(rows.front().archivedAt, "2026-09-19 10:00:00");
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfLoan(loanId, archive)), 0);
}

TEST_F(test_core_ArchiveCirculation, AnOpenLoanCannotBeArchived)
{
    const std::int64_t loanId =
        rawInsertLoan(*m_db, m_memberId, firstCopyOfNewBook(2), "2026-09-01", "2026-09-15");

    const auto refused = m_repository->archiveLoan(loanId);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.loan.archiveOpen");
    EXPECT_TRUE(
        m_db->scalar("SELECT archived_at FROM loans WHERE id = " + std::to_string(loanId)).isNull());
}

TEST_F(test_core_ArchiveCirculation, RestoreReturnsTheLoanToCirculationAsClosed)
{
    const std::int64_t loanId = returnedLoan(3);
    ASSERT_TRUE(m_repository->archiveLoan(loanId));
    ASSERT_TRUE(m_repository->restoreLoan(loanId));

    Repositories::LoanQuery returned;
    returned.filters = {Repositories::LoanFilter::kReturned};
    const auto rows = VLMS_UNWRAP(m_repository->listLoans(returned));
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows.front().id, loanId);
    EXPECT_TRUE(rows.front().archivedAt.empty());
}

TEST_F(test_core_ArchiveCirculation, RestoreOfALiveLoanIsRefused)
{
    const auto refused = m_repository->restoreLoan(returnedLoan(4));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.loan.notArchived");
}

TEST_F(test_core_ArchiveCirculation, MemberHistorySeesArchivedLoans)
{
    const std::int64_t archived = returnedLoan(5);
    returnedLoan(6);
    ASSERT_TRUE(m_repository->archiveLoan(archived));

    Repositories::LoanQuery history;
    history.memberId = m_memberId;
    history.archive = Repositories::ArchiveScope::Any;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(history)), 2);
    history.archive = Repositories::ArchiveScope::Live;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(history)), 1);
}

TEST_F(test_core_ArchiveCirculation, CheckoutPickerSkipsArchivedCopiesAndCopiesOfArchivedBooks)
{
    const std::int64_t liveCopy = firstCopyOfNewBook(7);
    const std::int64_t archivedCopy = firstCopyOfNewBook(8);
    const std::int64_t archivedBook = seedBook(*m_db, uniqueBookSeed(9));
    rawArchive("book_copies", archivedCopy);
    rawArchive("books", archivedBook);

    const auto options = VLMS_UNWRAP(m_repository->listAvailableCopies());
    std::vector<std::int64_t> ids;
    for (const Repositories::LoanCopyOption& option : options) {
        ids.push_back(option.id);
    }
    EXPECT_EQ(ids, std::vector<std::int64_t>{liveCopy});
}

TEST_F(test_core_ArchiveCirculation, CreateLoanRefusesAnArchivedCopy)
{
    const std::int64_t copyId = firstCopyOfNewBook(10);
    rawArchive("book_copies", copyId);
    const ScopedClock pinned(Date(2026, 9, 19));

    Repositories::LoanInput input;
    input.memberId = m_memberId;
    input.bookCopyId = copyId;
    input.borrowedAt = "2026-09-19";
    input.dueAt = "2026-10-03";
    const auto refused = m_repository->createLoan(input);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.loan.copyArchived");
}
