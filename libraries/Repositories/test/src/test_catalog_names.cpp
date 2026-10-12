#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CatalogRepository.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace VLMS;
using namespace Test;

namespace fs = std::filesystem;

class test_core_CatalogNames : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<Repositories::CatalogRepository>(m_db->session(),
                                                                         m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_repository.reset();
        m_db.reset();
    }

    void seed(int index, const std::string& author, const std::string& publisher)
    {
        BookSeed book = uniqueBookSeed(index);
        book.authorName = author;
        book.publisherName = publisher;
        ASSERT_GT(seedBook(*m_db, book), 0);
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_repository;
};

TEST_F(test_core_CatalogNames, AnEmptyCatalogHasNoNames)
{
    const auto authors = m_repository->listAuthorNames();
    const auto publishers = m_repository->listPublisherNames();
    ASSERT_TRUE(authors);
    ASSERT_TRUE(publishers);
    EXPECT_TRUE(authors->empty());
    EXPECT_TRUE(publishers->empty());
}

TEST_F(test_core_CatalogNames, AuthorsAreListedOnceInCaseInsensitiveOrder)
{
    seed(1, "zola", "Dar A");
    seed(2, "Ibn Khaldun", "Dar A");
    seed(3, "Balzac", "Dar B");
    seed(4, "Ibn Khaldun", "Dar B");

    const auto authors = m_repository->listAuthorNames();
    ASSERT_TRUE(authors);
    EXPECT_EQ(authors.value(), (std::vector<std::string>{"Balzac", "Ibn Khaldun", "zola"}));
}

TEST_F(test_core_CatalogNames, PublishersAreListedOnceInCaseInsensitiveOrder)
{
    seed(1, "A", "dar al-Kutub");
    seed(2, "B", "Actes Sud");
    seed(3, "C", "dar al-Kutub");

    const auto publishers = m_repository->listPublisherNames();
    ASSERT_TRUE(publishers);
    EXPECT_EQ(publishers.value(), (std::vector<std::string>{"Actes Sud", "dar al-Kutub"}));
}

TEST_F(test_core_CatalogNames, AuthorsAndPublishersAreSeparateLists)
{
    seed(1, "Same Name", "Other House");

    EXPECT_EQ(m_repository->listAuthorNames().value(), std::vector<std::string>{"Same Name"});
    EXPECT_EQ(m_repository->listPublisherNames().value(), std::vector<std::string>{"Other House"});
}

TEST_F(test_core_CatalogNames, ResolveCoverPathOfNothingIsNothing)
{
    EXPECT_EQ(m_repository->resolveCoverPath(""), "");
}

TEST_F(test_core_CatalogNames, ResolveCoverPathPutsRelativePathsUnderResources)
{
    EXPECT_EQ(fs::path(m_repository->resolveCoverPath("books/7/cover.png")),
              fs::path(m_db->resourcesDirectory()) / "books" / "7" / "cover.png");
}

TEST_F(test_core_CatalogNames, ResolveCoverPathKeepsAbsolutePaths)
{
    const std::string absolute = (fs::temp_directory_path() / "cover.png").string();
    EXPECT_EQ(m_repository->resolveCoverPath(absolute), absolute);
}
