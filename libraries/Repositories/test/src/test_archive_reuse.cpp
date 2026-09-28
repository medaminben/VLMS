#include "TestDatabase.h"
#include "TestSeed.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Core/Locale.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using VLMS::Locale;
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
    input.notes = copy.notes;
    return input;
}

BookCopyInput reservedCopy(const std::string& localId)
{
    BookCopyInput copy;
    copy.source = "arabic";
    copy.localId = localId;
    copy.globalCopyId = "AR-" + localId;
    return copy;
}

}  // namespace

class test_core_ArchiveReuse : public ::testing::Test {
protected:
    void SetUp() override
    {
        Locale::setCode("en");
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository =
            std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_repository.reset();
        m_db.reset();
        Locale::setCode(Locale::kDefaultCode);
    }

    /// A new book whose only copy is archived; `localId` receives its number.
    std::int64_t archivedCopyOfNewBook(int index, std::string* localId)
    {
        const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(index));
        const std::int64_t copyId = copyIdsOf(*m_db, bookId).front();
        *localId = column(copyId, "local_id");
        EXPECT_TRUE(m_db->exec("UPDATE book_copies SET archived_at = '2026-09-19 10:00:00' "
                               "WHERE id = " + std::to_string(copyId)));
        return copyId;
    }

    std::string column(std::int64_t copyId, const std::string& name)
    {
        const auto value = m_db->scalar("SELECT " + name + " FROM book_copies WHERE id = "
                                        + std::to_string(copyId));
        return value.isNull() ? std::string("<null>") : value.toString();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_repository;
};

TEST_F(test_core_ArchiveReuse, RestoreCopyAlsoRestoresItsArchivedBookButNotItsSiblings)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_TRUE(m_repository->archiveBook(bookId));
    const std::vector<std::int64_t> copies = copyIdsOf(*m_db, bookId);

    ASSERT_TRUE(m_repository->restoreCopy(copies.at(0)));
    EXPECT_EQ(column(copies.at(0), "archived_at"), "<null>");
    EXPECT_NE(column(copies.at(1), "archived_at"), "<null>");
    EXPECT_TRUE(m_repository->getBook(bookId)->archivedAt.empty());
}

TEST_F(test_core_ArchiveReuse, RestoreNumberlessCopyGetsTheNextNumberAndSaysSo)
{
    BookSeed other = uniqueBookSeed(2);
    other.initialCopyCount = 3;
    seedBook(*m_db, other);
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(3));
    const std::int64_t copyId = copyIdsOf(*m_db, bookId).front();
    ASSERT_TRUE(m_db->exec("UPDATE book_copies SET archived_at = '2026-09-19 10:00:00', "
                           "local_id = NULL, global_copy_id = NULL, notes = 'shelf B' WHERE id = "
                           + std::to_string(copyId)));
    const std::string expected = std::to_string(
        m_db->scalar("SELECT MAX(CAST(local_id AS INTEGER)) FROM book_copies WHERE source = 'arabic'")
            .toInt()
        + 1);

    ASSERT_TRUE(m_repository->restoreCopy(copyId));
    EXPECT_EQ(column(copyId, "local_id"), expected);
    EXPECT_EQ(column(copyId, "global_copy_id"), "AR-" + expected);
    EXPECT_EQ(column(copyId, "notes"), "shelf B\nnew indexed as " + expected);
    EXPECT_EQ(column(copyId, "archived_at"), "<null>");
}

TEST_F(test_core_ArchiveReuse, RestoreOfALiveCopyIsRefused)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(4));
    const auto refused = m_repository->restoreCopy(copyIdsOf(*m_db, bookId).front());
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.copy.notArchived");
}

TEST_F(test_core_ArchiveReuse, ReuseMovesTheNumberOntoANewBook)
{
    std::string number;
    const std::int64_t archived = archivedCopyOfNewBook(5, &number);

    BookWrite write;
    write.book = uniqueBookSeed(6).toInput();
    write.copies = {reservedCopy(number)};
    write.releaseFromCopyId = archived;
    const auto created = m_repository->saveNewBook(write);
    ASSERT_TRUE(created) << created.error().key;

    EXPECT_EQ(column(archived, "local_id"), "<null>");
    EXPECT_EQ(column(archived, "global_copy_id"), "<null>");
    EXPECT_EQ(column(archived, "notes"), "was indexed as " + number);
    const auto copies = m_repository->listCopies(created.value());
    ASSERT_EQ(copies->size(), 1u);
    EXPECT_EQ(copies->front().localId, number);
    EXPECT_EQ(copies->front().globalCopyId, "AR-" + number);
}

TEST_F(test_core_ArchiveReuse, ReuseOntoAnExistingBookKeepsItsOtherCopies)
{
    std::string number;
    const std::int64_t archived = archivedCopyOfNewBook(7, &number);
    const std::int64_t target = seedBook(*m_db, uniqueBookSeed(8));

    BookWrite write;
    write.book = inputOf(m_repository->getBook(target).value());
    const auto existing = m_repository->listCopies(target);
    for (const BookCopyRecord& copy : existing.value()) {
        write.copies.push_back(inputOf(copy));
    }
    write.copies.push_back(reservedCopy(number));
    write.releaseFromCopyId = archived;

    ASSERT_TRUE(m_repository->saveExistingBook(target, write));
    EXPECT_EQ(m_repository->listCopies(target)->size(), 2u);
    EXPECT_EQ(column(archived, "local_id"), "<null>");
}

TEST_F(test_core_ArchiveReuse, AFailedReuseSaveLeavesTheArchivedNumberInPlace)
{
    std::string number;
    const std::int64_t archived = archivedCopyOfNewBook(9, &number);
    const std::int64_t otherBook = seedBook(*m_db, uniqueBookSeed(10));
    const std::string takenNumber = column(copyIdsOf(*m_db, otherBook).front(), "local_id");
    const int booksBefore = m_db->count("books");

    BookWrite write;
    write.book = uniqueBookSeed(11).toInput();
    write.copies = {reservedCopy(number), reservedCopy(takenNumber)};
    write.releaseFromCopyId = archived;
    const auto failed = m_repository->saveNewBook(write);

    ASSERT_FALSE(failed);
    EXPECT_TRUE(failed.error().key == "error.copy.duplicateLocal"
                || failed.error().key == "error.copy.duplicateGlobal")
        << failed.error().key;
    EXPECT_EQ(column(archived, "local_id"), number);
    EXPECT_EQ(column(archived, "global_copy_id"), "AR-" + number);
    EXPECT_EQ(column(archived, "notes"), "<null>");
    EXPECT_EQ(m_db->count("books"), booksBefore);
}

TEST_F(test_core_ArchiveReuse, ReuseIsRefusedWhenTheArchivedCopyLostItsNumber)
{
    std::string number;
    const std::int64_t archived = archivedCopyOfNewBook(12, &number);
    ASSERT_TRUE(m_db->exec("UPDATE book_copies SET local_id = NULL, global_copy_id = NULL "
                           "WHERE id = " + std::to_string(archived)));
    const int booksBefore = m_db->count("books");

    BookWrite write;
    write.book = uniqueBookSeed(13).toInput();
    write.copies = {reservedCopy(number)};
    write.releaseFromCopyId = archived;
    const auto refused = m_repository->saveNewBook(write);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.copy.reuseStale");
    EXPECT_EQ(m_db->count("books"), booksBefore);
}

TEST_F(test_core_ArchiveReuse, ReuseOntoABookOfTheOtherSourceIsRefused)
{
    std::string number;
    const std::int64_t archived = archivedCopyOfNewBook(14, &number);

    BookWrite write;
    write.book = uniqueBookSeed(15).toInput();
    write.book.language = "fr";
    write.copies = {reservedCopy(number)};
    write.releaseFromCopyId = archived;
    const auto refused = m_repository->saveNewBook(write);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.copy.sourceMismatch");
    EXPECT_EQ(column(archived, "local_id"), number);
}

TEST_F(test_core_ArchiveReuse, CopySourceFollowsTheLanguage)
{
    EXPECT_EQ(CatalogRepository::copySourceForLanguage("ar"), "arabic");
    EXPECT_EQ(CatalogRepository::copySourceForLanguage("fr"), "foreign");
    EXPECT_EQ(CatalogRepository::copySourceForLanguage("en"), "foreign");
}
