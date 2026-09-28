#include "TestDatabase.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace VLMS::Test;
using VLMS::Status;

namespace {

void expectRefusal(const Status& check, const Status& action, const char* key)
{
    ASSERT_FALSE(check) << key;
    ASSERT_FALSE(action) << key;
    EXPECT_EQ(check.error().key, key);
    EXPECT_EQ(action.error().key, key);
}

}  // namespace

class test_core_CanRemove : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_circulation.reset();
        m_catalog.reset();
        m_members.reset();
        m_db.reset();
    }

    [[nodiscard]] bool bookIsLive(std::int64_t id) const
    {
        return m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(id)).isNull();
    }

    [[nodiscard]] bool memberIsLive(std::int64_t id) const
    {
        return m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(id))
            .isNull();
    }

    [[nodiscard]] bool loanIsLive(std::int64_t id) const
    {
        return m_db->scalar("SELECT archived_at FROM loans WHERE id = " + std::to_string(id)).isNull();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MemberRepository> m_members;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<CirculationRepository> m_circulation;
};

TEST_F(test_core_CanRemove, ArchiveBookPassesWhenNothingIsOut)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(1));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(2));

    EXPECT_TRUE(m_catalog->canArchiveBook(bookId));
    EXPECT_TRUE(m_catalog->archiveBook(bookId));

    EXPECT_FALSE(bookIsLive(bookId));
    EXPECT_TRUE(bookIsLive(bystander));
}

TEST_F(test_core_CanRemove, ArchiveBookRefusesACopyThatIsStillOut)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(3));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(4));
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(3));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                            "2026-09-15"),
              0);

    expectRefusal(m_catalog->canArchiveBook(bookId), m_catalog->archiveBook(bookId),
                  "error.book.hasActiveLoans");
    EXPECT_TRUE(bookIsLive(bookId));
    EXPECT_TRUE(bookIsLive(bystander));
}

TEST_F(test_core_CanRemove, ArchiveBookReportsAMissingTitleAsNotFound)
{
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(5));

    expectRefusal(m_catalog->canArchiveBook(bystander + 1000),
                  m_catalog->archiveBook(bystander + 1000), "error.book.notFound");
    EXPECT_TRUE(bookIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeBookRefusesALiveTitle)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(6));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(7));

    expectRefusal(m_catalog->canPurgeBook(bookId), m_catalog->purgeBook(bookId),
                  "error.book.notArchived");
    EXPECT_TRUE(bookIsLive(bookId));
    EXPECT_TRUE(bookIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeBookRefusesAnArchivedTitleThatStillHasCopies)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(8));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(9));
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    expectRefusal(m_catalog->canPurgeBook(bookId), m_catalog->purgeBook(bookId),
                  "error.book.hasCopies");
    EXPECT_FALSE(bookIsLive(bookId));
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE book_id = " + std::to_string(bookId))
                  .toInt(),
              1);
    EXPECT_TRUE(bookIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeBookPassesOnceTheArchivedTitleHasNoCopies)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(10));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(11));
    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    ASSERT_TRUE(m_catalog->purgeCopy(copyIdsOf(*m_db, bookId).front()));

    EXPECT_TRUE(m_catalog->canPurgeBook(bookId));
    EXPECT_TRUE(m_catalog->purgeBook(bookId));
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bookId)).toInt(),
              0);
    EXPECT_TRUE(bookIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeBookReportsAMissingTitleAsNotFound)
{
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(12));

    expectRefusal(m_catalog->canPurgeBook(bystander + 1000), m_catalog->purgeBook(bystander + 1000),
                  "error.book.notFound");
    EXPECT_TRUE(bookIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeCopyRefusesALiveCopy)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(13));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(14));
    const std::int64_t copyId = copyIdsOf(*m_db, bookId).front();

    expectRefusal(m_catalog->canPurgeCopy(copyId), m_catalog->purgeCopy(copyId),
                  "error.copy.notArchived");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = " + std::to_string(copyId))
                  .toInt(),
              1);
    EXPECT_EQ(copyIdsOf(*m_db, bystander).size(), 1u);
}

TEST_F(test_core_CanRemove, PurgeCopyRefusesAnArchivedCopyThatWasLent)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(15));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(16));
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(15));
    const std::int64_t copyId = copyIdsOf(*m_db, bookId).front();
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyId, "2026-09-01", "2026-09-15", "2026-09-10"), 0);
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    expectRefusal(m_catalog->canPurgeCopy(copyId), m_catalog->purgeCopy(copyId),
                  "error.copy.hasHistory");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = " + std::to_string(copyId))
                  .toInt(),
              1);
    EXPECT_EQ(copyIdsOf(*m_db, bystander).size(), 1u);
}

TEST_F(test_core_CanRemove, PurgeCopyPassesForAnArchivedCopyNobodyBorrowed)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(17));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(18));
    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    const std::int64_t copyId = copyIdsOf(*m_db, bookId).front();

    EXPECT_TRUE(m_catalog->canPurgeCopy(copyId));
    EXPECT_TRUE(m_catalog->purgeCopy(copyId));
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = " + std::to_string(copyId))
                  .toInt(),
              0);
    EXPECT_EQ(copyIdsOf(*m_db, bystander).size(), 1u);
}

TEST_F(test_core_CanRemove, PurgeCopyReportsAMissingCopyAsNotFound)
{
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(19));
    const std::int64_t missing = copyIdsOf(*m_db, bystander).front() + 1000;

    expectRefusal(m_catalog->canPurgeCopy(missing), m_catalog->purgeCopy(missing),
                  "error.copy.notFound");
    EXPECT_EQ(copyIdsOf(*m_db, bystander).size(), 1u);
}

TEST_F(test_core_CanRemove, ArchiveMemberPassesWhenNothingIsOut)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(20));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(21));

    EXPECT_TRUE(m_members->canArchiveMember(memberId));
    EXPECT_TRUE(m_members->archiveMember(memberId));
    EXPECT_FALSE(memberIsLive(memberId));
    EXPECT_TRUE(memberIsLive(bystander));
}

TEST_F(test_core_CanRemove, ArchiveMemberRefusesAMemberWithABookOut)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(22));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(23));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(22));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                            "2026-09-15"),
              0);

    expectRefusal(m_members->canArchiveMember(memberId), m_members->archiveMember(memberId),
                  "error.member.archiveHasLoans");
    EXPECT_TRUE(memberIsLive(memberId));
    EXPECT_TRUE(memberIsLive(bystander));
}

TEST_F(test_core_CanRemove, ArchiveMemberReportsAMissingMemberAsNotFound)
{
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(24));

    expectRefusal(m_members->canArchiveMember(bystander + 1000),
                  m_members->archiveMember(bystander + 1000), "error.member.notFound");
    EXPECT_TRUE(memberIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeMemberRefusesALiveMember)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(25));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(26));

    expectRefusal(m_members->canPurgeMember(memberId), m_members->purgeMember(memberId),
                  "error.member.notArchived");
    EXPECT_TRUE(memberIsLive(memberId));
    EXPECT_TRUE(memberIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeMemberRefusesAnArchivedMemberWhoStillHasABookOut)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(27));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(28));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(27));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                            "2026-09-15"),
              0);
    // datetime() must equal the stored text, so a bare date is rejected.
    ASSERT_TRUE(m_db->exec("UPDATE members SET archived_at = '2026-09-01 12:00:00' WHERE id = "
                           + std::to_string(memberId)))
        << m_db->lastError();

    expectRefusal(m_members->canPurgeMember(memberId), m_members->purgeMember(memberId),
                  "error.member.deleteHasLoans");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM members WHERE id = " + std::to_string(memberId))
                  .toInt(),
              1);
    EXPECT_TRUE(memberIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeMemberRefusesAnArchivedMemberWithReturnedLoans)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(29));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(30));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(29));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                            "2026-09-15", "2026-09-10"),
              0);
    ASSERT_TRUE(m_members->archiveMember(memberId));

    expectRefusal(m_members->canPurgeMember(memberId), m_members->purgeMember(memberId),
                  "error.member.hasHistory");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM members WHERE id = " + std::to_string(memberId))
                  .toInt(),
              1);
    EXPECT_TRUE(memberIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeMemberPassesForAnArchivedMemberWhoNeverBorrowed)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(31));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(32));
    ASSERT_TRUE(m_members->archiveMember(memberId));

    EXPECT_TRUE(m_members->canPurgeMember(memberId));
    EXPECT_TRUE(m_members->purgeMember(memberId));
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM members WHERE id = " + std::to_string(memberId))
                  .toInt(),
              0);
    EXPECT_TRUE(memberIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeMemberReportsAMissingMemberAsNotFound)
{
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(33));

    expectRefusal(m_members->canPurgeMember(bystander + 1000),
                  m_members->purgeMember(bystander + 1000), "error.member.notFound");
    EXPECT_TRUE(memberIsLive(bystander));
}

TEST_F(test_core_CanRemove, ArchiveLoanPassesForAReturnedLoan)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(40));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(40));
    const std::int64_t otherBook = seedBook(*m_db, uniqueBookSeed(41));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15",
                      "2026-09-10");
    const std::int64_t bystander =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, otherBook).front(), "2026-09-01",
                      "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_GT(bystander, 0);

    EXPECT_TRUE(m_circulation->canArchiveLoan(loanId));
    EXPECT_TRUE(m_circulation->archiveLoan(loanId));
    EXPECT_FALSE(loanIsLive(loanId));
    EXPECT_TRUE(loanIsLive(bystander));
}

TEST_F(test_core_CanRemove, ArchiveLoanRefusesALoanThatIsStillOut)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(42));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(42));
    const std::int64_t otherBook = seedBook(*m_db, uniqueBookSeed(43));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15");
    const std::int64_t bystander =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, otherBook).front(), "2026-09-01",
                      "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_GT(bystander, 0);

    expectRefusal(m_circulation->canArchiveLoan(loanId), m_circulation->archiveLoan(loanId),
                  "error.loan.archiveOpen");
    EXPECT_TRUE(loanIsLive(loanId));
    EXPECT_TRUE(loanIsLive(bystander));
}

TEST_F(test_core_CanRemove, ArchiveLoanReportsAMissingLoanAsNotFound)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(44));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(44));
    const std::int64_t bystander =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15",
                      "2026-09-10");
    ASSERT_GT(bystander, 0);

    expectRefusal(m_circulation->canArchiveLoan(bystander + 1000),
                  m_circulation->archiveLoan(bystander + 1000), "error.loan.notFound");
    EXPECT_TRUE(loanIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeLoanRefusesALiveLoan)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(45));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(45));
    const std::int64_t otherBook = seedBook(*m_db, uniqueBookSeed(46));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15",
                      "2026-09-10");
    const std::int64_t bystander =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, otherBook).front(), "2026-09-01",
                      "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_GT(bystander, 0);

    expectRefusal(m_circulation->canPurgeLoan(loanId), m_circulation->purgeLoan(loanId),
                  "error.loan.notArchived");
    EXPECT_TRUE(loanIsLive(loanId));
    EXPECT_TRUE(loanIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeLoanPassesForAnArchivedLoan)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(47));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(47));
    const std::int64_t otherBook = seedBook(*m_db, uniqueBookSeed(48));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15",
                      "2026-09-10");
    const std::int64_t bystander =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, otherBook).front(), "2026-09-01",
                      "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_GT(bystander, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));

    EXPECT_TRUE(m_circulation->canPurgeLoan(loanId));
    EXPECT_TRUE(m_circulation->purgeLoan(loanId));
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM loans WHERE id = " + std::to_string(loanId)).toInt(),
              0);
    EXPECT_TRUE(loanIsLive(bystander));
}

TEST_F(test_core_CanRemove, PurgeLoanReportsAMissingLoanAsNotFound)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(49));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(49));
    const std::int64_t bystander =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15",
                      "2026-09-10");
    ASSERT_GT(bystander, 0);

    expectRefusal(m_circulation->canPurgeLoan(bystander + 1000),
                  m_circulation->purgeLoan(bystander + 1000), "error.loan.notFound");
    EXPECT_TRUE(loanIsLive(bystander));
}
