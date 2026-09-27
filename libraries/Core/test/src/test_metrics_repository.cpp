#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Core/Date.h>
#include <VLMS/Core/MetricsRepository.h>
#include <VLMS/Core/MetricsTypes.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using VLMS::Date;
using namespace VLMS::Test;

class test_core_MetricsRepository : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<MetricsRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_repository.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MetricsRepository> m_repository;
};

TEST_F(test_core_MetricsRepository, EmptyDatabaseYieldsAllZeroMetrics)
{
    const LibraryMetrics metrics = VLMS_UNWRAP(m_repository->fetchMetrics());

    EXPECT_EQ(metrics.bookTitles, 0);
    EXPECT_EQ(metrics.totalCopies, 0);
    EXPECT_EQ(metrics.availableCopies, 0);
    EXPECT_EQ(metrics.totalMembers, 0);
    EXPECT_EQ(metrics.openLoans, 0);
    EXPECT_EQ(metrics.overdueLoans, 0);
    EXPECT_TRUE(metrics.topCategories.empty());
}

TEST_F(test_core_MetricsRepository, BookTitlesCountsSeededBooks)
{
    for (int i = 0; i < 4; ++i) {
        EXPECT_GT(seedBook(*m_db, uniqueBookSeed(i)), 0);
    }

    EXPECT_EQ(VLMS_UNWRAP(m_repository->fetchMetrics()).bookTitles, 4);
}

TEST_F(test_core_MetricsRepository, TotalCopiesCountsEveryCopy)
{
    BookSeed seed = uniqueBookSeed(10);
    seed.initialCopyCount = 5;
    EXPECT_GT(seedBook(*m_db, seed), 0);

    const LibraryMetrics metrics = VLMS_UNWRAP(m_repository->fetchMetrics());
    EXPECT_EQ(metrics.bookTitles, 1);
    EXPECT_EQ(metrics.totalCopies, 5);
}

TEST_F(test_core_MetricsRepository, AvailableCopiesExcludesOpenLoans)
{
    MemberSeed member = uniqueMemberSeed(1);
    member.status = MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    BookSeed book = uniqueBookSeed(11);
    book.initialCopyCount = 4;
    const std::int64_t bookId = seedBook(*m_db, book);
    ASSERT_GT(bookId, 0);

    const auto copies = copyIdsOf(*m_db, bookId);
    const Date today = Date::todayLocal();
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copies.at(0), today.toIso(),
                            today.addDays(14).toIso()),
              0);

    const LibraryMetrics metrics = VLMS_UNWRAP(m_repository->fetchMetrics());
    EXPECT_EQ(metrics.totalCopies, 4);
    EXPECT_EQ(metrics.availableCopies, 3);
    EXPECT_EQ(metrics.totalCopies - metrics.availableCopies, 1);
}

TEST_F(test_core_MetricsRepository, MemberStatusCountsSumToTotalMembers)
{
    const std::vector<std::string> statuses = {
        MemberStatus::kActive,
        MemberStatus::kActive,
        MemberStatus::kNonActive,
    };

    for (int i = 0; i < static_cast<int>(statuses.size()); ++i) {
        MemberSeed seed = uniqueMemberSeed(20 + i);
        seed.status = statuses.at(static_cast<std::size_t>(i));
        EXPECT_GT(seedMember(*m_db, seed), 0);
    }

    const LibraryMetrics metrics = VLMS_UNWRAP(m_repository->fetchMetrics());
    EXPECT_EQ(metrics.totalMembers, static_cast<int>(statuses.size()));
    EXPECT_EQ(metrics.membersActive + metrics.membersNonActive, metrics.totalMembers);
    EXPECT_EQ(metrics.membersActive, 2);
}

TEST_F(test_core_MetricsRepository, OpenPlusReturnedEqualsTotalLoans)
{
    MemberSeed member = uniqueMemberSeed(2);
    member.status = MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    BookSeed book = uniqueBookSeed(12);
    book.initialCopyCount = 4;
    const std::int64_t bookId = seedBook(*m_db, book);
    ASSERT_GT(bookId, 0);

    const auto copies = copyIdsOf(*m_db, bookId);
    const Date today = Date::todayLocal();

    EXPECT_GT(rawInsertLoan(*m_db, memberId, copies.at(0), today.toIso(),
                            today.addDays(14).toIso()),
              0);
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copies.at(1),
                            today.addDays(-30).toIso(),
                            today.addDays(-16).toIso(),
                            today.addDays(-20).toIso()),
              0);

    const LibraryMetrics metrics = VLMS_UNWRAP(m_repository->fetchMetrics());
    EXPECT_EQ(metrics.openLoans + metrics.returnedLoans, m_db->count("loans"));
    EXPECT_EQ(metrics.openLoans, 1);
    EXPECT_EQ(metrics.returnedLoans, 1);
}

TEST_F(test_core_MetricsRepository, OpenLoansAreTheOnesStillWithinTheirDueDate)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(31));
    BookSeed book = uniqueBookSeed(31);
    book.initialCopyCount = 3;
    const auto copies = copyIdsOf(*m_db, seedBook(*m_db, book));
    const Date today = Date::todayLocal();

    ASSERT_GT(rawInsertLoan(*m_db, memberId, copies.at(0), today.addDays(-3).toIso(),
                            today.addDays(11).toIso()),
              0);
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copies.at(1), today.addDays(-3).toIso(),
                            today.toIso()),
              0);
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copies.at(2), today.addDays(-30).toIso(),
                            today.addDays(-16).toIso()),
              0);

    const LibraryMetrics metrics = VLMS_UNWRAP(m_repository->fetchMetrics());
    EXPECT_EQ(metrics.openLoans, 2);     // due later today or after
    EXPECT_EQ(metrics.overdueLoans, 1);  // past its due date: overdue, not open
    EXPECT_EQ(metrics.openLoans + metrics.overdueLoans + metrics.returnedLoans,
              m_db->count("loans"));
}

TEST_F(test_core_MetricsRepository, OverdueCountMatchesOverdueLoans)
{
    MemberSeed member = uniqueMemberSeed(3);
    member.status = MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    BookSeed book = uniqueBookSeed(13);
    book.initialCopyCount = 3;
    const std::int64_t bookId = seedBook(*m_db, book);
    ASSERT_GT(bookId, 0);

    const auto copies = copyIdsOf(*m_db, bookId);
    const Date today = Date::todayLocal();

    // Comfortably overdue and comfortably current, so the UTC/local boundary
    // cannot make this test flaky. The boundary itself is pinned in Phase B.
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copies.at(0),
                            today.addDays(-40).toIso(),
                            today.addDays(-26).toIso()),
              0);
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copies.at(1), today.toIso(),
                            today.addDays(20).toIso()),
              0);

    EXPECT_EQ(VLMS_UNWRAP(m_repository->fetchMetrics()).overdueLoans, 1);
}

TEST_F(test_core_MetricsRepository, TopCategoriesIsOrderedByBookCountDescending)
{
    const std::int64_t big = seedCategory(*m_db, "BIG", "Big");
    const std::int64_t small = seedCategory(*m_db, "SMALL", "Small");
    ASSERT_GT(big, 0);
    ASSERT_GT(small, 0);

    for (int i = 0; i < 3; ++i) {
        BookSeed seed = uniqueBookSeed(30 + i);
        seed.categoryId = big;
        EXPECT_GT(seedBook(*m_db, seed), 0);
    }
    BookSeed lone = uniqueBookSeed(40);
    lone.categoryId = small;
    EXPECT_GT(seedBook(*m_db, lone), 0);

    const auto categories = VLMS_UNWRAP(m_repository->fetchMetrics()).topCategories;
    EXPECT_EQ(categories.size(), 2u);
    EXPECT_EQ(categories.front().label, "Big");
    EXPECT_EQ(categories.front().bookCount, 3);
    EXPECT_GE(categories.front().bookCount, categories.back().bookCount);
}

TEST_F(test_core_MetricsRepository, TopCategoriesFallsBackToCodeWhenLabelBlank)
{
    const std::int64_t id = seedCategory(*m_db, "NOLABEL", "");
    ASSERT_GT(id, 0);

    BookSeed seed = uniqueBookSeed(50);
    seed.categoryId = id;
    EXPECT_GT(seedBook(*m_db, seed), 0);

    const auto categories = VLMS_UNWRAP(m_repository->fetchMetrics()).topCategories;
    EXPECT_EQ(categories.size(), 1u);
    EXPECT_EQ(categories.front().label, "NOLABEL");
}
