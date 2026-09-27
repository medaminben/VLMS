#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CatalogTypes.h>
#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/LoanTypes.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

using namespace VLMS::Test;

namespace {

/// One book's loan history reaches every copy it has ever had, and no other
/// book's. The filter is one clause over the book_copies join every loan query
/// already makes.
class test_core_LoanBookFilter : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());

        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_GT(m_memberId, 0);

        BookSeed first = uniqueBookSeed(1);
        first.initialCopyCount = 2;
        m_firstBookId = seedBook(*m_db, first);
        ASSERT_GT(m_firstBookId, 0);

        BookSeed second = uniqueBookSeed(2);
        second.initialCopyCount = 1;
        m_secondBookId = seedBook(*m_db, second);
        ASSERT_GT(m_secondBookId, 0);

        const auto firstCopies = copyIdsOf(*m_db, m_firstBookId);
        ASSERT_EQ(firstCopies.size(), 2U);
        const auto secondCopies = copyIdsOf(*m_db, m_secondBookId);
        ASSERT_EQ(secondCopies.size(), 1U);

        // Two loans of the first book, on two different copies, one returned.
        ASSERT_GT(rawInsertLoan(*m_db, m_memberId, firstCopies.at(0),
                                "2025-01-10", "2025-01-24", "2025-01-20"), 0);
        ASSERT_GT(rawInsertLoan(*m_db, m_memberId, firstCopies.at(1),
                                "2025-02-10", "2025-02-24"), 0);
        // One loan of the second book, which must never show up.
        ASSERT_GT(rawInsertLoan(*m_db, m_memberId, secondCopies.at(0),
                                "2025-03-10", "2025-03-24"), 0);
    }

    void TearDown() override
    {
        m_circulation.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::int64_t m_memberId = 0;
    std::int64_t m_firstBookId = 0;
    std::int64_t m_secondBookId = 0;
};

TEST_F(test_core_LoanBookFilter, ListingByBookReturnsEveryCopysLoanAndNoOthers)
{
    LoanQuery query;
    query.bookId = m_firstBookId;
    query.archive = ArchiveScope::Any;

    const auto loans = m_circulation->listLoans(query);
    ASSERT_TRUE(loans.has_value());
    EXPECT_EQ(loans.value().size(), 2U);
    for (const LoanRecord& loan : loans.value()) {
        EXPECT_NE(loan.borrowedAt, "2025-03-10") << "a loan of another book leaked in";
    }
}

TEST_F(test_core_LoanBookFilter, CountingByBookCountsOnlyThatBook)
{
    LoanQuery query;
    query.bookId = m_secondBookId;
    query.archive = ArchiveScope::Any;

    const auto count = m_circulation->countLoans(query);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 1);
}

TEST_F(test_core_LoanBookFilter, ABookWithNoLoansCountsZero)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(3));
    ASSERT_GT(bookId, 0);

    LoanQuery query;
    query.bookId = bookId;
    query.archive = ArchiveScope::Any;

    const auto count = m_circulation->countLoans(query);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 0);
}

TEST_F(test_core_LoanBookFilter, AnArchivedCopysLoanIsStillPartOfTheHistory)
{
    // Archive the first book's first copy by submitting only the second: the
    // copy leaves the live list, but the loan it once carried still happened.
    // It has to be the first copy — saveCopies refuses to archive a copy that
    // is still out (error.copy.onLoan), and only the first one came back.
    CatalogRepository catalog(m_db->session(), m_db->resourcesDirectory());
    const auto before = catalog.listCopies(m_firstBookId);
    ASSERT_TRUE(before.has_value());
    ASSERT_EQ(before.value().size(), 2U);

    BookCopyInput survivor;
    survivor.id = before.value().at(1).id;
    survivor.source = before.value().at(1).source;
    survivor.localId = before.value().at(1).localId;
    survivor.globalCopyId = before.value().at(1).globalCopyId;
    const auto saved = catalog.saveCopies(m_firstBookId, {survivor});
    ASSERT_TRUE(saved) << saved.error().key;

    LoanQuery query;
    query.bookId = m_firstBookId;
    query.archive = ArchiveScope::Any;

    const auto count = m_circulation->countLoans(query);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 2) << "archiving a copy erased its loan from the history";
}

TEST_F(test_core_LoanBookFilter, LeavingTheBookUnsetChangesNothing)
{
    LoanQuery query;
    query.archive = ArchiveScope::Any;

    const auto count = m_circulation->countLoans(query);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 3);
}

TEST_F(test_core_LoanBookFilter, AvailableCopiesCanBeScopedToOneBook)
{
    // A third book, untouched, gives the scope something to exclude.
    BookSeed spare = uniqueBookSeed(4);
    spare.title = "Spare Book";
    spare.initialCopyCount = 2;
    const std::int64_t spareId = seedBook(*m_db, spare);
    ASSERT_GT(spareId, 0);

    const auto everything = m_circulation->listAvailableCopies();
    ASSERT_TRUE(everything.has_value());
    EXPECT_GE(everything.value().size(), 3U) << "every book's free copies";

    const auto scoped = m_circulation->listAvailableCopies({}, spareId);
    ASSERT_TRUE(scoped.has_value());
    EXPECT_EQ(scoped.value().size(), 2U);
    for (const LoanCopyOption& copy : scoped.value()) {
        EXPECT_EQ(copy.bookTitle, "Spare Book") << "another book's copy leaked in";
    }
}

TEST_F(test_core_LoanBookFilter, ScopingLeavesOutACopyThatIsOnLoan)
{
    // The first book has two copies; the second one's loan was never returned.
    const auto scoped = m_circulation->listAvailableCopies({}, m_firstBookId);
    ASSERT_TRUE(scoped.has_value());
    EXPECT_EQ(scoped.value().size(), 1U) << "the copy still out must not be offered";
}

}  // namespace
