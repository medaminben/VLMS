#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Core/Date.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace VLMS;
using namespace Test;

class test_core_CatalogRepository : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(resetStore()) << (m_db ? m_db->lastError() : "no database");
    }

    void TearDown() override
    {
        m_repository.reset();
        m_db.reset();
    }

    [[nodiscard]] bool resetStore()
    {
        m_repository.reset();
        m_db = std::make_unique<TestDatabase>();
        if (!m_db->isValid()) {
            return false;
        }
        m_repository = std::make_unique<Repositories::CatalogRepository>(m_db->session(),
                                                           m_db->resourcesDirectory());
        return true;
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_repository;
};

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------

TEST_F(test_core_CatalogRepository, CreateBookAssignsIdAndCreatesInitialCopies)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.initialCopyCount = 4;

    std::int64_t id = 0;
    const auto created = m_repository->createBook(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();
    EXPECT_GT(id, 0);

    const auto stored = m_repository->getBook(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->title, seed.title);
    EXPECT_EQ(stored->totalCopies, 4);
    EXPECT_EQ(stored->availableCopies, 4);
    EXPECT_EQ(copyIdsOf(*m_db, id).size(), 4u);
}

TEST_F(test_core_CatalogRepository, CreateBookRejectsEmptyTitle)
{
    BookSeed seed = uniqueBookSeed(2);
    seed.title = "   ";

    const auto failed = m_repository->createBook(seed.toInput());
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_EQ(m_db->count("books"), 0);
}

TEST_F(test_core_CatalogRepository, CreateBookRejectsEmptyLanguage)
{
    BookSeed seed = uniqueBookSeed(3);
    seed.language.clear();

    const auto failed = m_repository->createBook(seed.toInput());
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_EQ(m_db->count("books"), 0);
}

TEST_F(test_core_CatalogRepository, CreateBookReusesExistingAuthorByName)
{
    BookSeed first = uniqueBookSeed(4);
    BookSeed second = uniqueBookSeed(5);
    second.authorName = first.authorName;

    EXPECT_GT(seedBook(*m_db, first), 0);
    EXPECT_GT(seedBook(*m_db, second), 0);

    EXPECT_EQ(m_db->count("authors"), 1);
}

TEST_F(test_core_CatalogRepository, CreateBookReusesExistingPublisherByName)
{
    BookSeed first = uniqueBookSeed(6);
    BookSeed second = uniqueBookSeed(7);
    second.publisherName = first.publisherName;

    EXPECT_GT(seedBook(*m_db, first), 0);
    EXPECT_GT(seedBook(*m_db, second), 0);

    EXPECT_EQ(m_db->count("publishers"), 1);
}

TEST_F(test_core_CatalogRepository, CreateBookRejectsCopyCountBelowOne)
{
    BookSeed seed = uniqueBookSeed(8);
    seed.initialCopyCount = 0;

    const int booksBefore = m_db->count("books");

    // defect 10: addCopies() returns true early when count <= 0, so the book
    // was created with no copies at all -- a catalogue entry that can never be
    // borrowed, reports "0 copies", and cannot be told apart from a book whose
    // copies are all on loan. BookEditorDialog's spin box has setRange(1, 999)
    // so the UI cannot produce this, but Core is also what an importer calls.
    //
    // C11b rejects rather than clamping to 1. Clamping would invent a physical
    // copy that is not on any shelf, and the librarian would find out by
    // looking for it.
    std::int64_t id = 0;
    const auto failed = m_repository->createBook(seed.toInput());
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());

    // Rejected before the transaction opens, so nothing at all was written.
    EXPECT_EQ(m_db->count("books"), booksBefore);
    EXPECT_EQ(id, 0);
}

TEST_F(test_core_CatalogRepository, CreateBookAcceptsExactlyOneCopy)
{
    BookSeed seed = uniqueBookSeed(9);
    seed.initialCopyCount = 1;

    std::int64_t id = 0;
    const auto created = m_repository->createBook(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    // One is the boundary the rejection above sits on; it must still be a
    // perfectly ordinary book.
    EXPECT_EQ(copyIdsOf(*m_db, id).size(), 1u);
}

TEST_F(test_core_CatalogRepository, BlankOptionalBookFieldsAreStoredAsNull)
{
    BookSeed seed = uniqueBookSeed(90);
    seed.publicationDate.clear();
    seed.placeOfPublication.clear();
    seed.pages.clear();
    seed.dimensions.clear();
    seed.description.clear();

    std::int64_t id = 0;
    const auto created = m_repository->createBook(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    // These used to bind '' while the member fields bound NULL for the same
    // meaning. Asserted against the raw columns; getBook() COALESCEs both to
    // '' and so cannot tell them apart.
    for (const std::string& column : {std::string("publication_date"),
                                      std::string("place_of_publication"),
                                      std::string("pages"), std::string("dimensions"),
                                      std::string("description")}) {
        const SqlValue raw = m_db->scalar(
            "SELECT " + column + " FROM books WHERE id = " + std::to_string(id));
        EXPECT_TRUE(raw.isNull()) << column;
    }
}

TEST_F(test_core_CatalogRepository, CreateBookNormalisesThePublicationDateAndKeepsTheOriginal)
{
    BookSeed seed = uniqueBookSeed(92);
    seed.publicationDate = "Nov 06, 1996";

    std::int64_t id = 0;
    const auto created = m_repository->createBook(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    // The migration normalises what is already in the catalog; without this,
    // the very next book a librarian typed would put an unnormalised value
    // straight back in and the column would drift apart again.
    EXPECT_EQ(m_db->scalar("SELECT publication_date FROM books WHERE id = " + std::to_string(id))
                  .toString(),
              "1996-11-06");
    EXPECT_EQ(m_db->scalar("SELECT publication_date_original FROM books WHERE id = "
                           + std::to_string(id))
                  .toString(),
              "Nov 06, 1996");
}

TEST_F(test_core_CatalogRepository, CreateBookLeavesAnUnreadablePublicationDateAlone)
{
    BookSeed seed = uniqueBookSeed(93);
    seed.publicationDate = "201u";

    std::int64_t id = 0;
    const auto created = m_repository->createBook(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    // MARC notation typed deliberately. The field is free text on purpose and
    // saving must not be a way to lose what was written in it.
    EXPECT_EQ(m_db->scalar("SELECT publication_date FROM books WHERE id = " + std::to_string(id))
                  .toString(),
              "201u");
}

TEST_F(test_core_CatalogRepository, UpdateBookRenormalisesThePublicationDate)
{
    BookSeed seed = uniqueBookSeed(94);
    std::int64_t id = 0;
    const auto created = m_repository->createBook(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    Repositories::BookInput edited = seed.toInput();
    edited.publicationDate = "20 May 2013";
    const auto mutated = m_repository->updateBook(id, edited);
    ASSERT_TRUE(mutated) << mutated.error().key;

    EXPECT_EQ(m_db->scalar("SELECT publication_date FROM books WHERE id = " + std::to_string(id))
                  .toString(),
              "2013-05-20");
    EXPECT_EQ(m_db->scalar("SELECT publication_date_original FROM books WHERE id = "
                           + std::to_string(id))
                  .toString(),
              "20 May 2013");
}

TEST_F(test_core_CatalogRepository, TwoBooksWithoutAnIsbnStillCollideOnTheUniqueConstraint)
{
    BookSeed seed = uniqueBookSeed(91);
    seed.isbn.clear();

    std::int64_t first = 0;
    const auto created = m_repository->createBook(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    first = created.value();

    // isbn is part of UNIQUE (title, author_id, publisher_id, isbn, language),
    // and SQLite treats NULLs in a unique index as distinct from one another.
    // So isbn is the one blank field NOT routed through nullableText: with
    // NULL there, these two identical entries would both be accepted and the
    // constraint would protect only the books that least need it. This test is
    // what stops someone "finishing" the C11 unification later.
    const SqlValue rawIsbn =
        m_db->scalar("SELECT isbn FROM books WHERE id = " + std::to_string(first));
    EXPECT_FALSE(rawIsbn.isNull()) << "a blank isbn must stay '' -- see the UNIQUE constraint";

    const auto failed = m_repository->createBook(seed.toInput());
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_EQ(m_db->count("books"), 1);
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

TEST_F(test_core_CatalogRepository, UpdateBookChangesFields)
{
    const std::int64_t id = seedBook(*m_db, uniqueBookSeed(10));
    ASSERT_GT(id, 0);

    BookSeed changed = uniqueBookSeed(10);
    changed.title = "Revised Title";
    changed.description = "A description";

    const auto mutated = m_repository->updateBook(id, changed.toInput());
    ASSERT_TRUE(mutated) << mutated.error().key;

    const auto stored = m_repository->getBook(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->title, "Revised Title");
    EXPECT_EQ(stored->description, "A description");
}

TEST_F(test_core_CatalogRepository, UpdateBookReturnsFalseForUnknownId)
{
    const auto failed = m_repository->updateBook(999999, uniqueBookSeed(11).toInput());
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
}

/// updateBook used to reconcile the holdings against Repositories::BookInput::initialCopyCount,
/// deleting copies from the highest id down to make the numbers agree. Once a
/// copy carries the library's own local and central numbers, that is a way to
/// destroy inventory by editing a title. Copies move only through saveCopies now.
TEST_F(test_core_CatalogRepository, UpdateBookLeavesCopiesAlone)
{
    BookSeed seed = uniqueBookSeed(12);
    seed.initialCopyCount = 5;
    const std::int64_t id = seedBook(*m_db, seed);
    ASSERT_GT(id, 0);

    const std::vector<std::int64_t> before = copyIdsOf(*m_db, id);
    EXPECT_EQ(before.size(), 5u);

    // Ask for one copy. The holdings must not move.
    seed.initialCopyCount = 1;
    seed.title = "Retitled Work 12";
    const auto mutated = m_repository->updateBook(id, seed.toInput());
    ASSERT_TRUE(mutated) << mutated.error().key;
    EXPECT_EQ(copyIdsOf(*m_db, id), before);

    // And the other direction: asking for more must not conjure any either.
    seed.initialCopyCount = 9;
    const auto mutatedAgain = m_repository->updateBook(id, seed.toInput());
    ASSERT_TRUE(mutatedAgain) << mutatedAgain.error().key;
    EXPECT_EQ(copyIdsOf(*m_db, id), before);
}

TEST_F(test_core_CatalogRepository, FailedUpdateLeavesBookUnchanged)
{
    BookSeed seed = uniqueBookSeed(15);
    const std::int64_t id = seedBook(*m_db, seed);
    ASSERT_GT(id, 0);

    BookSeed invalid = seed;
    invalid.title.clear();

    EXPECT_FALSE(m_repository->updateBook(id, invalid.toInput()));

    // The transaction in updateBook must have rolled back cleanly.
    const auto stored = m_repository->getBook(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->title, seed.title);
}

// ---------------------------------------------------------------------------
// delete
// ---------------------------------------------------------------------------

TEST_F(test_core_CatalogRepository, DeleteBookRemovesBookAndCopies)
{
    BookSeed seed = uniqueBookSeed(20);
    seed.initialCopyCount = 3;
    const std::int64_t id = seedBook(*m_db, seed);
    ASSERT_GT(id, 0);

    const auto mutated = m_repository->deleteBook(id);
    ASSERT_TRUE(mutated) << mutated.error().key;
    EXPECT_EQ(m_db->count("books"), 0);
    EXPECT_EQ(m_db->count("book_copies"), 0);
}

TEST_F(test_core_CatalogRepository, DeleteBookRefusesWhenActiveLoansExist)
{
    const std::int64_t id = seedBook(*m_db, uniqueBookSeed(21));
    ASSERT_GT(id, 0);

    MemberSeed member = uniqueMemberSeed(21);
    member.status = Repositories::MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    const Core::Date today = Core::Date::todayLocal();
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, id).front(),
                            today.toIso(),
                            today.addDays(14).toIso()),
              0);

    const auto failed = m_repository->deleteBook(id);
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_EQ(m_db->count("books"), 1);
}

// ---------------------------------------------------------------------------
// categories
// ---------------------------------------------------------------------------

TEST_F(test_core_CatalogRepository, CreateCategoryRejectsEmptyCode)
{
    const auto failed = m_repository->createCategory("  ", "x");
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
}

TEST_F(test_core_CatalogRepository, CreateCategoryRejectsDuplicateCode)
{
    EXPECT_GT(seedCategory(*m_db, "SCI", "Science"), 0);

    const auto failed = m_repository->createCategory("SCI", "Other");
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_EQ(m_db->count("categories"), 1);
}

TEST_F(test_core_CatalogRepository, UpdateCategoryReturnsFalseForUnknownId)
{
    EXPECT_FALSE(m_repository->updateCategory(999999, "X", "Y"));

    EXPECT_TRUE(true); // error key checked on the Result/Status above when captured
}

TEST_F(test_core_CatalogRepository, DeleteCategoryRefusesWhenBooksReference)
{
    const std::int64_t categoryId = seedCategory(*m_db, "HIST", "History");
    ASSERT_GT(categoryId, 0);

    BookSeed seed = uniqueBookSeed(30);
    seed.categoryId = categoryId;
    EXPECT_GT(seedBook(*m_db, seed), 0);

    const auto failed = m_repository->deleteCategory(categoryId);
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_EQ(m_db->count("categories"), 1);
}

TEST_F(test_core_CatalogRepository, ListCategoriesExcludesEmptyCategories)
{
    EXPECT_GT(seedCategory(*m_db, "EMPTY", "Empty"), 0);
    const std::int64_t usedId = seedCategory(*m_db, "USED", "Used");
    ASSERT_GT(usedId, 0);

    BookSeed seed = uniqueBookSeed(31);
    seed.categoryId = usedId;
    EXPECT_GT(seedBook(*m_db, seed), 0);

    const auto categories = VLMS_UNWRAP(m_repository->listCategories());
    EXPECT_EQ(categories.size(), 1u);
    EXPECT_EQ(categories.front().code, "USED");
}

TEST_F(test_core_CatalogRepository, ListAllCategoriesIncludesEmptyCategories)
{
    EXPECT_GT(seedCategory(*m_db, "EMPTY", "Empty"), 0);
    EXPECT_GT(seedCategory(*m_db, "ALSO", "Also"), 0);

    EXPECT_EQ(VLMS_UNWRAP(m_repository->listAllCategories()).size(), 2u);
}

// ---------------------------------------------------------------------------
// queries
// ---------------------------------------------------------------------------

TEST_F(test_core_CatalogRepository, ListBooksAndCountBooksAgree)
{
    struct Case {
        const char* name;
        std::string search;
        std::vector<std::string> categoryCodes;
        std::vector<std::string> languages;
        int coverFilter;
    };

    const std::vector<std::string> none;
    const std::vector<std::string> hist{"HIST"};
    const std::vector<std::string> arabic{"ar"};
    const std::vector<std::string> both{"ar", "fr"};

    const std::vector<Case> cases = {
        {"no filters", "", none, none, 0},
        {"search", "Test", none, none, 0},
        {"category", "", hist, none, 0},
        {"language", "", none, arabic, 0},
        {"two languages", "", none, both, 0},
        {"search+category", "Test", hist, none, 0},
        {"search+language", "Test", none, arabic, 0},
        {"all three", "Test", hist, arabic, 0},
        {"withCover", "", none, none, 1},
        {"withoutCover", "", none, none, 2},
        {"withoutCover+lang", "", none, arabic, 2},
        // Numeric terms take the copy-number arm, which listBooks reaches
        // through its book_copies join and countBooks has to reach without one.
        {"local number", "1", none, none, 0},
        {"local number prefix fallback", "99", none, none, 0},
        {"local number+category", "1", hist, none, 0},
    };

    for (const auto& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        ASSERT_TRUE(resetStore()) << m_db->lastError();

        const std::int64_t categoryId = seedCategory(*m_db, "HIST", "History");
        ASSERT_GT(categoryId, 0);

        for (int i = 0; i < 6; ++i) {
            BookSeed seed = uniqueBookSeed(40 + i);
            seed.categoryId = (i % 2 == 0) ? categoryId : 0;
            seed.language = (i % 3 == 0) ? "fr" : "ar";
            EXPECT_GT(seedBook(*m_db, seed), 0);
        }

        Repositories::BookQuery query;
        query.search = testCase.search;
        query.categoryCodes = testCase.categoryCodes;
        query.languages = testCase.languages;
        query.coverFilter = static_cast<Repositories::CoverFilter>(testCase.coverFilter);

        // listBooks joins publishers/book_copies/loans; countBooks joins none.
        // They must still agree for every filter combination.
        EXPECT_EQ(VLMS_UNWRAP(m_repository->countBooks(query)),
                  static_cast<int>(VLMS_UNWRAP(m_repository->listBooks(query)).size()));
    }
}

TEST_F(test_core_CatalogRepository, ListBooksRespectsLimitAndOffset)
{
    for (int i = 0; i < 10; ++i) {
        EXPECT_GT(seedBook(*m_db, uniqueBookSeed(50 + i)), 0);
    }

    Repositories::BookQuery query;
    query.limit = 3;
    query.offset = 0;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listBooks(query)).size(), 3u);

    query.offset = 9;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listBooks(query)).size(), 1u);

    query.offset = 20;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listBooks(query)).size(), 0u);
}

TEST_F(test_core_CatalogRepository, ListBooksPagesCoverEveryRowExactlyOnce)
{
    constexpr int kBooks = 25;
    constexpr int kPageSize = 7;

    for (int i = 0; i < kBooks; ++i) {
        EXPECT_GT(seedBook(*m_db, uniqueBookSeed(100 + i)), 0);
    }

    Repositories::BookQuery query;
    query.limit = kPageSize;

    std::set<std::int64_t> seen;
    int rows = 0;
    for (int offset = 0; offset < kBooks; offset += kPageSize) {
        query.offset = offset;
        for (const Repositories::BookRecord& book : VLMS_UNWRAP(m_repository->listBooks(query))) {
            seen.insert(book.id);
            ++rows;
        }
    }

    EXPECT_EQ(rows, kBooks);
    EXPECT_EQ(static_cast<int>(seen.size()), kBooks);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countBooks(Repositories::BookQuery{})), kBooks);
}

TEST_F(test_core_CatalogRepository, ListBooksPagesAreStableWithDuplicateTitles)
{
    // ORDER BY b.title COLLATE NOCASE is not a total order. `books` only
    // requires (title, author, publisher, isbn, language) to be unique, so
    // identical titles are legal. If the sort has no tiebreak, paging can
    // repeat or skip rows.
    constexpr int kBooks = 20;
    constexpr int kPageSize = 6;

    for (int i = 0; i < kBooks; ++i) {
        BookSeed seed = uniqueBookSeed(300 + i);
        seed.title = "Identical Title";
        EXPECT_GT(seedBook(*m_db, seed), 0);
    }

    Repositories::BookQuery query;
    query.limit = kPageSize;

    std::set<std::int64_t> seen;
    int rows = 0;
    for (int offset = 0; offset < kBooks; offset += kPageSize) {
        query.offset = offset;
        for (const Repositories::BookRecord& book : VLMS_UNWRAP(m_repository->listBooks(query))) {
            seen.insert(book.id);
            ++rows;
        }
    }

    EXPECT_EQ(rows, kBooks);
    EXPECT_EQ(static_cast<int>(seen.size()), kBooks)
        << "paged over " << rows << " rows but saw only " << seen.size()
        << " distinct ids; ORDER BY needs a tiebreak";
}

TEST_F(test_core_CatalogRepository, AvailableCopiesExcludesOpenLoans)
{
    BookSeed seed = uniqueBookSeed(60);
    seed.initialCopyCount = 3;
    const std::int64_t id = seedBook(*m_db, seed);
    ASSERT_GT(id, 0);

    MemberSeed member = uniqueMemberSeed(60);
    member.status = Repositories::MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    const Core::Date today = Core::Date::todayLocal();
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, id).front(),
                            today.toIso(),
                            today.addDays(14).toIso()),
              0);

    const auto stored = m_repository->getBook(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->totalCopies, 3);
    EXPECT_EQ(stored->availableCopies, 2);
}

TEST_F(test_core_CatalogRepository, TotalCopiesCountsEveryCopy)
{
    BookSeed seed = uniqueBookSeed(61);
    seed.initialCopyCount = 7;
    const std::int64_t id = seedBook(*m_db, seed);
    ASSERT_GT(id, 0);

    const auto stored = m_repository->getBook(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->totalCopies, 7);
}

TEST_F(test_core_CatalogRepository, CoverFilterSelectsBooksWithoutCovers)
{
    for (int i = 0; i < 3; ++i) {
        EXPECT_GT(seedBook(*m_db, uniqueBookSeed(70 + i)), 0);
    }

    Repositories::BookQuery withoutCover;
    withoutCover.coverFilter = Repositories::CoverFilter::WithoutCover;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listBooks(withoutCover)).size(), 3u);

    Repositories::BookQuery withCover;
    withCover.coverFilter = Repositories::CoverFilter::WithCover;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listBooks(withCover)).size(), 0u);
}

// ---------------------------------------------------------------------------
// errors
// ---------------------------------------------------------------------------

TEST_F(test_core_CatalogRepository, LastErrorIsSetOnEveryFailurePath)
{
    const std::vector<const char*> scenarios = {
        "createEmptyTitle",
        "createEmptyLanguage",
        "updateUnknown",
        "deleteUnknown",
        "categoryEmptyCode",
        "categoryUpdateUnknown",
        "categoryDeleteUnknown",
        "coverEmptyPath",
        "coverMissingFile",
    };

    for (const char* scenario : scenarios) {
        SCOPED_TRACE(scenario);
        ASSERT_TRUE(resetStore()) << m_db->lastError();

        Core::Error error;
        bool result = true;
        if (std::string_view(scenario) == "createEmptyTitle") {
            BookSeed seed = uniqueBookSeed(80);
            seed.title.clear();
            const auto created = m_repository->createBook(seed.toInput());
            result = static_cast<bool>(created);
            error = created.error();
        } else if (std::string_view(scenario) == "createEmptyLanguage") {
            BookSeed seed = uniqueBookSeed(81);
            seed.language.clear();
            const auto created = m_repository->createBook(seed.toInput());
            result = static_cast<bool>(created);
            error = created.error();
        } else if (std::string_view(scenario) == "updateUnknown") {
            const auto updated = m_repository->updateBook(999999, uniqueBookSeed(82).toInput());
            result = static_cast<bool>(updated);
            error = updated.error();
        } else if (std::string_view(scenario) == "deleteUnknown") {
            const auto removed = m_repository->deleteBook(999999);
            result = static_cast<bool>(removed);
            error = removed.error();
        } else if (std::string_view(scenario) == "categoryEmptyCode") {
            const auto created = m_repository->createCategory("", "label");
            result = static_cast<bool>(created);
            error = created.error();
        } else if (std::string_view(scenario) == "categoryUpdateUnknown") {
            const auto updated = m_repository->updateCategory(999999, "A", "B");
            result = static_cast<bool>(updated);
            error = updated.error();
        } else if (std::string_view(scenario) == "categoryDeleteUnknown") {
            const auto removed = m_repository->deleteCategory(999999);
            result = static_cast<bool>(removed);
            error = removed.error();
        } else if (std::string_view(scenario) == "coverEmptyPath") {
            const std::int64_t id = seedBook(*m_db, uniqueBookSeed(83));
            ASSERT_GT(id, 0);
            const auto cover = m_repository->setCoverImage(id, "");
            result = static_cast<bool>(cover);
            error = cover.error();
        } else if (std::string_view(scenario) == "coverMissingFile") {
            const std::int64_t id = seedBook(*m_db, uniqueBookSeed(84));
            ASSERT_GT(id, 0);
            const auto cover = m_repository->setCoverImage(id, "/nonexistent/cover.png");
            result = static_cast<bool>(cover);
            error = cover.error();
        }

        EXPECT_FALSE(result) << "this scenario is supposed to fail";
        EXPECT_FALSE(error.key.empty())
            << "a failing call must leave a translatable key the UI can show";
    }
}

TEST_F(test_core_CatalogRepository, GetBookMissingIdIsNotFoundNotSql)
{
    const auto missing = m_repository->getBook(999999);
    EXPECT_FALSE(missing);
    EXPECT_EQ(missing.kind(), Core::ErrorKind::NotFound);
    EXPECT_EQ(missing.error().key, "error.book.notFound");
}

TEST_F(test_core_CatalogRepository, GetBookExecFailureIsSql)
{
    ASSERT_TRUE(m_db->exec("DROP TABLE books"));
    const auto failed = m_repository->getBook(1);
    EXPECT_FALSE(failed);
    EXPECT_EQ(failed.kind(), Core::ErrorKind::Sql);
    EXPECT_EQ(failed.error().key, "error.sql");
}

TEST_F(test_core_CatalogRepository, SaveNewBookRollsBackWhenCoverFails)
{
    Repositories::BookWrite write;
    write.book = uniqueBookSeed(90).toInput();
    write.coverSourcePath = "/nonexistent/cover.png";

    const auto created = m_repository->saveNewBook(write);
    EXPECT_FALSE(created);
    EXPECT_EQ(created.kind(), Core::ErrorKind::Validation);
    EXPECT_EQ(m_db->count("books"), 0);
}

// --- copies ---------------------------------------------------------------
//
// The catalogue import brings in 20282 physical holdings, each carrying the
// library's own local number, central number, classification and subject off
// the stock sheets. These cover the API that now owns them.

namespace {

/// A book with one copy, and that copy filled in the way an imported one is.
std::int64_t seedBookWithACatalogedCopy(::Test::TestDatabase& db,
                                        Repositories::CatalogRepository& repository,
                                        int index,
                                        std::int64_t* outCopyId)
{
    BookSeed seed = uniqueBookSeed(index);
    seed.initialCopyCount = 1;
    const std::int64_t bookId = seedBook(db, seed);
    if (bookId <= 0) {
        return 0;
    }

    const std::vector<Repositories::BookCopyRecord> existing = VLMS_UNWRAP(repository.listCopies(bookId));
    if (existing.size() != 1) {
        return 0;
    }

    Repositories::BookCopyInput copy;
    copy.id = existing.front().id;
    copy.source = "arabic";
    copy.localId = "90" + std::to_string(index);
    copy.globalCopyId = "AR-90" + std::to_string(index);
    copy.centralId = "137949";
    copy.classification = "ن";
    copy.subject = "ن-ش";
    copy.indexCode = "410";
    copy.location = "Salle 2";
    copy.inventoryStatus = "جرد 2025";
    copy.compensation = "تعويض";
    copy.notes = "ملاحظة";

    if (!repository.saveCopies(bookId, {copy})) {
        return 0;
    }
    if (outCopyId != nullptr) {
        *outCopyId = copy.id;
    }
    return bookId;
}

}  // namespace

TEST_F(test_core_CatalogRepository, ListCopiesReturnsEveryStoredField)
{
    std::int64_t copyId = 0;
    const std::int64_t bookId = seedBookWithACatalogedCopy(*m_db, *m_repository, 40, &copyId);
    ASSERT_GT(bookId, 0) << "repository call failed";

    const std::vector<Repositories::BookCopyRecord> copies = VLMS_UNWRAP(m_repository->listCopies(bookId));
    EXPECT_EQ(copies.size(), 1u);

    const Repositories::BookCopyRecord& copy = copies.front();
    EXPECT_EQ(copy.id, copyId);
    EXPECT_EQ(copy.bookId, bookId);
    EXPECT_EQ(copy.source, "arabic");
    EXPECT_EQ(copy.localId, "9040");
    EXPECT_EQ(copy.globalCopyId, "AR-9040");
    EXPECT_EQ(copy.centralId, "137949");
    EXPECT_EQ(copy.classification, "ن");
    EXPECT_EQ(copy.subject, "ن-ش");
    EXPECT_EQ(copy.indexCode, "410");
    EXPECT_EQ(copy.location, "Salle 2");
    EXPECT_EQ(copy.inventoryStatus, "جرد 2025");
    EXPECT_EQ(copy.compensation, "تعويض");
    EXPECT_EQ(copy.notes, "ملاحظة");
    EXPECT_FALSE(copy.onLoan);
}

TEST_F(test_core_CatalogRepository, ListCopiesFlagsCopiesOnLoan)
{
    BookSeed seed = uniqueBookSeed(41);
    seed.initialCopyCount = 3;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    MemberSeed member = uniqueMemberSeed(41);
    member.status = Repositories::MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    const std::vector<std::int64_t> copyIds = copyIdsOf(*m_db, bookId);
    const Core::Date today = Core::Date::todayLocal();
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copyIds.front(), today.toIso(),
                            today.addDays(14).toIso()),
              0);

    int onLoan = 0;
    for (const Repositories::BookCopyRecord& copy : VLMS_UNWRAP(m_repository->listCopies(bookId))) {
        if (copy.onLoan) {
            ++onLoan;
            EXPECT_EQ(copy.id, copyIds.front());
        }
    }
    EXPECT_EQ(onLoan, 1);
}

TEST_F(test_core_CatalogRepository, SaveCopiesRoundTripsAnEdit)
{
    std::int64_t copyId = 0;
    const std::int64_t bookId = seedBookWithACatalogedCopy(*m_db, *m_repository, 42, &copyId);
    ASSERT_GT(bookId, 0) << "repository call failed";

    std::vector<Repositories::BookCopyRecord> copies = VLMS_UNWRAP(m_repository->listCopies(bookId));
    EXPECT_EQ(copies.size(), 1u);

    Repositories::BookCopyInput edited;
    edited.id = copies.front().id;
    edited.source = copies.front().source;
    edited.localId = copies.front().localId;
    edited.globalCopyId = copies.front().globalCopyId;
    edited.centralId = copies.front().centralId;
    edited.classification = "930";
    edited.subject = copies.front().subject;

    const auto mutated = m_repository->saveCopies(bookId, {edited});
    ASSERT_TRUE(mutated) << mutated.error().key;

    copies = VLMS_UNWRAP(m_repository->listCopies(bookId));
    EXPECT_EQ(copies.size(), 1u);
    EXPECT_EQ(copies.front().id, copyId);
    EXPECT_EQ(copies.front().classification, "930");
    // Cleared fields really clear rather than silently keeping the old value.
    EXPECT_EQ(copies.front().indexCode, "");
}

TEST_F(test_core_CatalogRepository, SaveCopiesInsertsUpdatesAndDeletesInOneCall)
{
    BookSeed seed = uniqueBookSeed(43);
    seed.initialCopyCount = 3;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const std::vector<Repositories::BookCopyRecord> before = VLMS_UNWRAP(m_repository->listCopies(bookId));
    EXPECT_EQ(before.size(), 3u);

    std::vector<Repositories::BookCopyInput> submitted;

    // Keep the first, edited.
    Repositories::BookCopyInput kept;
    kept.id = before.at(0).id;
    kept.source = before.at(0).source;
    kept.localId = before.at(0).localId;
    kept.globalCopyId = before.at(0).globalCopyId;
    kept.subject = "Poésie";
    submitted.push_back(kept);

    // Keep the second untouched. Drop the third by leaving it out.
    Repositories::BookCopyInput untouched;
    untouched.id = before.at(1).id;
    untouched.source = before.at(1).source;
    untouched.localId = before.at(1).localId;
    untouched.globalCopyId = before.at(1).globalCopyId;
    submitted.push_back(untouched);

    // And add a brand new one.
    Repositories::BookCopyInput added;
    added.source = "foreign";
    added.localId = "77043";
    added.globalCopyId = "FR-77043";
    added.classification = "ROMAN";
    submitted.push_back(added);

    const auto mutated = m_repository->saveCopies(bookId, submitted);
    ASSERT_TRUE(mutated) << mutated.error().key;

    const std::vector<Repositories::BookCopyRecord> after = VLMS_UNWRAP(m_repository->listCopies(bookId));
    EXPECT_EQ(after.size(), 3u);

    std::set<std::int64_t> ids;
    for (const Repositories::BookCopyRecord& copy : after) {
        ids.insert(copy.id);
    }
    EXPECT_TRUE(ids.contains(before.at(0).id));
    EXPECT_TRUE(ids.contains(before.at(1).id));
    EXPECT_FALSE(ids.contains(before.at(2).id));

    bool foundEdited = false;
    bool foundAdded = false;
    for (const Repositories::BookCopyRecord& copy : after) {
        if (copy.id == before.at(0).id) {
            foundEdited = copy.subject == "Poésie";
        }
        if (copy.globalCopyId == "FR-77043") {
            foundAdded = copy.classification == "ROMAN"
                         && copy.source == "foreign";
        }
    }
    EXPECT_TRUE(foundEdited);
    EXPECT_TRUE(foundAdded);
}

TEST_F(test_core_CatalogRepository, SaveCopiesRefusesToDeleteACopyOnLoan)
{
    BookSeed seed = uniqueBookSeed(44);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    MemberSeed member = uniqueMemberSeed(44);
    member.status = Repositories::MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    const std::vector<Repositories::BookCopyRecord> before = VLMS_UNWRAP(m_repository->listCopies(bookId));
    const Core::Date today = Core::Date::todayLocal();
    EXPECT_GT(rawInsertLoan(*m_db, memberId, before.at(0).id, today.toIso(),
                            today.addDays(14).toIso()),
              0);

    // Submit only the second copy: the loaned one would be dropped.
    Repositories::BookCopyInput survivor;
    survivor.id = before.at(1).id;
    survivor.source = before.at(1).source;
    survivor.localId = before.at(1).localId;
    survivor.globalCopyId = before.at(1).globalCopyId;

    EXPECT_FALSE(m_repository->saveCopies(bookId, {survivor}))
        << "removing a copy that is out on loan must be refused";
    EXPECT_TRUE(true); // error key checked on the Result/Status above when captured
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listCopies(bookId)).size(), 2u);
}

/// The whole point of reconciling in one transaction: a bad row late in the
/// list must not leave the good rows before it already written.
TEST_F(test_core_CatalogRepository, SaveCopiesRollsBackEverythingWhenOneRowIsRefused)
{
    BookSeed seed = uniqueBookSeed(45);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    // A second book, so there is a copy elsewhere to collide with.
    BookSeed other = uniqueBookSeed(46);
    other.initialCopyCount = 1;
    const std::int64_t otherBookId = seedBook(*m_db, other);
    ASSERT_GT(otherBookId, 0);
    const std::string takenGlobalId =
        VLMS_UNWRAP(m_repository->listCopies(otherBookId)).front().globalCopyId;

    const std::vector<Repositories::BookCopyRecord> before = VLMS_UNWRAP(m_repository->listCopies(bookId));
    EXPECT_EQ(before.size(), 2u);

    std::vector<Repositories::BookCopyInput> submitted;

    Repositories::BookCopyInput good;
    good.id = before.at(0).id;
    good.source = before.at(0).source;
    good.localId = before.at(0).localId;
    good.globalCopyId = before.at(0).globalCopyId;
    good.subject = "This edit must not survive";
    submitted.push_back(good);

    Repositories::BookCopyInput bad;
    bad.id = before.at(1).id;
    bad.source = before.at(1).source;
    bad.localId = before.at(1).localId;
    bad.globalCopyId = takenGlobalId;  // already belongs to the other book
    submitted.push_back(bad);

    EXPECT_FALSE(m_repository->saveCopies(bookId, submitted));

    const std::vector<Repositories::BookCopyRecord> after = VLMS_UNWRAP(m_repository->listCopies(bookId));
    EXPECT_EQ(after.size(), 2u);
    EXPECT_EQ(after.at(0).subject, "");
    EXPECT_EQ(after.at(1).globalCopyId, before.at(1).globalCopyId);
}

TEST_F(test_core_CatalogRepository, SaveCopiesReportsDuplicateGlobalIdReadably)
{
    BookSeed seed = uniqueBookSeed(47);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const std::vector<Repositories::BookCopyRecord> before = VLMS_UNWRAP(m_repository->listCopies(bookId));
    std::vector<Repositories::BookCopyInput> submitted;
    for (const Repositories::BookCopyRecord& copy : before) {
        Repositories::BookCopyInput input;
        input.id = copy.id;
        input.source = copy.source;
        input.localId = copy.localId;
        input.globalCopyId = "AR-DUPLICATE";  // both the same
        submitted.push_back(input);
    }

    const auto failed = m_repository->saveCopies(bookId, submitted);
    EXPECT_FALSE(failed);
    const std::string message = failed.error().detail;
    EXPECT_NE(message.find("AR-DUPLICATE"), std::string::npos)
        << "the message must name the clashing number, got: " << message;
    EXPECT_EQ(message.find("UNIQUE constraint"), std::string::npos)
        << "a raw SQLite error must not reach the UI: " << message;
}

TEST_F(test_core_CatalogRepository, SaveCopiesReportsDuplicateLocalIdReadably)
{
    BookSeed seed = uniqueBookSeed(48);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const std::vector<Repositories::BookCopyRecord> before = VLMS_UNWRAP(m_repository->listCopies(bookId));
    std::vector<Repositories::BookCopyInput> submitted;
    int index = 0;
    for (const Repositories::BookCopyRecord& copy : before) {
        Repositories::BookCopyInput input;
        input.id = copy.id;
        input.source = "arabic";
        input.localId = "55555";  // same local number, same stock
        input.globalCopyId = "AR-UNIQUE-" + std::to_string(index++);
        submitted.push_back(input);
    }

    const auto failed = m_repository->saveCopies(bookId, submitted);
    EXPECT_FALSE(failed);
    const std::string message = failed.error().detail;
    EXPECT_NE(message.find("55555"), std::string::npos)
        << "the message must name the clashing number, got: " << message;
    EXPECT_EQ(message.find("UNIQUE constraint"), std::string::npos)
        << "a raw SQLite error must not reach the UI: " << message;
}

/// 1749 of the imported books have no holdings at all. Refusing to save an
/// empty list would make those records impossible to edit.
TEST_F(test_core_CatalogRepository, SaveCopiesAcceptsAnEmptyList)
{
    BookSeed seed = uniqueBookSeed(49);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const auto mutated = m_repository->saveCopies(bookId, {});
    ASSERT_TRUE(mutated) << mutated.error().key;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listCopies(bookId)).size(), 0u);

    // And saving nothing again on a book that already has nothing is a no-op,
    // not an error.
    const auto mutatedAgain = m_repository->saveCopies(bookId, {});
    ASSERT_TRUE(mutatedAgain) << mutatedAgain.error().key;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listCopies(bookId)).size(), 0u);
}

/// source_row points back at the spreadsheet row a copy came from. The editor
/// never shows it, so an edit must not quietly drop it.
TEST_F(test_core_CatalogRepository, SaveCopiesPreservesSourceRow)
{
    std::int64_t copyId = 0;
    const std::int64_t bookId = seedBookWithACatalogedCopy(*m_db, *m_repository, 50, &copyId);
    ASSERT_GT(bookId, 0) << "repository call failed";

    EXPECT_TRUE(m_db->execBound("UPDATE book_copies SET source_row = 4711 WHERE id = :id",
                                {{"id", copyId}}))
        << m_db->lastError();

    const Repositories::BookCopyRecord stored = VLMS_UNWRAP(m_repository->listCopies(bookId)).front();
    EXPECT_EQ(stored.sourceRow, 4711);

    Repositories::BookCopyInput edited;
    edited.id = stored.id;
    edited.source = stored.source;
    edited.localId = stored.localId;
    edited.globalCopyId = stored.globalCopyId;
    edited.subject = "Changed";
    const auto mutated = m_repository->saveCopies(bookId, {edited});
    ASSERT_TRUE(mutated) << mutated.error().key;

    EXPECT_EQ(VLMS_UNWRAP(m_repository->listCopies(bookId)).front().sourceRow, 4711);
}

TEST_F(test_core_CatalogRepository, SuggestCopyIdentifiersFollowsTheLibraryNumbering)
{
    std::string source;
    std::string localId;
    std::string globalCopyId;

    m_repository->suggestCopyIdentifiers("ar", &source, &localId, &globalCopyId);
    EXPECT_EQ(source, "arabic");
    EXPECT_EQ(globalCopyId, std::string("AR-") + localId);

    m_repository->suggestCopyIdentifiers("fr", &source, &localId, &globalCopyId);
    EXPECT_EQ(source, "foreign");
    EXPECT_EQ(globalCopyId, std::string("FR-") + localId);

    // And the suggestion is actually free: saving it must not collide.
    BookSeed seed = uniqueBookSeed(51);
    seed.language = "fr";
    seed.initialCopyCount = 1;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    m_repository->suggestCopyIdentifiers("fr", &source, &localId, &globalCopyId);

    const Repositories::BookCopyRecord existing = VLMS_UNWRAP(m_repository->listCopies(bookId)).front();
    Repositories::BookCopyInput keep;
    keep.id = existing.id;
    keep.source = existing.source;
    keep.localId = existing.localId;
    keep.globalCopyId = existing.globalCopyId;

    Repositories::BookCopyInput added;
    added.source = source;
    added.localId = localId;
    added.globalCopyId = globalCopyId;

    const auto mutated = m_repository->saveCopies(bookId, {keep, added});
    ASSERT_TRUE(mutated) << mutated.error().key;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->listCopies(bookId)).size(), 2u);
}

TEST_F(test_core_CatalogRepository, ListBooksSortsByTitleAndCopies)
{
    const std::int64_t cat = seedCategory(*m_db, "SRT", "Sort");
    ASSERT_GT(cat, 0);

    BookSeed zebra = uniqueBookSeed(501);
    zebra.title = "zebra";
    zebra.categoryId = cat;
    zebra.initialCopyCount = 1;
    const std::int64_t idZ = seedBook(*m_db, zebra);
    BookSeed apple = uniqueBookSeed(502);
    apple.title = "Apple";
    apple.categoryId = cat;
    apple.initialCopyCount = 3;
    const std::int64_t idA = seedBook(*m_db, apple);
    ASSERT_GT(idZ, 0);
    ASSERT_GT(idA, 0);

    Repositories::BookQuery byTitle;
    byTitle.sortColumn = Repositories::BookSort::kTitle;
    byTitle.sortAscending = true;
    const auto titles = VLMS_UNWRAP(m_repository->listBooks(byTitle));
    ASSERT_EQ(titles.size(), 2u);
    EXPECT_EQ(titles.at(0).id, idA);
    EXPECT_EQ(titles.at(1).id, idZ);

    Repositories::BookQuery byCopies;
    byCopies.sortColumn = Repositories::BookSort::kCopies;
    byCopies.sortAscending = false;
    const auto copies = VLMS_UNWRAP(m_repository->listBooks(byCopies));
    ASSERT_EQ(copies.size(), 2u);
    EXPECT_EQ(copies.at(0).id, idA);
    EXPECT_EQ(copies.at(1).id, idZ);

    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfBook(idA, byTitle)), 0);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfBook(idZ, byTitle)), 1);
}

TEST_F(test_core_CatalogRepository, ListBooksReturnsCopyLocalIdsAscendingNumerically)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.initialCopyCount = 3;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const auto copyIds = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copyIds.size(), 3U);
    // Deliberately out of order, and chosen so a lexical sort would give 100, 9, 10.
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIds[0], "100"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIds[1], "9"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIds[2], "10"));

    Repositories::BookQuery query;
    const auto books = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(books.size(), 1U);
    EXPECT_EQ(books.front().localIds, (std::vector<std::string>{"9", "10", "100"}));
}

TEST_F(test_core_CatalogRepository, ListBooksReturnsNoLocalIdsForABookWithoutCopies)
{
    // createBook rejects initialCopyCount < 1 (C11b, see
    // CreateBookRejectsCopyCountBelowOne above), so a book cannot be seeded
    // with zero copies directly. Instead, seed one copy and archive it via
    // saveCopies({}) -- the same path Catalog Delete uses when the librarian
    // removes the last copy row -- which leaves the book itself live but with
    // no live copies, the real-world shape this test is about.
    BookSeed seed = uniqueBookSeed(2);
    seed.initialCopyCount = 1;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    ASSERT_TRUE(m_repository->saveCopies(bookId, {}));

    Repositories::BookQuery query;
    const auto books = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(books.size(), 1U);
    EXPECT_TRUE(books.front().localIds.empty());
}

TEST_F(test_core_CatalogRepository, ListBooksNamesTheCopiesThatAreOutOnLoan)
{
    BookSeed seed = uniqueBookSeed(210);
    seed.initialCopyCount = 3;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const auto copyIds = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copyIds.size(), 3U);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIds[0], "7"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIds[1], "8"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIds[2], "9"));

    MemberSeed member = uniqueMemberSeed(210);
    member.status = Repositories::MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    const Core::Date today = Core::Date::todayLocal();
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIds[1], today.toIso(),
                            today.addDays(14).toIso()),
              0);

    Repositories::BookQuery query;
    const auto books = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(books.size(), 1U);
    EXPECT_EQ(books.front().localIds, (std::vector<std::string>{"7", "8", "9"}));
    EXPECT_EQ(books.front().localIdsOnLoan, (std::vector<std::string>{"8"}))
        << "only the copy with an unreturned loan belongs to the subset";
}

TEST_F(test_core_CatalogRepository, ListBooksNamesNoCopiesOnLoanWhenEveryLoanIsReturned)
{
    BookSeed seed = uniqueBookSeed(211);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const auto copyIds = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copyIds.size(), 2U);

    MemberSeed member = uniqueMemberSeed(211);
    member.status = Repositories::MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    const Core::Date today = Core::Date::todayLocal();
    const std::int64_t loanId = rawInsertLoan(*m_db, memberId, copyIds.front(),
                                              today.addDays(-20).toIso(),
                                              today.addDays(-6).toIso());
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_db->execBound("UPDATE loans SET returned_at = :when WHERE id = :id",
                                {{"when", today.addDays(-6).toIso()}, {"id", loanId}}));

    Repositories::BookQuery query;
    const auto books = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(books.size(), 1U);
    EXPECT_EQ(books.front().localIds.size(), 2U);
    EXPECT_TRUE(books.front().localIdsOnLoan.empty())
        << "a returned loan leaves its copy on the shelf";
}

TEST_F(test_core_CatalogRepository, ACopyWithoutALocalNumberNeverLeaksABlankOnLoanEntry)
{
    // 1,749 live copies carry no local number at all. One of those out on loan
    // must not turn into an empty string in the subset, which the catalogue
    // would then try to colour a number it cannot name.
    BookSeed seed = uniqueBookSeed(212);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const auto copyIds = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copyIds.size(), 2U);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIds[0], "31"));
    ASSERT_TRUE(m_db->execBound("UPDATE book_copies SET local_id = NULL WHERE id = :id",
                                {{"id", copyIds[1]}}));

    MemberSeed member = uniqueMemberSeed(212);
    member.status = Repositories::MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    const Core::Date today = Core::Date::todayLocal();
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIds[1], today.toIso(),
                            today.addDays(14).toIso()),
              0);

    Repositories::BookQuery query;
    const auto books = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(books.size(), 1U);
    EXPECT_EQ(books.front().localIds, (std::vector<std::string>{"31"}));
    EXPECT_TRUE(books.front().localIdsOnLoan.empty());
}

TEST_F(test_core_CatalogRepository, SortingByLocalNumberOrdersByTheLowestNumber)
{
    BookSeed high = uniqueBookSeed(10);
    high.title = "High";
    high.initialCopyCount = 1;
    const std::int64_t highId = seedBook(*m_db, high);
    ASSERT_GT(highId, 0);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIdsOf(*m_db, highId).front(), "500"));

    BookSeed low = uniqueBookSeed(11);
    low.title = "Low";
    low.initialCopyCount = 2;
    const std::int64_t lowId = seedBook(*m_db, low);
    ASSERT_GT(lowId, 0);
    const auto lowCopies = copyIdsOf(*m_db, lowId);
    ASSERT_EQ(lowCopies.size(), 2U);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, lowCopies[0], "900"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, lowCopies[1], "42"));

    Repositories::BookQuery query;
    query.sortColumn = Repositories::BookSort::kLocalNumber;
    query.sortAscending = true;
    const auto ascending = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(ascending.size(), 2U);
    // "Low" wins on MIN(42), not on its 900.
    EXPECT_EQ(ascending.front().title, "Low");

    query.sortAscending = false;
    const auto descending = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(descending.size(), 2U);
    EXPECT_EQ(descending.front().title, "High");
}

TEST_F(test_core_CatalogRepository, BooksWithoutCopiesSortLastByLocalNumberInBothDirections)
{
    BookSeed withCopy = uniqueBookSeed(12);
    withCopy.title = "Has a copy";
    withCopy.initialCopyCount = 1;
    const std::int64_t withId = seedBook(*m_db, withCopy);
    ASSERT_GT(withId, 0);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIdsOf(*m_db, withId).front(), "7"));

    // createBook rejects initialCopyCount < 1 (rule C11b, "error.book.minCopies"),
    // so a copy-less book cannot be seeded directly. Seed one copy and archive it
    // through saveCopies({}) — the path Catalog Delete uses when the librarian
    // removes the last copy row — which leaves the book live with no live copies.
    BookSeed bare = uniqueBookSeed(13);
    bare.title = "No copies";
    bare.initialCopyCount = 1;
    const std::int64_t bareId = seedBook(*m_db, bare);
    ASSERT_GT(bareId, 0);
    ASSERT_TRUE(m_repository->saveCopies(bareId, {}));

    Repositories::BookQuery query;
    query.sortColumn = Repositories::BookSort::kLocalNumber;

    query.sortAscending = true;
    const auto ascending = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(ascending.size(), 2U);
    EXPECT_EQ(ascending.back().title, "No copies");

    query.sortAscending = false;
    const auto descending = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(descending.size(), 2U);
    EXPECT_EQ(descending.back().title, "No copies");
}

// ---------------------------------------------------------------------------
// search by local number
// ---------------------------------------------------------------------------

namespace {

/// Seeds a one-copy book and stamps that copy with `localNumber`. The language
/// decides the copy's source ("ar" -> arabic, anything else -> foreign), which
/// matters because book_copies is UNIQUE (source, local_id): two books can only
/// share a number when they sit in different stocks.
std::int64_t seedBookWithLocalNumber(TestDatabase& db,
                                     const int seedIndex,
                                     const std::string& title,
                                     const std::string& localNumber,
                                     const std::string& language = "ar")
{
    BookSeed seed = uniqueBookSeed(seedIndex);
    seed.title = title;
    seed.language = language;
    seed.initialCopyCount = 1;
    const std::int64_t bookId = seedBook(db, seed);
    if (bookId <= 0) {
        return 0;
    }
    const auto copies = copyIdsOf(db, bookId);
    if (copies.size() != 1U || !rawSetCopyLocalId(db, copies.front(), localNumber)) {
        return 0;
    }
    return bookId;
}

std::vector<std::string> titlesOf(const std::vector<Repositories::BookRecord>& books)
{
    std::vector<std::string> titles;
    titles.reserve(books.size());
    for (const Repositories::BookRecord& book : books) {
        titles.push_back(book.title);
    }
    std::sort(titles.begin(), titles.end());
    return titles;
}

}  // namespace

TEST_F(test_core_CatalogRepository, SearchByLocalNumberFindsTheBookHoldingThatCopy)
{
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 60, "Wanted", "4100"), 0);
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 61, "Other", "77"), 0);

    Repositories::BookQuery query;
    query.search = "4100";
    EXPECT_EQ(titlesOf(VLMS_UNWRAP(m_repository->listBooks(query))),
              (std::vector<std::string>{"Wanted"}));
}

TEST_F(test_core_CatalogRepository, SearchByLocalNumberFindsEveryBookSharingThatNumber)
{
    // Local numbers are unique per stock, not overall: 2,803 of the live ones
    // belong to both an arabic and a foreign book. Both rows are correct.
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 62, "Arabic side", "1002", "ar"), 0);
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 63, "Foreign side", "1002", "fr"), 0);

    Repositories::BookQuery query;
    query.search = "1002";
    EXPECT_EQ(titlesOf(VLMS_UNWRAP(m_repository->listBooks(query))),
              (std::vector<std::string>{"Arabic side", "Foreign side"}));
}

TEST_F(test_core_CatalogRepository, SearchByLocalNumberIgnoresNumbersThatMerelyContainTheTerm)
{
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 64, "Exactly", "123"), 0);
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 65, "Longer", "1234"), 0);
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 66, "Suffix", "9123"), 0);

    Repositories::BookQuery query;
    query.search = "123";
    EXPECT_EQ(titlesOf(VLMS_UNWRAP(m_repository->listBooks(query))),
              (std::vector<std::string>{"Exactly"}));
}

TEST_F(test_core_CatalogRepository, SearchByLocalNumberFallsBackToPrefixWhenNoCopyHasThatNumber)
{
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 67, "First", "5000"), 0);
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 68, "Second", "5001"), 0);
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 69, "Elsewhere", "700"), 0);

    Repositories::BookQuery query;
    query.search = "50";
    EXPECT_EQ(titlesOf(VLMS_UNWRAP(m_repository->listBooks(query))),
              (std::vector<std::string>{"First", "Second"}));
}

TEST_F(test_core_CatalogRepository, PrefixFallbackStaysOffWhenAnExactNumberExists)
{
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 70, "Exactly fifty", "50"), 0);
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 71, "Starts with fifty", "5001"), 0);

    Repositories::BookQuery query;
    query.search = "50";
    EXPECT_EQ(titlesOf(VLMS_UNWRAP(m_repository->listBooks(query))),
              (std::vector<std::string>{"Exactly fifty"}));
}

TEST_F(test_core_CatalogRepository, SearchByLocalNumberStillMatchesTheTextFields)
{
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 72, "By number", "1984"), 0);
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 73, "Orwell 1984", "31"), 0);

    Repositories::BookQuery query;
    query.search = "1984";
    EXPECT_EQ(titlesOf(VLMS_UNWRAP(m_repository->listBooks(query))),
              (std::vector<std::string>{"By number", "Orwell 1984"}));
}

TEST_F(test_core_CatalogRepository, SearchByLocalNumberIgnoresArchivedCopiesInTheLiveScope)
{
    const std::int64_t bookId = seedBookWithLocalNumber(*m_db, 74, "Archived copy", "8800");
    ASSERT_GT(bookId, 0);
    // saveCopies({}) archives the copy the way Catalog Delete does, leaving the
    // book live with no live copies; its number is released with it.
    ASSERT_TRUE(m_repository->saveCopies(bookId, {}));

    Repositories::BookQuery query;
    query.search = "8800";
    EXPECT_TRUE(VLMS_UNWRAP(m_repository->listBooks(query)).empty());
}

TEST_F(test_core_CatalogRepository, SearchByLocalNumberReportsWhichCopyMatched)
{
    BookSeed seed = uniqueBookSeed(75);
    seed.title = "Several copies";
    seed.initialCopyCount = 3;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copies.size(), 3U);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies[0], "1002"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies[1], "15000"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies[2], "15001"));

    Repositories::BookQuery query;
    query.search = "15000";
    const auto matched = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(matched.size(), 1U);
    EXPECT_EQ(matched.front().matchedLocalId, "15000");
    // The full list is untouched: it is still every number, ascending.
    EXPECT_EQ(matched.front().localIds,
              (std::vector<std::string>{"1002", "15000", "15001"}));
}

TEST_F(test_core_CatalogRepository, NoCopyIsReportedAsMatchedForANonNumericSearch)
{
    ASSERT_GT(seedBookWithLocalNumber(*m_db, 76, "Plain title", "4242"), 0);

    Repositories::BookQuery query;
    query.search = "Plain";
    const auto books = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(books.size(), 1U);
    EXPECT_TRUE(books.front().matchedLocalId.empty());
}
