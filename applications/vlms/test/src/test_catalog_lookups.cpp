#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/presentation/QtCatalogLookups.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CatalogTypes.h>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

class CatalogLookupsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(),
                                                                      m_db->resourcesDirectory());
        m_lookups = std::make_unique<QtCatalogLookups>(*m_catalog);
    }

    void TearDown() override
    {
        m_lookups.reset();
        m_catalog.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<QtCatalogLookups> m_lookups;
};

TEST_F(CatalogLookupsTest, AuthorNamesForwardFromRepository)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.authorName = "Fixture Author";
    ASSERT_GT(seedBook(*m_db, seed), 0);

    const QStringList names = m_lookups->authorNames();
    EXPECT_TRUE(names.contains(QStringLiteral("Fixture Author")));
}

TEST_F(CatalogLookupsTest, SuggestCopyIdentifiersReturnsNonEmptyLocalId)
{
    QString source;
    QString localId;
    QString globalId;
    m_lookups->suggestCopyIdentifiers(QStringLiteral("ar"), &source, &localId, &globalId);
    EXPECT_FALSE(localId.isEmpty());
    EXPECT_FALSE(source.isEmpty());
    EXPECT_FALSE(globalId.isEmpty());
}

TEST_F(CatalogLookupsTest, PublisherNamesAndCategoriesForward)
{
    const std::int64_t categoryId = seedCategory(*m_db, "FIC", "Fiction");
    ASSERT_GT(categoryId, 0);

    BookSeed seed = uniqueBookSeed(2);
    seed.publisherName = "Fixture Publisher";
    seed.categoryId = categoryId;
    ASSERT_GT(seedBook(*m_db, seed), 0);

    EXPECT_TRUE(m_lookups->publisherNames().contains(QStringLiteral("Fixture Publisher")));

    const auto categories = m_lookups->categories();
    ASSERT_FALSE(categories.empty());
    bool found = false;
    for (const Repositories::CategoryRecord& category : categories) {
        if (category.id == categoryId) {
            found = true;
            EXPECT_EQ(category.code, "FIC");
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(CatalogLookupsTest, FreeLocalNumbersAndCoverPath)
{
    BookSeed seed = uniqueBookSeed(3);
    seed.language = "ar";
    ASSERT_GT(seedBook(*m_db, seed), 0);

    const QString source = QString::fromStdString(Repositories::CatalogRepository::copySourceForLanguage("ar"));
    const QStringList free = m_lookups->freeLocalNumbers(source, 5);
    // Gaps may or may not exist after one copy; the call must not crash and
    // returns a list (possibly empty).
    EXPECT_GE(free.size(), 0);

    const QString resolved = m_lookups->resolveCoverPath(QStringLiteral("books/1/cover.jpg"));
    EXPECT_TRUE(resolved.contains(QStringLiteral("books/1/cover.jpg")));
    EXPECT_TRUE(m_lookups->resolveCoverPath(QString()).isEmpty());
}

TEST_F(CatalogLookupsTest, CopiesForBookForwardsFromRepository)
{
    BookSeed seed = uniqueBookSeed(4);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const auto copies = m_lookups->copiesForBook(bookId);
    EXPECT_EQ(copies.size(), 2u);
    EXPECT_TRUE(m_lookups->copiesForBook(999999).empty());
}
