#include "TestDatabase.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CatalogRepository.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace VLMS;
using namespace Test;

/// A number is free when no book_copies row holds it -- live or archived.
/// Computed, never stored: a permanently removed number rejoins the list by
/// itself, and the gaps the 2022 import left are in it from the first run.
class test_core_FreeLocalNumbers : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_catalog.reset();
        m_db.reset();
    }

    /// Seeds one arabic book whose copies carry exactly `numbers`.
    void seedArabicNumbers(int index, const std::vector<int>& numbers)
    {
        BookSeed seed = uniqueBookSeed(index);
        seed.language = "ar";
        seed.initialCopyCount = static_cast<int>(numbers.size());
        const std::int64_t bookId = seedBook(*m_db, seed);
        const auto copies = copyIdsOf(*m_db, bookId);
        ASSERT_EQ(copies.size(), numbers.size());
        for (std::size_t i = 0; i < numbers.size(); ++i) {
            ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(i), std::to_string(numbers.at(i))));
        }
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
};

TEST_F(test_core_FreeLocalNumbers, TheGapsBetweenTheNumbersInUseAreFree)
{
    seedArabicNumbers(1, {1, 2, 5, 9});

    const auto free = VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100));
    EXPECT_EQ(free, (std::vector<std::string>{"3", "4", "6", "7", "8"}));
}

TEST_F(test_core_FreeLocalNumbers, AContiguousStockHasNoFreeNumbers)
{
    seedArabicNumbers(2, {1, 2, 3});

    EXPECT_TRUE(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100)).empty());
}

TEST_F(test_core_FreeLocalNumbers, AnArchivedCopyStillHoldsItsNumberButAPurgedOneGivesItBack)
{
    BookSeed seed = uniqueBookSeed(3);
    seed.language = "ar";
    seed.initialCopyCount = 3;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(0), "1"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(1), "2"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(2), "3"));
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    EXPECT_TRUE(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100)).empty());

    ASSERT_TRUE(m_catalog->purgeCopy(copies.at(1)));
    EXPECT_EQ(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100)),
              (std::vector<std::string>{"2"}));
}

TEST_F(test_core_FreeLocalNumbers, EachStockIsCountedOnItsOwn)
{
    seedArabicNumbers(4, {1, 4});

    BookSeed foreign = uniqueBookSeed(5);
    foreign.language = "fr";
    foreign.initialCopyCount = 2;
    const std::int64_t foreignBook = seedBook(*m_db, foreign);
    const auto foreignCopies = copyIdsOf(*m_db, foreignBook);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, foreignCopies.at(0), "1"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, foreignCopies.at(1), "2"));

    EXPECT_EQ(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100)),
              (std::vector<std::string>{"2", "3"}));
    EXPECT_TRUE(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("foreign", 100)).empty());
}

TEST_F(test_core_FreeLocalNumbers, TheCapIsHonouredBecauseTheDropDownIsAChoiceNotAHaystack)
{
    seedArabicNumbers(6, {1, 20});

    const auto free = VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 3));
    EXPECT_EQ(free, (std::vector<std::string>{"2", "3", "4"}));
}
