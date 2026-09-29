#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/presentation/QtMemberLookups.h"

#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

class MemberLookupsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(),
                                                                     m_db->resourcesDirectory());
        m_lookups = std::make_unique<QtMemberLookups>(*m_members);
    }

    void TearDown() override
    {
        m_lookups.reset();
        m_members.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<QtMemberLookups> m_lookups;
};

TEST_F(MemberLookupsTest, SuggestMembershipNumberIsNonEmpty)
{
    const QString number = m_lookups->suggestMembershipNumber();
    EXPECT_FALSE(number.isEmpty());
}

TEST_F(MemberLookupsTest, CitiesForwardFromRepository)
{
    MemberSeed seed = uniqueMemberSeed(1);
    seed.city = "Fixture City";
    ASSERT_GT(seedMember(*m_db, seed), 0);

    const QStringList cities = m_lookups->cities(Repositories::ArchiveScope::Live);
    EXPECT_TRUE(cities.contains(QStringLiteral("Fixture City")));
}

TEST_F(MemberLookupsTest, ResolveImagePathJoinsResourcesDirectory)
{
    const QString resolved = m_lookups->resolveImagePath(QStringLiteral("members/1/photo.jpg"));
    EXPECT_TRUE(resolved.contains(QStringLiteral("members/1/photo.jpg")));
    EXPECT_TRUE(m_lookups->resolveImagePath(QString()).isEmpty());
}
