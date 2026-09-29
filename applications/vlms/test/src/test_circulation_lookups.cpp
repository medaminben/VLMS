#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/presentation/QtCirculationLookups.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/LoanTypes.h>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

class CirculationLookupsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_lookups = std::make_unique<QtCirculationLookups>(*m_circulation);
    }

    void TearDown() override
    {
        m_lookups.reset();
        m_circulation.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<QtCirculationLookups> m_lookups;
};

TEST_F(CirculationLookupsTest, BorrowableMembersForwardFromRepository)
{
    MemberSeed seed = uniqueMemberSeed(1);
    seed.firstName = "Fixture";
    seed.lastName = "Borrower";
    const std::int64_t memberId = seedMember(*m_db, seed);
    ASSERT_GT(memberId, 0);

    const auto members = m_lookups->borrowableMembers(QStringLiteral("Fixture"));
    ASSERT_FALSE(members.empty());
    bool found = false;
    for (const Repositories::LoanMemberOption& option : members) {
        if (option.id == memberId) {
            found = true;
            EXPECT_EQ(option.firstName, "Fixture");
            EXPECT_EQ(option.lastName, "Borrower");
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(CirculationLookupsTest, AvailableCopiesForwardFromRepository)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(1));
    ASSERT_GT(bookId, 0);
    const auto copyIds = copyIdsOf(*m_db, bookId);
    ASSERT_FALSE(copyIds.empty());

    const auto copies = m_lookups->availableCopies({}, bookId);
    ASSERT_FALSE(copies.empty());
    bool found = false;
    for (const Repositories::LoanCopyOption& option : copies) {
        if (option.id == copyIds.front()) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}
