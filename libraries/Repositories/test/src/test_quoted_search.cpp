// Search terms in double quotes match whole values, like a web search:
// 1 finds 1, 10, 21 and "Book 1"; "1" finds only what is exactly 1.

#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace VLMS;
using namespace Test;

class test_core_QuotedSearch : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(),
                                                                     m_db->resourcesDirectory());
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(),
                                                                      m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());

        // Members numbered 1..12 and books "Test Book 1".."Test Book 12",
        // whose single copies carry local numbers 1..12 in creation order.
        for (int i = 1; i <= 12; ++i) {
            MemberSeed member = uniqueMemberSeed(i);
            member.membershipNumber = std::to_string(i);
            m_memberIds.push_back(seedMember(*m_db, member));
            BookSeed book = uniqueBookSeed(i);
            book.title = "Test Book " + std::to_string(i);
            m_copyIds.push_back(copyIdsOf(*m_db, seedBook(*m_db, book)).front());
        }
    }

    void TearDown() override
    {
        m_circulation.reset();
        m_catalog.reset();
        m_members.reset();
        m_db.reset();
    }

    std::string localIdOf(std::int64_t copyId) const
    {
        return m_db->scalar("SELECT local_id FROM book_copies WHERE id = " + std::to_string(copyId))
            .toString();
    }

    std::vector<std::string> memberNumbers(const std::string& search) const
    {
        Repositories::MemberQuery query;
        query.search = search;
        const auto members = m_members->listMembers(query);
        EXPECT_TRUE(members);
        std::vector<std::string> numbers;
        for (const auto& member : members.value()) {
            numbers.push_back(member.membershipNumber);
        }
        std::sort(numbers.begin(), numbers.end());
        return numbers;
    }

    std::vector<std::string> bookTitles(const std::string& search) const
    {
        Repositories::BookQuery query;
        query.search = search;
        const auto books = m_catalog->listBooks(query);
        EXPECT_TRUE(books);
        std::vector<std::string> titles;
        for (const auto& book : books.value()) {
            titles.push_back(book.title);
        }
        std::sort(titles.begin(), titles.end());
        return titles;
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::vector<std::int64_t> m_memberIds;
    std::vector<std::int64_t> m_copyIds;
};

TEST_F(test_core_QuotedSearch, CopyLocalNumbersFollowCreationOrder)
{
    ASSERT_EQ(localIdOf(m_copyIds.at(0)), "1");
    ASSERT_EQ(localIdOf(m_copyIds.at(9)), "10");
}

TEST_F(test_core_QuotedSearch, MembersPlainNumberStillMatchesAnywhere)
{
    EXPECT_EQ(memberNumbers("1"), (std::vector<std::string>{"1", "10", "11", "12"}));
}

TEST_F(test_core_QuotedSearch, MembersQuotedNumberMatchesOnlyThatMember)
{
    EXPECT_EQ(memberNumbers("\"1\""), std::vector<std::string>{"1"});
    EXPECT_EQ(memberNumbers(" \"12\" "), std::vector<std::string>{"12"});
}

TEST_F(test_core_QuotedSearch, BooksQuotedTitleMatchesOnlyThatTitle)
{
    EXPECT_EQ(bookTitles("Test Book 1").size(), 4U);
    EXPECT_EQ(bookTitles("\"Test Book 1\""), std::vector<std::string>{"Test Book 1"});
}

TEST_F(test_core_QuotedSearch, BooksQuotedNumberMatchesOnlyThatCopyNumber)
{
    EXPECT_EQ(bookTitles("\"1\""), std::vector<std::string>{"Test Book 1"});
    EXPECT_EQ(bookTitles("\"10\""), std::vector<std::string>{"Test Book 10"});
}

TEST_F(test_core_QuotedSearch, CopyRowsQuotedNumberMatchesOnlyThatCopy)
{
    Repositories::CopyQuery query;
    query.search = "\"1\"";
    const auto rows = m_catalog->listCopyRows(query);
    ASSERT_TRUE(rows);
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->front().localId, "1");
}

TEST_F(test_core_QuotedSearch, LoansQuotedNumberMatchesThatMemberOrThatCopy)
{
    // Member 1 borrows copy 2, member 10 borrows copy 10, member 3 borrows copy 1.
    const auto lend = [this](std::size_t member, std::size_t copy) {
        ASSERT_GT(rawInsertLoan(*m_db, m_memberIds.at(member), m_copyIds.at(copy), "2026-09-01",
                                "2026-09-15"),
                  0);
    };
    lend(0, 1);
    lend(9, 9);
    lend(2, 0);

    Repositories::LoanQuery query;
    query.search = "\"1\"";
    const auto loans = m_circulation->listLoans(query);
    ASSERT_TRUE(loans);
    std::vector<std::string> seen;
    for (const auto& loan : loans.value()) {
        seen.push_back(loan.membershipNumber + "/" + localIdOf(loan.bookCopyId));
    }
    std::sort(seen.begin(), seen.end());
    EXPECT_EQ(seen, (std::vector<std::string>{"1/2", "3/1"}));

    query.search = "1";
    EXPECT_EQ(m_circulation->listLoans(query)->size(), 3U);
}

TEST_F(test_core_QuotedSearch, CheckoutPickersHonourQuotes)
{
    const auto members = m_circulation->listBorrowableMembers("\"1\"");
    ASSERT_TRUE(members);
    ASSERT_EQ(members->size(), 1U);

    const auto copies = m_circulation->listAvailableCopies("\"1\"");
    ASSERT_TRUE(copies);
    ASSERT_EQ(copies->size(), 1U);
}
