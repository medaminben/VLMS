#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MetricsRepository.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using VLMS::Date;
using VLMS::ScopedClock;
using namespace VLMS;
using namespace Test;

/**
 * The one-year rule through the repositories: what a member reads as,
 * what a librarian's save does to the date, and who may borrow.
 */
class test_core_MemberStatusExpiry : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_metrics = std::make_unique<Repositories::MetricsRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_metrics.reset();
        m_circulation.reset();
        m_members.reset();
        m_db.reset();
    }

    /// A member registered on `day`, saved Active.
    std::int64_t registerOn(const Date& day, const int index)
    {
        const ScopedClock pinned(day);
        return seedMember(*m_db, uniqueMemberSeed(index));
    }

    std::string lastHistoryNote(const std::int64_t memberId) const
    {
        return m_db->scalar("SELECT note FROM member_status_history WHERE member_id = :id "
                            "ORDER BY id DESC LIMIT 1",
                            {{"id", memberId}})
            .toString();
    }

    int historyCount(const std::int64_t memberId) const
    {
        return m_db->scalar("SELECT COUNT(*) FROM member_status_history WHERE member_id = :id",
                            {{"id", memberId}})
            .toInt();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<Repositories::MetricsRepository> m_metrics;
};

TEST_F(test_core_MemberStatusExpiry, RegisteringStartsAYearLessADay)
{
    const std::int64_t id = registerOn(Date(2026, 9, 23), 1);
    ASSERT_GT(id, 0);

    const ScopedClock pinned(Date(2026, 9, 23));
    const Repositories::MemberRecord member = VLMS_UNWRAP(m_members->getMember(id));
    EXPECT_EQ(member.activeUntil, "2027-09-22");
    EXPECT_EQ(member.status, Repositories::MemberStatus::kActive);
    EXPECT_EQ(lastHistoryNote(id), "Registered, active until 2027-09-22");
}

TEST_F(test_core_MemberStatusExpiry, ActiveOnTheLastDayNotActiveTheDayAfter)
{
    const std::int64_t id = registerOn(Date(2025, 9, 24), 1);
    ASSERT_GT(id, 0);

    Repositories::MemberQuery active;
    active.statuses = {Repositories::MemberStatus::kActive};
    Repositories::MemberQuery notActive;
    notActive.statuses = {Repositories::MemberStatus::kNonActive};

    {
        const ScopedClock pinned(Date(2026, 9, 23));
        EXPECT_EQ(VLMS_UNWRAP(m_members->getMember(id)).status, Repositories::MemberStatus::kActive);
        EXPECT_EQ(VLMS_UNWRAP(m_members->countMembers(active)), 1);
        EXPECT_EQ(VLMS_UNWRAP(m_members->countMembers(notActive)), 0);
    }
    {
        const ScopedClock pinned(Date(2026, 9, 24));
        EXPECT_EQ(VLMS_UNWRAP(m_members->getMember(id)).status, Repositories::MemberStatus::kNonActive);
        EXPECT_EQ(VLMS_UNWRAP(m_members->countMembers(active)), 0);
        EXPECT_EQ(VLMS_UNWRAP(m_members->countMembers(notActive)), 1);
        EXPECT_EQ(VLMS_UNWRAP(m_members->listMembers(notActive)).size(), 1u);
    }
}

TEST_F(test_core_MemberStatusExpiry, RenewingAnExpiredMemberGivesAnotherYear)
{
    const std::int64_t id = registerOn(Date(2025, 1, 10), 1);
    ASSERT_GT(id, 0);

    const ScopedClock pinned(Date(2026, 9, 23));
    ASSERT_EQ(VLMS_UNWRAP(m_members->getMember(id)).status, Repositories::MemberStatus::kNonActive);

    Repositories::MemberInput input = uniqueMemberSeed(1).toInput();
    input.status = Repositories::MemberStatus::kActive;
    ASSERT_TRUE(m_members->updateMember(id, input));

    const Repositories::MemberRecord member = VLMS_UNWRAP(m_members->getMember(id));
    EXPECT_EQ(member.activeUntil, "2027-09-22");
    EXPECT_EQ(member.status, Repositories::MemberStatus::kActive);
    EXPECT_EQ(lastHistoryNote(id), "Renewed until 2027-09-22");
    EXPECT_EQ(m_db->scalar("SELECT old_status FROM member_status_history WHERE member_id = :id "
                           "ORDER BY id DESC LIMIT 1",
                           {{"id", id}})
                  .toString(),
              Repositories::MemberStatus::kNonActive);
}

TEST_F(test_core_MemberStatusExpiry, SettingNotActiveEndsTheMembershipYesterday)
{
    const std::int64_t id = registerOn(Date(2026, 9, 1), 1);
    ASSERT_GT(id, 0);

    const ScopedClock pinned(Date(2026, 9, 23));
    Repositories::MemberInput input = uniqueMemberSeed(1).toInput();
    input.status = Repositories::MemberStatus::kNonActive;
    ASSERT_TRUE(m_members->updateMember(id, input));

    const Repositories::MemberRecord member = VLMS_UNWRAP(m_members->getMember(id));
    EXPECT_EQ(member.activeUntil, "2026-09-22");
    EXPECT_EQ(member.status, Repositories::MemberStatus::kNonActive);
    EXPECT_EQ(lastHistoryNote(id), "Ended early");
}

TEST_F(test_core_MemberStatusExpiry, SavingWithTheSameStatusKeepsTheDate)
{
    const std::int64_t id = registerOn(Date(2026, 9, 1), 1);
    ASSERT_GT(id, 0);
    const int historyBefore = historyCount(id);

    const ScopedClock pinned(Date(2026, 9, 23));
    Repositories::MemberInput input = uniqueMemberSeed(1).toInput();
    input.phone = "71 000 000";
    input.status = Repositories::MemberStatus::kActive;
    ASSERT_TRUE(m_members->updateMember(id, input));

    EXPECT_EQ(VLMS_UNWRAP(m_members->getMember(id)).activeUntil, "2027-08-31");
    EXPECT_EQ(historyCount(id), historyBefore);
}

TEST_F(test_core_MemberStatusExpiry, BorrowingIsRefusedTheDayAfterExpiry)
{
    const std::int64_t memberId = registerOn(Date(2025, 9, 24), 1);
    ASSERT_GT(memberId, 0);
    BookSeed book = uniqueBookSeed(1);
    book.initialCopyCount = 2;
    const auto copies = copyIdsOf(*m_db, seedBook(*m_db, book));
    ASSERT_EQ(copies.size(), 2u);

    Repositories::LoanInput loan;
    loan.memberId = memberId;
    {
        const ScopedClock pinned(Date(2026, 9, 23));
        loan.bookCopyId = copies.at(0);
        EXPECT_TRUE(m_circulation->createLoan(loan)) << "the last active day still lends";
    }
    {
        const ScopedClock pinned(Date(2026, 9, 24));
        loan.bookCopyId = copies.at(1);
        const auto refused = m_circulation->createLoan(loan);
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().key, "error.loan.memberInactive");
    }
}

TEST_F(test_core_MemberStatusExpiry, TheBorrowableListDropsTheExpiredMember)
{
    const std::int64_t expired = registerOn(Date(2025, 1, 10), 1);
    const std::int64_t current = registerOn(Date(2026, 9, 1), 2);
    ASSERT_GT(expired, 0);
    ASSERT_GT(current, 0);

    const ScopedClock pinned(Date(2026, 9, 23));
    const auto borrowable = VLMS_UNWRAP(m_circulation->listBorrowableMembers());
    ASSERT_EQ(borrowable.size(), 1u);
    EXPECT_EQ(borrowable.front().id, current);
    EXPECT_EQ(borrowable.front().status, Repositories::MemberStatus::kActive);
}

TEST_F(test_core_MemberStatusExpiry, LoanRowsCarryTheMembersStatusOnTheDay)
{
    const std::int64_t memberId = registerOn(Date(2025, 9, 24), 1);
    const auto copies = copyIdsOf(*m_db, seedBook(*m_db, uniqueBookSeed(1)));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copies.at(0), "2026-09-20", "2026-10-04"), 0);

    Repositories::LoanQuery all;
    all.filters = {Repositories::LoanFilter::kAll};
    {
        const ScopedClock pinned(Date(2026, 9, 23));
        EXPECT_EQ(VLMS_UNWRAP(m_circulation->listLoans(all)).front().memberStatus,
                  Repositories::MemberStatus::kActive);
    }
    {
        const ScopedClock pinned(Date(2026, 9, 24));
        EXPECT_EQ(VLMS_UNWRAP(m_circulation->listLoans(all)).front().memberStatus,
                  Repositories::MemberStatus::kNonActive);
    }
}

TEST_F(test_core_MemberStatusExpiry, MetricsCountFromTheDate)
{
    ASSERT_GT(registerOn(Date(2025, 1, 10), 1), 0);
    ASSERT_GT(registerOn(Date(2026, 9, 1), 2), 0);
    ASSERT_GT(registerOn(Date(2026, 9, 2), 3), 0);

    const ScopedClock pinned(Date(2026, 9, 23));
    const Repositories::LibraryMetrics metrics = VLMS_UNWRAP(m_metrics->fetchMetrics());
    EXPECT_EQ(metrics.membersActive, 2);
    EXPECT_EQ(metrics.membersNonActive, 1);
}

TEST_F(test_core_MemberStatusExpiry, SortingByStatusPutsTheEarliestLastDayFirst)
{
    const std::int64_t later = registerOn(Date(2026, 9, 1), 1);
    const std::int64_t earlier = registerOn(Date(2025, 1, 10), 2);

    const ScopedClock pinned(Date(2026, 9, 23));
    Repositories::MemberQuery query;
    query.sortColumn = Repositories::MemberSort::kStatus;
    query.sortAscending = true;
    const auto members = VLMS_UNWRAP(m_members->listMembers(query));
    ASSERT_EQ(members.size(), 2u);
    EXPECT_EQ(members.at(0).id, earlier);
    EXPECT_EQ(members.at(1).id, later);
}
