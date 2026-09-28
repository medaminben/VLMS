#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace VLMS;
using namespace Test;

namespace {

const std::vector<std::string> kCoreTables = {
    "members", "member_status_history", "books", "loans", "employees",
};

std::string trimmed(std::string_view text)
{
    const auto begin = text.find_first_not_of(" \t\n\r");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\n\r");
    return std::string(text.substr(begin, end - begin + 1));
}

}  // namespace

class test_core_MemberInjection : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        for (int i = 0; i < 3; ++i) {
            ASSERT_GT(seedMember(*m_db, uniqueMemberSeed(i)), 0);
        }
        m_memberCountAtStart = m_db->count("members");
    }

    void TearDown() override
    {
        if (m_db != nullptr && m_db->isValid()) {
            std::string whatChanged;
            EXPECT_TRUE(schemaIsIntact(*m_db, kCoreTables, &whatChanged)) << whatChanged;
            EXPECT_EQ(m_db->count("members"), m_memberCountAtStart);
        }
        m_repository.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MemberRepository> m_repository;
    int m_memberCountAtStart = 0;
};

TEST_F(test_core_MemberInjection, SearchWithHostilePayloadIsTreatedAsLiteral)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        MemberQuery query;
        query.search = entry.value;

        EXPECT_TRUE(VLMS_UNWRAP(m_repository->listMembers(query)).empty())
            << "a bound payload cannot match any seeded member";
    }
}

TEST_F(test_core_MemberInjection, CountMembersAgreesWithListMembersForHostileSearch)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        MemberQuery query;
        query.search = entry.value;

        EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(query)),
                  static_cast<int>(VLMS_UNWRAP(m_repository->listMembers(query)).size()));
    }
}

TEST_F(test_core_MemberInjection, StatusFilterWithHostilePayloadMatchesNothing)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        // listMembers does NOT validate statuses against statusCodes(); it binds
        // them. So a hostile status must simply match zero rows.
        MemberQuery query;
        query.statuses = {entry.value};

        EXPECT_EQ(static_cast<int>(VLMS_UNWRAP(m_repository->listMembers(query)).size()), 0);
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(query)), 0);
    }
}

TEST_F(test_core_MemberInjection, StatusFilterWithMixOfValidAndHostileCodes)
{
    MemberQuery query;
    query.statuses = {
        "'; DROP TABLE members;--",
        MemberStatus::kActive,
        "' OR '1'='1",
    };

    // The seeds are all 'active', so exactly the real code must select them.
    const auto results = VLMS_UNWRAP(m_repository->listMembers(query));
    ASSERT_EQ(static_cast<int>(results.size()), m_memberCountAtStart);
    for (const MemberRecord& member : results) {
        EXPECT_EQ(member.status, MemberStatus::kActive);
    }
}

TEST_F(test_core_MemberInjection, NewFiltersWithHostilePayloadMatchNothing)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        MemberQuery bySex;
        bySex.sexes = {entry.value};
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(bySex)), 0);

        MemberQuery byYear;
        byYear.inscriptionYears = {entry.value};
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(byYear)), 0);

        MemberQuery byAge;
        byAge.ageGroups = {entry.value};
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(byAge)), 0);

        MemberQuery byCity;
        byCity.cities = {entry.value};
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(byCity)), 0);
    }
}

TEST_F(test_core_MemberInjection, CreateMemberRejectsHostileStatus)
{
    int index = 500;
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        MemberSeed seed = uniqueMemberSeed(index++);
        seed.status = entry.value;

        EXPECT_FALSE(m_repository->createMember(seed.toInput()))
            << "isValidStatus must reject anything outside the whitelist";
    }
}

TEST_F(test_core_MemberInjection, CreateMemberRejectsHostileSex)
{
    int index = 501;
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        MemberSeed seed = uniqueMemberSeed(index++);
        seed.sex = entry.value;

        EXPECT_FALSE(m_repository->createMember(seed.toInput()))
            << "isValidSex must reject anything outside the whitelist";
    }
}

TEST_F(test_core_MemberInjection, MembershipNumberWithHostilePayloadRoundTrips)
{
    int index = 502;
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        MemberSeed seed = uniqueMemberSeed(index++);
        seed.membershipNumber = entry.value;

        std::int64_t id = 0;
        const auto created = m_repository->createMember(seed.toInput());
        ASSERT_TRUE(created) << created.error().key;
        id = created.value();
        m_memberCountAtStart = m_db->count("members");

        const auto stored = m_repository->getMember(id);
        ASSERT_TRUE(stored.has_value());
        EXPECT_NE(stored->membershipNumber, entry.value)
            << "create must assign the next integer, never a client-supplied number";
        EXPECT_FALSE(stored->membershipNumber.empty());
    }
}

TEST_F(test_core_MemberInjection, FirstNameWithHostilePayloadRoundTrips)
{
    int index = 503;
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        MemberSeed seed = uniqueMemberSeed(index++);
        seed.firstName = entry.value;

        std::int64_t id = 0;
        const auto created = m_repository->createMember(seed.toInput());
        ASSERT_TRUE(created) << created.error().key;
        id = created.value();
        m_memberCountAtStart = m_db->count("members");

        const auto stored = m_repository->getMember(id);
        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(stored->firstName, entry.value);
    }
}

TEST_F(test_core_MemberInjection, NotesWithHostilePayloadRoundTrips)
{
    int index = 504;
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        MemberSeed seed = uniqueMemberSeed(index++);
        seed.notes = entry.value;

        std::int64_t id = 0;
        const auto created = m_repository->createMember(seed.toInput());
        ASSERT_TRUE(created) << created.error().key;
        id = created.value();
        m_memberCountAtStart = m_db->count("members");

        const auto stored = m_repository->getMember(id);
        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(stored->notes, entry.value);
    }
}

TEST_F(test_core_MemberInjection, EmailWithHostilePayloadIsRejectedOrStoredVerbatim)
{
    int index = 505;
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        MemberSeed seed = uniqueMemberSeed(index++);
        seed.email = entry.value;

        const auto created = m_repository->createMember(seed.toInput());
        m_memberCountAtStart = m_db->count("members");

        // Two acceptable outcomes and no third. Either the address failed the
        // well-formedness check -- which is where every one of these payloads
        // should land -- or it was bound as a parameter and came back byte for
        // byte. What must never happen is that it ran, and cleanup() checks the
        // schema after every row.
        if (!created) {
            EXPECT_FALSE(created.error().key.empty());
            continue;
        }
        const std::int64_t id = created.value();

        const auto stored = m_repository->getMember(id);
        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(stored->email, trimmed(entry.value));
    }
}

TEST_F(test_core_MemberInjection, SearchWithPercentDoesNotMatchEverything)
{
    MemberSeed literal = uniqueMemberSeed(505);
    literal.lastName = "100% Cotton";
    ASSERT_GT(seedMember(*m_db, literal), 0);
    m_memberCountAtStart = m_db->count("members");

    MemberQuery query;
    query.search = "%";

    const auto results = VLMS_UNWRAP(m_repository->listMembers(query));
    ASSERT_EQ(static_cast<int>(results.size()), 1);
    EXPECT_EQ(results.front().lastName, literal.lastName);
}

TEST_F(test_core_MemberInjection, HostileSortColumnUsesDefaultOrder)
{
    MemberQuery safe;
    const auto expected = VLMS_UNWRAP(m_repository->listMembers(safe));
    ASSERT_FALSE(expected.empty());

    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);
        MemberQuery query;
        query.sortColumn = entry.value;
        const auto rows = VLMS_UNWRAP(m_repository->listMembers(query));
        ASSERT_EQ(rows.size(), expected.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            EXPECT_EQ(rows.at(i).id, expected.at(i).id);
        }
    }
}
