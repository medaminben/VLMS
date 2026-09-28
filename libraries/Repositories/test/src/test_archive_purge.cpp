#include "TestDatabase.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace VLMS;
using namespace Test;

/// One fixture for the whole funnel: a purge is always a question about the
/// rows beneath a record, so every test needs all three repositories.
class test_core_ArchivePurge : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_circulation.reset();
        m_catalog.reset();
        m_members.reset();
        m_db.reset();
    }

    /// A returned loan, already archived, so it is ready to be purged.
    std::int64_t seedArchivedLoan(int index)
    {
        const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(index));
        const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(index));
        const std::int64_t loanId =
            rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                          "2026-09-15", "2026-09-10");
        EXPECT_GT(loanId, 0);
        EXPECT_TRUE(m_circulation->archiveLoan(loanId));
        return loanId;
    }

    [[nodiscard]] bool loanExists(std::int64_t loanId) const
    {
        return m_db->scalar("SELECT COUNT(*) FROM loans WHERE id = " + std::to_string(loanId))
                   .toInt()
            > 0;
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
};

TEST_F(test_core_ArchivePurge, PurgeLoanRemovesAnArchivedLoanAndLeavesTheOthers)
{
    const std::int64_t purged = seedArchivedLoan(1);
    const std::int64_t bystander = seedArchivedLoan(2);

    ASSERT_TRUE(m_circulation->purgeLoan(purged));

    EXPECT_FALSE(loanExists(purged));
    EXPECT_TRUE(loanExists(bystander));
}

TEST_F(test_core_ArchivePurge, PurgeLoanIsRefusedWhileTheLoanIsStillLive)
{
    const std::int64_t bystander = seedArchivedLoan(5);
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(3));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(3));
    const std::int64_t loanId = rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(),
                                              "2026-09-01", "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);

    const auto refused = m_circulation->purgeLoan(loanId);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.loan.notArchived");
    EXPECT_TRUE(loanExists(loanId));
    EXPECT_TRUE(loanExists(bystander));
}

TEST_F(test_core_ArchivePurge, PurgeLoanReportsAMissingLoanRatherThanSucceeding)
{
    const std::int64_t bystander = seedArchivedLoan(4);

    const auto missing = m_circulation->purgeLoan(bystander + 1000);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().key, "error.loan.notFound");
    EXPECT_TRUE(loanExists(bystander));
}

TEST_F(test_core_ArchivePurge, PurgeCopyRemovesAnArchivedCopyAndLeavesItsBookAndTheOtherCopy)
{
    BookSeed seed = uniqueBookSeed(10);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copies.size(), 2u);
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    ASSERT_TRUE(m_catalog->purgeCopy(copies.at(0)));

    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = "
                           + std::to_string(copies.at(0)))
                  .toInt(),
              0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = "
                           + std::to_string(copies.at(1)))
                  .toInt(),
              1);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bookId))
                  .toInt(),
              1);
}

TEST_F(test_core_ArchivePurge, PurgeCopyIsRefusedWhileALoanNamesIt)
{
    BookSeed seed = uniqueBookSeed(11);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(11));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copies.at(0), "2026-09-01", "2026-09-15", "2026-09-10"),
              0);
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    const auto refused = m_catalog->purgeCopy(copies.at(0));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.copy.hasHistory");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = "
                           + std::to_string(copies.at(0)))
                  .toInt(),
              1);

    // The copy nobody borrowed still goes: the gate is per copy, not per book.
    EXPECT_TRUE(m_catalog->purgeCopy(copies.at(1)));
}

TEST_F(test_core_ArchivePurge, PurgeCopyIsRefusedWhileTheCopyIsStillLive)
{
    BookSeed seed = uniqueBookSeed(12);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);

    const auto refused = m_catalog->purgeCopy(copies.at(0));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.copy.notArchived");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE book_id = "
                           + std::to_string(bookId))
                  .toInt(),
              2);
}

TEST_F(test_core_ArchivePurge, PurgeCopyCountsArchivedLoansToo)
{
    BookSeed seed = uniqueBookSeed(13);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(13));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copies.at(0), "2026-09-01", "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    // Archiving the loan does not unblock the copy -- the funnel has to be
    // walked, not stepped around.
    const auto refused = m_catalog->purgeCopy(copies.at(0));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.copy.hasHistory");

    ASSERT_TRUE(m_circulation->purgeLoan(loanId));
    EXPECT_TRUE(m_catalog->purgeCopy(copies.at(0)));
}

TEST_F(test_core_ArchivePurge, PurgeBookRemovesAnArchivedTitleThatHasNoCopiesLeft)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(20));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(21));
    const std::int64_t copyId = copyIdsOf(*m_db, bookId).front();
    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    ASSERT_TRUE(m_catalog->purgeCopy(copyId));

    ASSERT_TRUE(m_catalog->purgeBook(bookId));

    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bookId))
                  .toInt(),
              0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bystander))
                  .toInt(),
              1);
}

TEST_F(test_core_ArchivePurge, PurgeBookIsRefusedWhileAnyCopyRemains)
{
    BookSeed seed = uniqueBookSeed(22);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(29));
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    ASSERT_TRUE(m_catalog->purgeCopy(copies.at(0)));

    const auto refused = m_catalog->purgeBook(bookId);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.book.hasCopies");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bookId))
                  .toInt(),
              1);

    ASSERT_TRUE(m_catalog->purgeCopy(copies.at(1)));
    EXPECT_TRUE(m_catalog->purgeBook(bookId));

    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bookId))
                  .toInt(),
              0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bystander))
                  .toInt(),
              1);
}

TEST_F(test_core_ArchivePurge, PurgeBookIsRefusedWhileTheTitleIsStillLive)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(23));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(28));

    const auto refused = m_catalog->purgeBook(bookId);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.book.notArchived");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bookId))
                  .toInt(),
              1);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bystander))
                  .toInt(),
              1);
}

TEST_F(test_core_ArchivePurge, PurgeBookTakesTheCoverFolderWithIt)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(24));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(25));
    const std::filesystem::path folder =
        std::filesystem::path(m_db->resourcesDirectory()) / "books" / std::to_string(bookId);
    const std::filesystem::path keptFolder =
        std::filesystem::path(m_db->resourcesDirectory()) / "books" / std::to_string(bystander);
    std::filesystem::create_directories(folder);
    std::filesystem::create_directories(keptFolder);
    std::ofstream(folder / "cover.jpg") << "jpeg";
    std::ofstream(keptFolder / "cover.jpg") << "jpeg";

    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    ASSERT_TRUE(m_catalog->purgeCopy(copyIdsOf(*m_db, bookId).front()));
    ASSERT_TRUE(m_catalog->purgeBook(bookId));

    EXPECT_FALSE(std::filesystem::exists(folder));
    EXPECT_TRUE(std::filesystem::exists(keptFolder / "cover.jpg"));
}

TEST_F(test_core_ArchivePurge, BookHasOpenLoansAnswersForTheCatalogueBeforeItConfirms)
{
    BookSeed seed = uniqueBookSeed(26);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const std::int64_t quiet = seedBook(*m_db, uniqueBookSeed(27));
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(26));

    EXPECT_FALSE(VLMS_UNWRAP(m_catalog->bookHasOpenLoans(bookId)));

    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                            "2026-09-15"),
              0);
    EXPECT_TRUE(VLMS_UNWRAP(m_catalog->bookHasOpenLoans(bookId)));
    EXPECT_FALSE(VLMS_UNWRAP(m_catalog->bookHasOpenLoans(quiet)));
}

TEST_F(test_core_ArchivePurge, PurgeMemberIsRefusedWhileTheMemberIsStillLive)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(30));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(31));

    const auto refused = m_members->purgeMember(memberId);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.member.notArchived");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM members").toInt(), 2);
    EXPECT_GT(bystander, 0);
}

TEST_F(test_core_ArchivePurge, PurgeMemberRemovesAnArchivedMemberWhoNeverBorrowed)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(32));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(33));
    ASSERT_TRUE(m_members->archiveMember(memberId));

    ASSERT_TRUE(m_members->purgeMember(memberId));

    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM members WHERE id = " + std::to_string(memberId))
                  .toInt(),
              0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM members WHERE id = " + std::to_string(bystander))
                  .toInt(),
              1);
}

TEST_F(test_core_ArchivePurge, PurgeMemberTakesTheirPhotoFolderWithIt)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(36));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(37));
    const std::filesystem::path folder =
        std::filesystem::path(m_db->resourcesDirectory()) / "members" / std::to_string(memberId);
    const std::filesystem::path keptFolder =
        std::filesystem::path(m_db->resourcesDirectory()) / "members" / std::to_string(bystander);
    std::filesystem::create_directories(folder);
    std::filesystem::create_directories(keptFolder);
    std::ofstream(folder / "photo.jpg") << "jpeg";
    std::ofstream(keptFolder / "photo.jpg") << "jpeg";

    ASSERT_TRUE(m_members->archiveMember(memberId));
    ASSERT_TRUE(m_members->purgeMember(memberId));

    EXPECT_FALSE(std::filesystem::exists(folder));
    EXPECT_TRUE(std::filesystem::exists(keptFolder / "photo.jpg"));
}

TEST_F(test_core_ArchivePurge, TheFunnelIsWalkableFromALoanUpToTheMemberWhoBorrowedIt)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(34));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(35));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(34));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15",
                      "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_members->archiveMember(memberId));

    // Blocked at the top of the funnel while the loan is still there ...
    const auto blocked = m_members->purgeMember(memberId);
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().key, "error.member.hasHistory");

    // ... and archiving the loan is not enough; it has to go.
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));
    const auto stillBlocked = m_members->purgeMember(memberId);
    ASSERT_FALSE(stillBlocked);
    EXPECT_EQ(stillBlocked.error().key, "error.member.hasHistory");

    ASSERT_TRUE(m_circulation->purgeLoan(loanId));
    EXPECT_TRUE(m_members->purgeMember(memberId));
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM members WHERE id = " + std::to_string(bystander))
                  .toInt(),
              1);
}

TEST_F(test_core_ArchivePurge, MemberLoanCountCountsArchivedLoansSoTheColumnMatchesTheGate)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(40));
    const std::int64_t quiet = seedMember(*m_db, uniqueMemberSeed(41));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(40));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15",
                      "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));

    const auto borrower = m_members->getMember(memberId);
    ASSERT_TRUE(borrower);
    EXPECT_EQ(borrower->loanCount, 1);
    EXPECT_EQ(borrower->activeLoanCount, 0);

    const auto never = m_members->getMember(quiet);
    ASSERT_TRUE(never);
    EXPECT_EQ(never->loanCount, 0);

    // The column and the gate must agree, or a 0 sits beside a button that refuses.
    ASSERT_TRUE(m_circulation->purgeLoan(loanId));
    EXPECT_EQ(m_members->getMember(memberId)->loanCount, 0);
}

TEST_F(test_core_ArchivePurge, CopyLoanCountCountsArchivedLoansSoTheColumnMatchesTheGate)
{
    BookSeed seed = uniqueBookSeed(42);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(42));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copies.at(0), "2026-09-01", "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    Repositories::CopyQuery query;
    query.archive = Repositories::ArchiveScope::Archived;
    const auto rows = VLMS_UNWRAP(m_catalog->listCopyRows(query));
    ASSERT_EQ(rows.size(), 2u);

    int borrowed = 0;
    int untouched = 0;
    for (const Repositories::BookCopyRecord& copy : rows) {
        if (copy.id == copies.at(0)) {
            borrowed = copy.loanCount;
        } else if (copy.id == copies.at(1)) {
            untouched = copy.loanCount;
        }
    }
    EXPECT_EQ(borrowed, 1);
    EXPECT_EQ(untouched, 0);
}
