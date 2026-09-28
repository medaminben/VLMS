#include "TestDatabase.h"
#include "TestSeed.h"

#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MetricsRepository.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using VLMS::Date;
using VLMS::DateTime;
using VLMS::ScopedClock;
using namespace VLMS::Test;

class test_core_ArchiveMembers : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_metrics = std::make_unique<MetricsRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_metrics.reset();
        m_members.reset();
        m_db.reset();
    }

    void rawArchive(const std::string& sqlWhere)
    {
        ASSERT_TRUE(m_db->exec(sqlWhere));
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MemberRepository> m_members;
    std::unique_ptr<MetricsRepository> m_metrics;
};

TEST_F(test_core_ArchiveMembers, ArchivedMembersAppearOnlyUnderTheArchivedScope)
{
    const std::int64_t kept = seedMember(*m_db, uniqueMemberSeed(1));
    const std::int64_t archived = seedMember(*m_db, uniqueMemberSeed(2));
    ASSERT_TRUE(m_members->archiveMember(archived));

    EXPECT_EQ(VLMS_UNWRAP(m_members->countMembers({})), 1);
    EXPECT_EQ(VLMS_UNWRAP(m_members->listMembers({})).front().id, kept);

    MemberQuery archive;
    archive.archive = ArchiveScope::Archived;
    const auto rows = VLMS_UNWRAP(m_members->listMembers(archive));
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows.front().id, archived);
    EXPECT_FALSE(rows.front().archivedAt.empty());
    EXPECT_EQ(VLMS_UNWRAP(m_members->countMembers(archive)), 1);
    EXPECT_EQ(VLMS_UNWRAP(m_members->rankOfMember(archived, archive)), 0);
}

TEST_F(test_core_ArchiveMembers, ArchiveMemberStampsFromTheClock)
{
    const std::int64_t id = seedMember(*m_db, uniqueMemberSeed(3));
    const ScopedClock pinned(DateTime(Date(2026, 9, 19), 10, 0, 0));
    ASSERT_TRUE(m_members->archiveMember(id));

    const auto member = m_members->getMember(id);
    ASSERT_TRUE(member.has_value());
    EXPECT_EQ(member->archivedAt, "2026-09-19 10:00:00");
    EXPECT_EQ(member->updatedAt, "2026-09-19 10:00:00");
}

TEST_F(test_core_ArchiveMembers, RestoreMemberBringsThemBack)
{
    const std::int64_t id = seedMember(*m_db, uniqueMemberSeed(4));
    ASSERT_TRUE(m_members->archiveMember(id));
    ASSERT_TRUE(m_members->restoreMember(id));

    EXPECT_EQ(VLMS_UNWRAP(m_members->countMembers({})), 1);
    EXPECT_TRUE(m_members->getMember(id)->archivedAt.empty());
}

TEST_F(test_core_ArchiveMembers, RestoreOfALiveMemberIsRefused)
{
    const auto refused = m_members->restoreMember(seedMember(*m_db, uniqueMemberSeed(5)));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.member.notArchived");
}

TEST_F(test_core_ArchiveMembers, TheArchiveListsNewestFirstByDefault)
{
    const std::int64_t older = seedMember(*m_db, uniqueMemberSeed(6));
    const std::int64_t newer = seedMember(*m_db, uniqueMemberSeed(7));
    {
        const ScopedClock first(DateTime(Date(2026, 9, 1), 9, 0, 0));
        ASSERT_TRUE(m_members->archiveMember(older));
    }
    {
        const ScopedClock second(DateTime(Date(2026, 9, 19), 10, 0, 0));
        ASSERT_TRUE(m_members->archiveMember(newer));
    }

    MemberQuery archive;
    archive.archive = ArchiveScope::Archived;
    const auto rows = VLMS_UNWRAP(m_members->listMembers(archive));
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows.at(0).id, newer);
    EXPECT_EQ(rows.at(1).id, older);
}

TEST_F(test_core_ArchiveMembers, MetricsHoldingsCountLiveBooksAndCopiesOnly)
{
    const std::int64_t category = seedCategory(*m_db, "HIS", "History");
    BookSeed kept = uniqueBookSeed(1);
    kept.categoryId = category;
    kept.initialCopyCount = 2;
    BookSeed gone = uniqueBookSeed(2);
    gone.categoryId = category;
    gone.initialCopyCount = 3;
    seedBook(*m_db, kept);
    const std::string goneId = std::to_string(seedBook(*m_db, gone));
    rawArchive("UPDATE books SET archived_at = '2026-09-19 10:00:00' WHERE id = " + goneId);
    rawArchive("UPDATE book_copies SET archived_at = '2026-09-19 10:00:00' WHERE book_id = " + goneId);

    const auto metrics = VLMS_UNWRAP(m_metrics->fetchMetrics());
    EXPECT_EQ(metrics.bookTitles, 1);
    EXPECT_EQ(metrics.totalCopies, 2);
    EXPECT_EQ(metrics.availableCopies, 2);
    ASSERT_EQ(metrics.topCategories.size(), 1u);
    EXPECT_EQ(metrics.topCategories.front().bookCount, 1);
}

TEST_F(test_core_ArchiveMembers, MetricsMemberAndLoanFiguresCountArchivedRowsAsHistory)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(8));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(3));
    const std::int64_t loanId = rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(),
                                              "2026-09-01", "2026-09-15", "2026-09-10");
    const auto before = VLMS_UNWRAP(m_metrics->fetchMetrics());

    rawArchive("UPDATE loans SET archived_at = '2026-09-19 10:00:00' WHERE id = "
               + std::to_string(loanId));
    ASSERT_TRUE(m_members->archiveMember(memberId));

    const auto after = VLMS_UNWRAP(m_metrics->fetchMetrics());
    EXPECT_EQ(after.returnedLoans, before.returnedLoans);
    EXPECT_EQ(after.totalMembers, before.totalMembers);
}
