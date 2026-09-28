#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Repositories/LoanTypes.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using VLMS::Date;
using namespace VLMS;
using namespace Test;

namespace {

const std::vector<std::string> kCoreTables = {
    "loans", "members", "books", "book_copies", "employees",
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

class test_core_CirculationInjection : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<Repositories::CirculationRepository>(m_db->session());

        MemberSeed member = uniqueMemberSeed(1);
        member.status = Repositories::MemberStatus::kActive;
        m_memberId = seedMember(*m_db, member);
        ASSERT_GT(m_memberId, 0);

        BookSeed book = uniqueBookSeed(1);
        book.initialCopyCount = 3;
        const std::int64_t bookId = seedBook(*m_db, book);
        ASSERT_GT(bookId, 0);
        m_copyIds = copyIdsOf(*m_db, bookId);
        ASSERT_EQ(static_cast<int>(m_copyIds.size()), 3);

        // One open loan, one returned, so every filter has something to select.
        const Date today = Date::todayLocal();
        ASSERT_GT(rawInsertLoan(*m_db, m_memberId, m_copyIds.at(0), today.addDays(-3).toIso(),
                                today.addDays(11).toIso()),
                  0);
        ASSERT_GT(rawInsertLoan(*m_db, m_memberId, m_copyIds.at(1), today.addDays(-30).toIso(),
                                today.addDays(-16).toIso(), today.addDays(-20).toIso()),
                  0);

        m_loanCountAtStart = m_db->count("loans");
        ASSERT_EQ(m_loanCountAtStart, 2);
    }

    void TearDown() override
    {
        if (m_db != nullptr && m_db->isValid()) {
            std::string whatChanged;
            EXPECT_TRUE(schemaIsIntact(*m_db, kCoreTables, &whatChanged)) << whatChanged;
            EXPECT_EQ(m_db->count("loans"), m_loanCountAtStart);
        }
        m_repository.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CirculationRepository> m_repository;
    std::int64_t m_memberId = 0;
    std::vector<std::int64_t> m_copyIds;
    int m_loanCountAtStart = 0;
};

TEST_F(test_core_CirculationInjection, SearchWithHostilePayloadIsTreatedAsLiteral)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        Repositories::LoanQuery query;
        query.search = entry.value;

        EXPECT_TRUE(VLMS_UNWRAP(m_repository->listLoans(query)).empty());
    }
}

TEST_F(test_core_CirculationInjection, CountLoansAgreesWithListLoansForHostileSearch)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        Repositories::LoanQuery query;
        query.search = entry.value;

        EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(query)),
                  static_cast<int>(VLMS_UNWRAP(m_repository->listLoans(query)).size()));
    }
}

TEST_F(test_core_CirculationInjection, FilterWithHostileSqlFragmentIsSilentlyDropped)
{
    std::vector<std::pair<const char*, std::string>> cases;
    for (const HostilePayload& entry : hostilePayloads()) {
        cases.emplace_back(entry.id, entry.value);
    }
    // These specifically target the one place where real SQL text is joined:
    // listLoans appends filterClauses.join(" OR ") into the WHERE clause.
    cases.emplace_back("closeParenOrTrue", "open) OR 1=1 --");
    cases.emplace_back("openWithTail", "open OR 1=1");
    cases.emplace_back("overdueWithUnion", "overdue') UNION SELECT 1--");

    for (const auto& [id, payload] : cases) {
        SCOPED_TRACE(id);

        Repositories::LoanQuery hostile;
        hostile.filters = {payload};

        Repositories::LoanQuery unfiltered;

        // An unrecognised filter falls through the whitelist and contributes no
        // clause, so the result must equal the no-filter case. Critically it must
        // NOT equal the open-filter case, or the payload changed the query.
        const auto hostileResults = VLMS_UNWRAP(m_repository->listLoans(hostile));
        const auto unfilteredResults = VLMS_UNWRAP(m_repository->listLoans(unfiltered));
        EXPECT_EQ(hostileResults.size(), unfilteredResults.size());
        EXPECT_EQ(static_cast<int>(hostileResults.size()), 2);
    }
}

TEST_F(test_core_CirculationInjection, FilterListWithMixOfValidAndHostileCodes)
{
    Repositories::LoanQuery query;
    query.filters = {
        "'; DROP TABLE loans;--",
        Repositories::LoanFilter::kReturned,
    };

    // The one recognised code still applies; the hostile entry adds nothing.
    const auto results = VLMS_UNWRAP(m_repository->listLoans(query));
    ASSERT_EQ(static_cast<int>(results.size()), 1);
    EXPECT_FALSE(results.front().returnedAt.empty());
}

TEST_F(test_core_CirculationInjection, UnknownFilterCodeBehavesAsNoFilter)
{
    Repositories::LoanQuery query;
    query.filters = {"definitely-not-a-filter"};

    EXPECT_EQ(static_cast<int>(VLMS_UNWRAP(m_repository->listLoans(query)).size()), 2);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countLoans(query)), 2);
}

TEST_F(test_core_CirculationInjection, BorrowableMemberSearchWithHostilePayload)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);
        EXPECT_TRUE(VLMS_UNWRAP(m_repository->listBorrowableMembers(entry.value)).empty());
    }
}

TEST_F(test_core_CirculationInjection, AvailableCopySearchWithHostilePayload)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);
        EXPECT_TRUE(VLMS_UNWRAP(m_repository->listAvailableCopies(entry.value)).empty());
    }
}

TEST_F(test_core_CirculationInjection, HostileSortColumnUsesDefaultOrder)
{
    Repositories::LoanQuery safe;
    const auto expected = VLMS_UNWRAP(m_repository->listLoans(safe));
    ASSERT_FALSE(expected.empty());
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);
        Repositories::LoanQuery query;
        query.sortColumn = entry.value;
        const auto rows = VLMS_UNWRAP(m_repository->listLoans(query));
        ASSERT_EQ(rows.size(), expected.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            EXPECT_EQ(rows.at(i).id, expected.at(i).id);
        }
    }
}

TEST_F(test_core_CirculationInjection, LoanNotesWithHostilePayloadRoundTrip)
{
    int bookIndex = 200;
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        BookSeed extra = uniqueBookSeed(bookIndex++);
        extra.initialCopyCount = 1;
        const std::int64_t bookId = seedBook(*m_db, extra);
        ASSERT_GT(bookId, 0);
        const auto copies = copyIdsOf(*m_db, bookId);
        ASSERT_EQ(static_cast<int>(copies.size()), 1);

        const Date today = Date::todayLocal();
        Repositories::LoanInput input;
        input.memberId = m_memberId;
        input.bookCopyId = copies.front();
        input.borrowedAt = today.toIso();
        input.dueAt = today.addDays(14).toIso();
        input.notes = entry.value;

        std::int64_t id = 0;
        const auto created = m_repository->createLoan(input);
        ASSERT_TRUE(created) << created.error().key;
        id = created.value();
        m_loanCountAtStart = m_db->count("loans");

        const auto stored = m_repository->getLoan(id);
        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(stored->notes, trimmed(entry.value));
    }
}
