#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CatalogTypes.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace VLMS::Test;

namespace {

const std::vector<std::string> kCoreTables = {
    "books",      "book_copies", "authors",   "publishers",
    "categories", "members",     "loans",     "employees",
};

}  // namespace

/**
 * Every test here asserts two things:
 *   (a) the hostile string is treated as a literal value — the query returns
 *       the CORRECT result, not merely "no crash";
 *   (b) the schema and every row count survive unchanged (checked in cleanup).
 *
 * (b) alone is weak: a payload that silently matches nothing would pass it.
 * (a) is what proves the value reached SQLite as data.
 */
class test_core_CatalogInjection : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        seedBaseline();
        m_bookCountAtStart = m_db->count("books");
    }

    void TearDown() override
    {
        if (m_db != nullptr && m_db->isValid()) {
            std::string whatChanged;
            EXPECT_TRUE(schemaIsIntact(*m_db, kCoreTables, &whatChanged)) << whatChanged;
            EXPECT_EQ(m_db->count("books"), m_bookCountAtStart);
        }
        m_repository.reset();
        m_db.reset();
    }

    void seedBaseline()
    {
        for (int i = 0; i < 3; ++i) {
            ASSERT_GT(seedBook(*m_db, uniqueBookSeed(i)), 0);
        }
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_repository;
    int m_bookCountAtStart = 0;
};

TEST_F(test_core_CatalogInjection, SearchWithHostilePayloadIsTreatedAsLiteral)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        BookQuery query;
        query.search = entry.value;

        const auto results = VLMS_UNWRAP(m_repository->listBooks(query));

        // None of the seeded titles ("Test Book 0..2") contain any payload, so a
        // correctly-bound search matches nothing. If a payload were executed as
        // SQL, an injected OR would return every row instead.
        EXPECT_TRUE(results.empty())
            << "payload matched " << results.size() << " book(s); it should match none";
    }
}

TEST_F(test_core_CatalogInjection, CountBooksAgreesWithListBooksForHostileSearch)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        BookQuery query;
        query.search = entry.value;

        // listBooks and countBooks build their WHERE clauses separately; a payload
        // that diverged between them would show up here.
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countBooks(query)),
                  static_cast<int>(VLMS_UNWRAP(m_repository->listBooks(query)).size()));
    }
}

TEST_F(test_core_CatalogInjection, CategoryCodeWithHostilePayloadMatchesNothing)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        BookQuery query;
        query.categoryCodes = {entry.value};

        EXPECT_EQ(static_cast<int>(VLMS_UNWRAP(m_repository->listBooks(query)).size()), 0);
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countBooks(query)), 0);
    }
}

TEST_F(test_core_CatalogInjection, LanguageWithHostilePayloadMatchesNothing)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        BookQuery query;
        query.languages = {entry.value};

        EXPECT_EQ(static_cast<int>(VLMS_UNWRAP(m_repository->listBooks(query)).size()), 0);
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countBooks(query)), 0);
    }
}

TEST_F(test_core_CatalogInjection, MultipleCategoryCodesWithMixedPayloads)
{
    const std::int64_t categoryId = seedCategory(*m_db, "HIST", "History");
    ASSERT_GT(categoryId, 0);

    BookSeed seed = uniqueBookSeed(90);
    seed.categoryId = categoryId;
    ASSERT_GT(seedBook(*m_db, seed), 0);
    m_bookCountAtStart = m_db->count("books");

    // Three entries, two hostile, one real. Proves the placeholder loop in
    // listBooks indexes and binds each element independently.
    BookQuery query;
    query.categoryCodes = {
        "'; DROP TABLE books;--",
        "HIST",
        "' OR '1'='1",
    };

    const auto results = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(static_cast<int>(results.size()), 1);
    EXPECT_EQ(results.front().categoryCode, "HIST");
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countBooks(query)), 1);
}

TEST_F(test_core_CatalogInjection, MultipleLanguagesWithMixedPayloads)
{
    BookSeed french = uniqueBookSeed(91);
    french.language = "fr";
    ASSERT_GT(seedBook(*m_db, french), 0);
    m_bookCountAtStart = m_db->count("books");

    BookQuery query;
    query.languages = {
        "' UNION SELECT 1,2,3--",
        "fr",
    };

    const auto results = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(static_cast<int>(results.size()), 1);
    EXPECT_EQ(results.front().language, "fr");
}

TEST_F(test_core_CatalogInjection, CategoryCodeWithLegitimateApostropheMatchesExactly)
{
    // An apostrophe is ordinary data in a bibliographic code, not an attack.
    // Binding must let it through and match.
    const std::string code = "O'BRIEN";
    const std::int64_t categoryId = seedCategory(*m_db, code, "O'Brien collection");
    ASSERT_GT(categoryId, 0);

    BookSeed seed = uniqueBookSeed(92);
    seed.categoryId = categoryId;
    ASSERT_GT(seedBook(*m_db, seed), 0);
    m_bookCountAtStart = m_db->count("books");

    BookQuery query;
    query.categoryCodes = {code};

    const auto results = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(static_cast<int>(results.size()), 1);
    EXPECT_EQ(results.front().categoryCode, code);
}

TEST_F(test_core_CatalogInjection, SearchWithPercentDoesNotMatchEverything)
{
    // The escapeLike proof. '%' is the LIKE wildcard; if it were not escaped,
    // searching "%" would return every book instead of only the literal match.
    BookSeed literal = uniqueBookSeed(93);
    literal.title = "Ten Percent % Solution";
    ASSERT_GT(seedBook(*m_db, literal), 0);
    m_bookCountAtStart = m_db->count("books");

    BookQuery query;
    query.search = "%";

    const auto results = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(static_cast<int>(results.size()), 1);
    EXPECT_EQ(results.front().title, literal.title);
}

TEST_F(test_core_CatalogInjection, SearchWithUnderscoreDoesNotMatchSingleCharacter)
{
    // '_' is the LIKE single-character wildcard. "Test Book 0" would match an
    // unescaped "_" pattern; only the literal underscore title must come back.
    BookSeed literal = uniqueBookSeed(94);
    literal.title = "snake_case Title";
    ASSERT_GT(seedBook(*m_db, literal), 0);
    m_bookCountAtStart = m_db->count("books");

    BookQuery query;
    query.search = "_";

    const auto results = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(static_cast<int>(results.size()), 1);
    EXPECT_EQ(results.front().title, literal.title);
}

TEST_F(test_core_CatalogInjection, SearchWithBackslashMatchesLiteralBackslash)
{
    // '\' is the declared ESCAPE character, so it must itself be escaped.
    BookSeed literal = uniqueBookSeed(95);
    literal.title = "C:\\Windows\\Path";
    ASSERT_GT(seedBook(*m_db, literal), 0);
    m_bookCountAtStart = m_db->count("books");

    BookQuery query;
    query.search = "\\";

    const auto results = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(static_cast<int>(results.size()), 1);
    EXPECT_EQ(results.front().title, literal.title);
}

TEST_F(test_core_CatalogInjection, BookTitleWithHostilePayloadRoundTrips)
{
    int index = 200;
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        BookSeed seed = uniqueBookSeed(index++);
        seed.title = entry.value;

        CatalogRepository repository(m_db->session(), m_db->resourcesDirectory());
        std::int64_t id = 0;
        const auto created = repository.createBook(seed.toInput());
        ASSERT_TRUE(created) << created.error().key;
        id = created.value();
        m_bookCountAtStart = m_db->count("books");

        const auto stored = repository.getBook(id);
        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(stored->title, entry.value);
    }
}

TEST_F(test_core_CatalogInjection, AuthorNameWithHostilePayloadRoundTrips)
{
    int index = 201;
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        BookSeed seed = uniqueBookSeed(index++);
        seed.authorName = entry.value;

        std::int64_t id = 0;
        const auto created = m_repository->createBook(seed.toInput());
        ASSERT_TRUE(created) << created.error().key;
        id = created.value();
        m_bookCountAtStart = m_db->count("books");

        const auto stored = m_repository->getBook(id);
        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(stored->authorName, entry.value);
    }
}

TEST_F(test_core_CatalogInjection, CategoryCodeWithHostilePayloadRoundTrips)
{
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        const auto created = m_repository->createCategory(entry.value, "label");
        ASSERT_TRUE(created) << created.error().key;
        const std::int64_t id = created.value();

        const auto categories = VLMS_UNWRAP(m_repository->listAllCategories());
        bool found = false;
        for (const CategoryRecord& category : categories) {
            if (category.id == id) {
                EXPECT_EQ(category.code, entry.value);
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }
}

/// Every per-copy field became writable when the editor grew a copies table.
/// They are catalogue text off a stock sheet, so they hold apostrophes and
/// stranger things quite legitimately -- and they all reach SQL.
TEST_F(test_core_CatalogInjection, CopyFieldsWithHostilePayloadRoundTrip)
{
    int index = 500;
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);

        BookSeed seed = uniqueBookSeed(index++);
        seed.initialCopyCount = 1;
        const std::int64_t bookId = seedBook(*m_db, seed);
        ASSERT_GT(bookId, 0);
        // Seeding a book of our own moves the baseline cleanup() checks against.
        m_bookCountAtStart = m_db->count("books");

        const std::vector<BookCopyRecord> before = VLMS_UNWRAP(m_repository->listCopies(bookId));
        ASSERT_EQ(static_cast<int>(before.size()), 1);

        BookCopyInput copy;
        copy.id = before.front().id;
        copy.source = before.front().source;
        copy.localId = entry.value;
        copy.globalCopyId = entry.value + "-g";
        copy.centralId = entry.value;
        copy.classification = entry.value;
        copy.subject = entry.value;
        copy.indexCode = entry.value;
        copy.location = entry.value;
        copy.inventoryStatus = entry.value;
        copy.compensation = entry.value;
        copy.notes = entry.value;

        const auto mutated = m_repository->saveCopies(bookId, {copy});
        ASSERT_TRUE(mutated) << mutated.error().key;

        const std::vector<BookCopyRecord> after = VLMS_UNWRAP(m_repository->listCopies(bookId));
        ASSERT_EQ(static_cast<int>(after.size()), 1);

        const BookCopyRecord& stored = after.front();
        EXPECT_EQ(stored.localId, entry.value);
        EXPECT_EQ(stored.globalCopyId, entry.value + "-g");
        EXPECT_EQ(stored.centralId, entry.value);
        EXPECT_EQ(stored.classification, entry.value);
        EXPECT_EQ(stored.subject, entry.value);
        EXPECT_EQ(stored.indexCode, entry.value);
        EXPECT_EQ(stored.location, entry.value);
        EXPECT_EQ(stored.inventoryStatus, entry.value);
        EXPECT_EQ(stored.compensation, entry.value);
        EXPECT_EQ(stored.notes, entry.value);

        // The book must still be there: a payload that dropped a table would show
        // up here rather than as a passing round trip over rubble.
        EXPECT_TRUE(m_repository->getBook(bookId).has_value());
    }
}

TEST_F(test_core_CatalogInjection, HostileSortColumnUsesDefaultOrder)
{
    BookQuery safe;
    const auto expected = VLMS_UNWRAP(m_repository->listBooks(safe));
    ASSERT_FALSE(expected.empty());

    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);
        BookQuery query;
        query.sortColumn = entry.value;
        const auto rows = VLMS_UNWRAP(m_repository->listBooks(query));
        ASSERT_EQ(rows.size(), expected.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            EXPECT_EQ(rows.at(i).id, expected.at(i).id);
        }
    }
}
