#include "TestDatabase.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using VLMS::Date;
using VLMS::DateTime;
using VLMS::ScopedClock;
using namespace VLMS::Test;

namespace {

BookInput inputOf(const BookRecord& book)
{
    BookInput input;
    input.title = book.title;
    input.authorName = book.authorName;
    input.publisherName = book.publisherName;
    input.categoryId = book.categoryId;
    input.isbn = book.isbn;
    input.publicationDate = book.publicationDate;
    input.placeOfPublication = book.placeOfPublication;
    input.pages = book.pages;
    input.dimensions = book.dimensions;
    input.language = book.language;
    input.description = book.description;
    return input;
}

BookCopyInput inputOf(const BookCopyRecord& copy)
{
    BookCopyInput input;
    input.id = copy.id;
    input.globalCopyId = copy.globalCopyId;
    input.source = copy.source;
    input.localId = copy.localId;
    input.centralId = copy.centralId;
    input.classification = copy.classification;
    input.subject = copy.subject;
    input.notes = copy.notes;
    input.inventoryStatus = copy.inventoryStatus;
    input.compensation = copy.compensation;
    input.location = copy.location;
    input.indexCode = copy.indexCode;
    return input;
}

}  // namespace

class test_core_ArchiveCatalog : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository =
            std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_repository.reset();
        m_db.reset();
    }

    std::int64_t seedBookWithCopies(int index, int copies)
    {
        BookSeed seed = uniqueBookSeed(index);
        seed.initialCopyCount = copies;
        return seedBook(*m_db, seed);
    }

    /// Saves the book with its live copies minus `dropped`, plus `extra`, the
    /// way the editor does after the librarian removed that row.
    VLMS::Status saveWithout(std::int64_t bookId,
                                   std::int64_t dropped,
                                   const std::vector<BookCopyInput>& extra = {})
    {
        const auto book = m_repository->getBook(bookId);
        const auto copies = m_repository->listCopies(bookId);
        BookWrite write;
        write.book = inputOf(book.value());
        for (const BookCopyRecord& copy : copies.value()) {
            if (copy.id != dropped) {
                write.copies.push_back(inputOf(copy));
            }
        }
        for (const BookCopyInput& copy : extra) {
            write.copies.push_back(copy);
        }
        return m_repository->saveExistingBook(bookId, write);
    }

    std::string copyStamp(std::int64_t copyId)
    {
        const auto value =
            m_db->scalar("SELECT archived_at FROM book_copies WHERE id = " + std::to_string(copyId));
        return value.isNull() ? std::string() : value.toString();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_repository;
};

TEST_F(test_core_ArchiveCatalog, LiveListsHideArchivedBooksAndTheArchiveShowsOnlyThem)
{
    const std::int64_t kept = seedBookWithCopies(1, 1);
    const std::int64_t archived = seedBookWithCopies(2, 2);
    ASSERT_TRUE(m_repository->archiveBook(archived));

    const auto live = VLMS_UNWRAP(m_repository->listBooks({}));
    ASSERT_EQ(live.size(), 1u);
    EXPECT_EQ(live.front().id, kept);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countBooks({})), 1);

    BookQuery archive;
    archive.archive = ArchiveScope::Archived;
    const auto rows = VLMS_UNWRAP(m_repository->listBooks(archive));
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows.front().id, archived);
    EXPECT_EQ(rows.front().totalCopies, 2);
    EXPECT_FALSE(rows.front().archivedAt.empty());
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countBooks(archive)), 1);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfBook(archived, archive)), 0);
}

TEST_F(test_core_ArchiveCatalog, ArchiveBookStampsTheBookAndEveryLiveCopyAlike)
{
    const std::int64_t bookId = seedBookWithCopies(3, 2);
    const ScopedClock pinned(DateTime(Date(2026, 9, 19), 10, 0, 0));
    ASSERT_TRUE(m_repository->archiveBook(bookId));

    EXPECT_EQ(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId))
                  .toString(),
              "2026-09-19 10:00:00");
    for (const std::int64_t copyId : copyIdsOf(*m_db, bookId)) {
        EXPECT_EQ(copyStamp(copyId), "2026-09-19 10:00:00");
    }
}

TEST_F(test_core_ArchiveCatalog, ArchiveBookIsRefusedWhileACopyIsOnLoan)
{
    const std::int64_t bookId = seedBookWithCopies(4, 2);
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(1));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                            "2026-09-15"),
              0);

    const auto refused = m_repository->archiveBook(bookId);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.book.hasActiveLoans");
    EXPECT_TRUE(
        m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId)).isNull());
    for (const std::int64_t copyId : copyIdsOf(*m_db, bookId)) {
        EXPECT_TRUE(copyStamp(copyId).empty());
    }
}

TEST_F(test_core_ArchiveCatalog, ArchiveBookKeepsTheCoverFolder)
{
    const std::int64_t bookId = seedBookWithCopies(5, 1);
    const std::filesystem::path folder =
        std::filesystem::path(m_db->resourcesDirectory()) / "books" / std::to_string(bookId);
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "cover.jpg") << "jpeg";

    ASSERT_TRUE(m_repository->archiveBook(bookId));
    EXPECT_TRUE(std::filesystem::exists(folder / "cover.jpg"));
}

TEST_F(test_core_ArchiveCatalog, RemovingACopyRowArchivesItOnSave)
{
    const std::int64_t bookId = seedBookWithCopies(6, 2);
    const std::vector<std::int64_t> copies = copyIdsOf(*m_db, bookId);

    ASSERT_TRUE(saveWithout(bookId, copies.front()));
    EXPECT_FALSE(copyStamp(copies.front()).empty());
    EXPECT_TRUE(copyStamp(copies.back()).empty());
    EXPECT_EQ(m_repository->getBook(bookId)->totalCopies, 1);
    EXPECT_EQ(m_repository->listCopies(bookId)->size(), 1u);
    EXPECT_EQ(m_db->count("book_copies"), 2);  // archived, not deleted
}

TEST_F(test_core_ArchiveCatalog, RemovingTheLastCopyLeavesALiveBookWithNoCopies)
{
    const std::int64_t bookId = seedBookWithCopies(7, 1);
    ASSERT_TRUE(saveWithout(bookId, copyIdsOf(*m_db, bookId).front()));

    const auto book = m_repository->getBook(bookId);
    ASSERT_TRUE(book.has_value());
    EXPECT_TRUE(book->archivedAt.empty());
    EXPECT_EQ(book->totalCopies, 0);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countBooks({})), 1);
}

TEST_F(test_core_ArchiveCatalog, ReAddingANumberStillHeldByAnArchivedCopyIsRefused)
{
    const std::int64_t bookId = seedBookWithCopies(8, 2);
    const auto copies = m_repository->listCopies(bookId).value();
    BookCopyInput again;
    again.source = copies.front().source;
    again.localId = copies.front().localId;
    again.globalCopyId = copies.front().globalCopyId;

    const auto refused = saveWithout(bookId, copies.front().id, {again});
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.copy.numberHeldByArchived");
    EXPECT_EQ(refused.error().detail, copies.front().localId);
    EXPECT_TRUE(copyStamp(copies.front().id).empty());  // the whole save rolled back
    EXPECT_EQ(m_db->count("book_copies"), 2);
}

TEST_F(test_core_ArchiveCatalog, RestoreBookBringsBackOnlyTheCopiesArchivedWithIt)
{
    const std::int64_t bookId = seedBookWithCopies(9, 3);
    const std::vector<std::int64_t> copies = copyIdsOf(*m_db, bookId);
    {
        const ScopedClock earlier(DateTime(Date(2026, 9, 1), 9, 0, 0));
        ASSERT_TRUE(saveWithout(bookId, copies.at(0)));
    }
    {
        const ScopedClock later(DateTime(Date(2026, 9, 19), 10, 0, 0));
        ASSERT_TRUE(m_repository->archiveBook(bookId));
    }

    ASSERT_TRUE(m_repository->restoreBook(bookId));
    EXPECT_EQ(copyStamp(copies.at(0)), "2026-09-01 09:00:00");
    EXPECT_TRUE(copyStamp(copies.at(1)).empty());
    EXPECT_TRUE(copyStamp(copies.at(2)).empty());
    EXPECT_TRUE(m_repository->getBook(bookId)->archivedAt.empty());
}

TEST_F(test_core_ArchiveCatalog, RestoreBookLeavesNumberlessCopiesArchived)
{
    const std::int64_t bookId = seedBookWithCopies(10, 2);
    const std::vector<std::int64_t> copies = copyIdsOf(*m_db, bookId);
    ASSERT_TRUE(m_repository->archiveBook(bookId));
    ASSERT_TRUE(m_db->exec("UPDATE book_copies SET local_id = NULL, global_copy_id = NULL WHERE id = "
                           + std::to_string(copies.at(0))));

    ASSERT_TRUE(m_repository->restoreBook(bookId));
    EXPECT_FALSE(copyStamp(copies.at(0)).empty());
    EXPECT_TRUE(copyStamp(copies.at(1)).empty());
}

TEST_F(test_core_ArchiveCatalog, RestoreOfALiveBookIsRefused)
{
    const std::int64_t bookId = seedBookWithCopies(11, 1);
    const auto refused = m_repository->restoreBook(bookId);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.book.notArchived");
}

TEST_F(test_core_ArchiveCatalog, AddingABookIdenticalToAnArchivedOneSaysSo)
{
    BookSeed seed = uniqueBookSeed(12);
    seed.isbn = "9789973000012";
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_TRUE(m_repository->archiveBook(bookId));

    const auto duplicate = m_repository->createBook(seed.toInput());
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().key, "error.book.duplicateArchived");
}

TEST_F(test_core_ArchiveCatalog, ArchivedCopyListShowsTitleNumberAndStamp)
{
    const std::int64_t bookId = seedBookWithCopies(13, 2);
    const std::vector<std::int64_t> copies = copyIdsOf(*m_db, bookId);
    {
        const ScopedClock pinned(DateTime(Date(2026, 9, 19), 10, 0, 0));
        ASSERT_TRUE(saveWithout(bookId, copies.at(0)));
    }

    CopyQuery query;
    query.archive = ArchiveScope::Archived;
    const auto rows = VLMS_UNWRAP(m_repository->listCopyRows(query));
    ASSERT_EQ(rows.size(), 1u);
    const BookCopyRecord& row = rows.front();
    EXPECT_EQ(row.id, copies.at(0));
    EXPECT_EQ(row.bookTitle, m_repository->getBook(bookId)->title);
    EXPECT_EQ(row.archivedAt, "2026-09-19 10:00:00");
    EXPECT_FALSE(row.localId.empty());
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countCopyRows(query)), 1);

    query.search = row.localId;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countCopyRows(query)), 1);
}

TEST_F(test_core_ArchiveCatalog, FacetsCountLiveBooksOnly)
{
    const std::int64_t category = seedCategory(*m_db, "HIS", "History");
    BookSeed kept = uniqueBookSeed(14);
    kept.categoryId = category;
    BookSeed gone = uniqueBookSeed(15);
    gone.categoryId = category;
    seedBook(*m_db, kept);
    const std::int64_t goneId = seedBook(*m_db, gone);
    ASSERT_TRUE(m_repository->archiveBook(goneId));

    const auto categories = VLMS_UNWRAP(m_repository->listCategories());
    ASSERT_EQ(categories.size(), 1u);
    EXPECT_EQ(categories.front().bookCount, 1);

    const auto languages = VLMS_UNWRAP(m_repository->listBookLanguages());
    ASSERT_EQ(languages.size(), 1u);
    EXPECT_EQ(languages.front().bookCount, 1);
}
