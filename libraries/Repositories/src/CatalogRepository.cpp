#include <VLMS/Repositories/CatalogRepository.h>

#include <VLMS/Core/Clock.h>
#include <VLMS/Core/DateText.h>
#include <VLMS/Database/SqlText.h>

#include "BookCopyStore.h"
#include "BookSql.h"
#include "CategoryStore.h"
#include "NamedEntityStore.h"
#include "RepoSql.h"
#include <VLMS/Database/SqliteSession.h>
#include <VLMS/Core/Text.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace VLMS::Repositories {

namespace {

std::string blankIsbnSentinel(const std::string& isbn)
{
    const std::string trimmed = Core::trim(isbn);
    return trimmed.empty() ? std::string() : trimmed;
}

Core::Status bindOptionalId(Database::SqliteStatement& query, const std::string& name, const std::int64_t id)
{
    if (id > 0) {
        return query.bind(name, id);
    }
    return query.bindNull(name);
}

Core::Status bindBookFields(Database::SqliteStatement& query,
                      const BookInput& input,
                      const std::int64_t authorId,
                      const std::int64_t publisherId)
{
    const std::string language = Core::trim(input.language);
    if (!query.bind(":title", Core::trim(input.title)) || !bindOptionalId(query, ":author_id", authorId)
        || !bindOptionalId(query, ":publisher_id", publisherId)
        || !bindOptionalId(query, ":category_id", input.categoryId)
        || !query.bind(":isbn", blankIsbnSentinel(input.isbn))
        || !query.bindOptional(":publication_date",
                               Database::SqlText::nullableText(Core::DateText::normalizePublicationDate(input.publicationDate)))
        || !query.bindOptional(":publication_date_original", Database::SqlText::nullableText(input.publicationDate))
        || !query.bindOptional(":place_of_publication", Database::SqlText::nullableText(input.placeOfPublication))
        || !query.bindOptional(":pages", Database::SqlText::nullableText(input.pages))
        || !query.bindOptional(":dimensions", Database::SqlText::nullableText(input.dimensions))
        || !query.bind(":language", language)
        || !query.bindOptional(":description", Database::SqlText::nullableText(input.description))) {
        return RepoSql::sqlFailure("bind book fields");
    }
    return Core::Status::ok();
}

const char* kBookSelect = R"SQL(
            b.id,
            b.title,
            COALESCE(a.name, '') AS author_name,
            COALESCE(p.name, '') AS publisher_name,
            COALESCE(b.category_id, 0) AS category_id,
            COALESCE(c.code, '') AS category_code,
            COALESCE(NULLIF(TRIM(c.label), ''), c.code, '') AS category_label,
            COALESCE(b.isbn, '') AS isbn,
            COALESCE(b.publication_date, '') AS publication_date,
            COALESCE(b.place_of_publication, '') AS place_of_publication,
            COALESCE(b.pages, '') AS pages,
            COALESCE(b.dimensions, '') AS dimensions,
            b.language,
            COALESCE(b.description, '') AS description,
            COALESCE(b.cover_image_path, '') AS cover_image_path,
            COALESCE((
                SELECT bc2.subject
                FROM book_copies bc2
                WHERE bc2.book_id = b.id
                  AND bc2.subject IS NOT NULL
                  AND TRIM(bc2.subject) != ''
                LIMIT 1
            ), '') AS subject,
            COUNT(bc.id) AS total_copies,
            COALESCE(SUM(CASE WHEN bc.id IS NOT NULL AND active_loan.id IS NULL THEN 1 ELSE 0 END), 0) AS available_copies,
            COALESCE(b.archived_at, '') AS archived_at,
            COALESCE(GROUP_CONCAT(DISTINCT bc.local_id), '') AS local_ids,
            COALESCE(GROUP_CONCAT(DISTINCT CASE
                WHEN active_loan.id IS NOT NULL THEN bc.local_id
            END), '') AS on_loan_local_ids
)SQL";

/// Live books count their live copies; archived books count the copies that
/// are archived, which is the Archive's "archived copies" column.
std::string bookFrom(const ArchiveScope scope)
{
    const char* copyScope =
        scope == ArchiveScope::Archived ? "bc.archived_at IS NOT NULL" : "bc.archived_at IS NULL";
    return std::string(R"SQL(
        FROM books b
        LEFT JOIN authors a ON a.id = b.author_id
        LEFT JOIN publishers p ON p.id = b.publisher_id
        LEFT JOIN categories c ON c.id = b.category_id
        LEFT JOIN book_copies bc ON bc.book_id = b.id AND )SQL")
        + copyScope + R"SQL(
        LEFT JOIN loans active_loan
            ON active_loan.book_copy_id = bc.id
           AND active_loan.returned_at IS NULL
)SQL";
}

/// SQLite will not order a GROUP_CONCAT, so the numbers are sorted here.
/// They are accession counters: compare numerically, falling back to text so a
/// non-numeric legacy value still lands somewhere stable.
std::vector<std::string> splitLocalIds(const std::string& joined)
{
    std::vector<std::string> ids;
    std::size_t start = 0;
    while (start <= joined.size()) {
        const std::size_t comma = joined.find(',', start);
        const std::size_t end = comma == std::string::npos ? joined.size() : comma;
        std::string piece = joined.substr(start, end - start);
        if (!piece.empty()) {
            ids.push_back(std::move(piece));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    std::sort(ids.begin(), ids.end(), [](const std::string& a, const std::string& b) {
        const long long na = std::strtoll(a.c_str(), nullptr, 10);
        const long long nb = std::strtoll(b.c_str(), nullptr, 10);
        if (na != nb) {
            return na < nb;
        }
        return a < b;
    });
    return ids;
}

BookRecord readBookRow(Database::SqliteStatement& query)
{
    BookRecord book;
    book.id = query.int64(0);
    book.title = query.text(1);
    book.authorName = query.text(2);
    book.publisherName = query.text(3);
    book.categoryId = query.int64(4);
    book.categoryCode = query.text(5);
    book.categoryLabel = query.text(6);
    book.isbn = query.text(7);
    book.publicationDate = query.text(8);
    book.placeOfPublication = query.text(9);
    book.pages = query.text(10);
    book.dimensions = query.text(11);
    book.language = query.text(12);
    book.description = query.text(13);
    book.coverImagePath = query.text(14);
    book.subject = query.text(15);
    book.totalCopies = query.integer(16);
    book.availableCopies = query.integer(17);
    book.archivedAt = query.text(18);
    book.localIds = splitLocalIds(query.text(19));
    // The CASE yields NULL for a copy that is in, and GROUP_CONCAT drops those,
    // so this is the out-on-loan subset of the line above -- never a blank
    // entry, and never a number the book does not hold.
    book.localIdsOnLoan = splitLocalIds(query.text(20));
    book.matchedLocalId = query.text(21);
    return book;
}

}  // namespace

CatalogRepository::CatalogRepository(Database::SqliteSession& session,
                                     std::string resourcesDirectory)
    : m_session(session),
      m_resourcesDirectory(std::move(resourcesDirectory)),
      m_names(std::make_unique<NamedEntityStore>(session)),
      m_copies(std::make_unique<BookCopyStore>(session)),
      m_categories(std::make_unique<CategoryStore>(session))
{
}

CatalogRepository::~CatalogRepository() = default;

Core::Result<std::vector<BookRecord>> CatalogRepository::listBooks(const BookQuery& query) const
{
    std::string sql = std::string("SELECT ") + kBookSelect + ", "
        + BookSql::matchedLocalIdColumn(query) + bookFrom(query.archive)
        + " WHERE LENGTH(TRIM(b.title)) > 0";
    sql += BookSql::filterClause(query);
    sql += " GROUP BY b.id";
    sql += BookSql::orderClause(query);
    sql += " LIMIT :limit OFFSET :offset";

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<std::vector<BookRecord>>(q.error().detail);
    }
    BookSql::bindFilters(*q, query);
    if (!q->bind(":limit", static_cast<std::int64_t>(query.limit))
        || !q->bind(":offset", static_cast<std::int64_t>(query.offset))) {
        return RepoSql::sqlResult<std::vector<BookRecord>>(m_session.lastError());
    }

    std::vector<BookRecord> books;
    while (q->next()) {
        books.push_back(readBookRow(*q));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<BookRecord>>(m_session.lastError());
    }
    return Core::Result<std::vector<BookRecord>>::ok(std::move(books));
}

Core::Result<int> CatalogRepository::rankOfBook(const std::int64_t id, const BookQuery& query) const
{
    std::string sql =
        "SELECT ranked.rank FROM (\n"
        "    SELECT b.id, (ROW_NUMBER() OVER (ORDER BY "
        + BookSql::orderExpressions(query) + ")) - 1 AS rank\n"
        + bookFrom(query.archive)
        + " WHERE LENGTH(TRIM(b.title)) > 0"
        + BookSql::filterClause(query)
        + " GROUP BY b.id\n"
          ") ranked WHERE ranked.id = :id\n";

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<int>(q.error().detail);
    }
    BookSql::bindFilters(*q, query);
    if (!q->bind(":id", id)) {
        return RepoSql::sqlResult<int>(m_session.lastError());
    }
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlResult<int>(m_session.lastError());
        }
        return RepoSql::notFoundResult<int>("error.book.notFound");
    }
    return Core::Result<int>::ok(q->integer(0));
}

Core::Result<int> CatalogRepository::countBooks(const BookQuery& query) const
{
    std::string sql = R"SQL(
        SELECT COUNT(*) FROM (
            SELECT b.id
            FROM books b
            LEFT JOIN authors a ON a.id = b.author_id
            LEFT JOIN categories c ON c.id = b.category_id
            WHERE LENGTH(TRIM(b.title)) > 0
    )SQL";
    sql += BookSql::filterClause(query);
    sql += " GROUP BY b.id) ";

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<int>(q.error().detail);
    }
    BookSql::bindFilters(*q, query);
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlResult<int>(m_session.lastError());
        }
        return Core::Result<int>::ok(0);
    }
    return Core::Result<int>::ok(q->integer(0));
}

Core::Result<BookRecord> CatalogRepository::getBook(const std::int64_t id) const
{
    // getBook has no search behind it, so nothing matched: the column is still
    // selected, and empty, to keep readBookRow's indices in one place.
    const std::string sql =
        std::string("SELECT ") + kBookSelect + ", '' AS matched_local_id"
        + bookFrom(ArchiveScope::Live) + " WHERE b.id = :id GROUP BY b.id";
    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<BookRecord>(q.error().detail);
    }
    if (!q->bind(":id", id)) {
        return RepoSql::sqlResult<BookRecord>(m_session.lastError());
    }
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlResult<BookRecord>(m_session.lastError());
        }
        return RepoSql::notFoundResult<BookRecord>("error.book.notFound");
    }
    return Core::Result<BookRecord>::ok(readBookRow(*q));
}

Core::Result<std::int64_t> CatalogRepository::insertBookRow(const BookInput& input)
{
    if (Core::trim(input.title).empty()) {
        return RepoSql::validationResult<std::int64_t>("error.book.titleRequired");
    }
    if (Core::trim(input.language).empty()) {
        return RepoSql::validationResult<std::int64_t>("error.book.languageRequired");
    }

    const auto author = m_names->findOrCreate(NamedEntityStore::Kind::Author, input.authorName);
    if (!author) {
        return author;
    }
    const auto publisher =
        m_names->findOrCreate(NamedEntityStore::Kind::Publisher, input.publisherName);
    if (!publisher) {
        return publisher;
    }

    auto insert = m_session.prepare(R"SQL(
        INSERT INTO books (
            title, author_id, publisher_id, category_id, isbn,
            publication_date, publication_date_original, place_of_publication,
            pages, dimensions, language, description
        ) VALUES (
            :title, :author_id, :publisher_id, :category_id, :isbn,
            :publication_date, :publication_date_original, :place_of_publication,
            :pages, :dimensions, :language, :description
        )
    )SQL");
    if (!insert) {
        return RepoSql::sqlResult<std::int64_t>(insert.error().detail);
    }
    if (const auto bound = bindBookFields(*insert, input, author.value(), publisher.value());
        !bound) {
        return Core::Result<std::int64_t>::fail(bound.error().kind, bound.error().key,
                                          bound.error().detail);
    }
    if (const auto written = insert->exec(); !written) {
        const std::string error = written.error().detail;
        if (error.find("UNIQUE constraint failed: books.") != std::string::npos
            && archivedBookHasKey(input, author.value(), publisher.value())) {
            return RepoSql::validationResult<std::int64_t>("error.book.duplicateArchived");
        }
        return RepoSql::sqlResult<std::int64_t>(error);
    }
    return Core::Result<std::int64_t>::ok(m_session.lastInsertRowId());
}

Core::Status CatalogRepository::applyBookFields(const std::int64_t id, const BookInput& input)
{
    if (Core::trim(input.title).empty()) {
        return RepoSql::validation("error.book.titleRequired");
    }
    if (Core::trim(input.language).empty()) {
        return RepoSql::validation("error.book.languageRequired");
    }

    const auto author = m_names->findOrCreate(NamedEntityStore::Kind::Author, input.authorName);
    if (!author) {
        return Core::asStatus(author);
    }
    const auto publisher =
        m_names->findOrCreate(NamedEntityStore::Kind::Publisher, input.publisherName);
    if (!publisher) {
        return Core::asStatus(publisher);
    }

    auto update = m_session.prepare(R"SQL(
        UPDATE books SET
            title = :title,
            author_id = :author_id,
            publisher_id = :publisher_id,
            category_id = :category_id,
            isbn = :isbn,
            publication_date = :publication_date,
            publication_date_original = :publication_date_original,
            place_of_publication = :place_of_publication,
            pages = :pages,
            dimensions = :dimensions,
            language = :language,
            description = :description,
            updated_at = :updated_at
        WHERE id = :id
    )SQL");
    if (!update) {
        return RepoSql::sqlFailure(update.error().detail);
    }
    if (const auto bound = bindBookFields(*update, input, author.value(), publisher.value());
        !bound) {
        return bound;
    }
    if (!update->bind(":updated_at", Core::Clock::nowIso()) || !update->bind(":id", id)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (const auto written = update->exec(); !written) {
        const std::string error = written.error().detail;
        if (error.find("UNIQUE constraint failed: books.") != std::string::npos
            && archivedBookHasKey(input, author.value(), publisher.value())) {
            return RepoSql::validation("error.book.duplicateArchived");
        }
        return RepoSql::sqlFailure(error);
    }
    if (update->changes() <= 0) {
        return RepoSql::notFound("error.book.notFound");
    }
    return Core::Status::ok();
}

Core::Result<std::int64_t> CatalogRepository::createBook(const BookInput& input)
{
    if (input.initialCopyCount < 1) {
        return RepoSql::validationResult<std::int64_t>("error.book.minCopies");
    }

    std::int64_t bookId = 0;
    const Core::Status work = m_session.transaction([&] {
        const auto inserted = insertBookRow(input);
        if (!inserted) {
            return Core::asStatus(inserted);
        }
        bookId = inserted.value();
        return m_copies->addCopies(bookId, Core::trim(input.language), input.initialCopyCount);
    });
    if (!work) {
        return Core::Result<std::int64_t>::fail(work.error().kind, work.error().key, work.error().detail);
    }
    return Core::Result<std::int64_t>::ok(bookId);
}

Core::Status CatalogRepository::updateBook(const std::int64_t id, const BookInput& input)
{
    return m_session.transaction([&] { return applyBookFields(id, input); });
}

Core::Status CatalogRepository::deleteBook(const std::int64_t id)
{
    auto loanCheck = m_session.prepare(
        "SELECT COUNT(*) FROM loans l "
        "INNER JOIN book_copies bc ON bc.id = l.book_copy_id "
        "WHERE bc.book_id = :book_id AND l.returned_at IS NULL");
    if (!loanCheck) {
        return RepoSql::sqlFailure(loanCheck.error().detail);
    }
    if (!loanCheck->bind(":book_id", id) || !loanCheck->next()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (loanCheck->integer(0) > 0) {
        return RepoSql::validation("error.book.hasActiveLoans");
    }

    auto remove = m_session.prepare("DELETE FROM books WHERE id = :id");
    if (!remove) {
        return RepoSql::sqlFailure(remove.error().detail);
    }
    if (!remove->bind(":id", id) || !remove->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (remove->changes() <= 0) {
        return RepoSql::notFound("error.book.notFound");
    }

    const std::filesystem::path bookDir =
        std::filesystem::path(m_resourcesDirectory) / "books" / std::to_string(id);
    if (std::filesystem::exists(bookDir)) {
        std::error_code error;
        std::filesystem::remove_all(bookDir, error);
    }
    return Core::Status::ok();
}

Core::Result<bool> CatalogRepository::bookHasOpenLoans(const std::int64_t id) const
{
    auto loanCheck = m_session.prepare(
        "SELECT COUNT(*) FROM loans l "
        "INNER JOIN book_copies bc ON bc.id = l.book_copy_id "
        "WHERE bc.book_id = :book_id AND bc.archived_at IS NULL AND l.returned_at IS NULL");
    if (!loanCheck) {
        return RepoSql::sqlResult<bool>(loanCheck.error().detail);
    }
    if (!loanCheck->bind(":book_id", id) || !loanCheck->next()) {
        return RepoSql::sqlResult<bool>(m_session.lastError());
    }
    return Core::Result<bool>::ok(loanCheck->integer(0) > 0);
}

Core::Status CatalogRepository::canArchiveBook(const std::int64_t id) const
{
    const auto open = bookHasOpenLoans(id);
    if (!open) {
        return Core::asStatus(open);
    }
    if (open.value()) {
        return RepoSql::validation("error.book.hasActiveLoans");
    }

    auto read = m_session.prepare(
        "SELECT 1 FROM books WHERE id = :id AND archived_at IS NULL");
    if (!read) {
        return RepoSql::sqlFailure(read.error().detail);
    }
    if (!read->bind(":id", id)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (!read->next()) {
        if (!read->ok()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return RepoSql::notFound("error.book.notFound");
    }
    return Core::Status::ok();
}

Core::Status CatalogRepository::archiveBook(const std::int64_t id)
{
    return m_session.transaction([&] {
        if (const Core::Status gate = canArchiveBook(id); !gate) {
            return gate;
        }

        // One read of the clock for the book and every copy: restoreBook brings
        // back exactly the copies that carry the book's own stamp. The cover
        // folder stays -- the Archive still shows it.
        const std::string stamp = Core::Clock::nowIso();
        auto book = m_session.prepare(
            "UPDATE books SET archived_at = :stamp, updated_at = :stamp "
            "WHERE id = :id AND archived_at IS NULL");
        if (!book) {
            return RepoSql::sqlFailure(book.error().detail);
        }
        if (!book->bind(":stamp", stamp) || !book->bind(":id", id) || !book->exec()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (book->changes() <= 0) {
            return RepoSql::notFound("error.book.notFound");
        }

        auto copies = m_session.prepare(
            "UPDATE book_copies SET archived_at = :stamp "
            "WHERE book_id = :id AND archived_at IS NULL");
        if (!copies) {
            return RepoSql::sqlFailure(copies.error().detail);
        }
        if (!copies->bind(":stamp", stamp) || !copies->bind(":id", id) || !copies->exec()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return Core::Status::ok();
    });
}

Core::Status CatalogRepository::restoreBook(const std::int64_t id)
{
    return m_session.transaction([&] {
        std::string stamp;
        {
            auto read = m_session.prepare("SELECT archived_at FROM books WHERE id = :id");
            if (!read) {
                return RepoSql::sqlFailure(read.error().detail);
            }
            if (!read->bind(":id", id)) {
                return RepoSql::sqlFailure(m_session.lastError());
            }
            if (!read->next()) {
                if (!read->ok()) {
                    return RepoSql::sqlFailure(m_session.lastError());
                }
                return RepoSql::notFound("error.book.notFound");
            }
            if (read->isNull(0)) {
                return RepoSql::validation("error.book.notArchived");
            }
            stamp = read->text(0);
        }

        auto book = m_session.prepare(
            "UPDATE books SET archived_at = NULL, updated_at = :now WHERE id = :id");
        if (!book) {
            return RepoSql::sqlFailure(book.error().detail);
        }
        if (!book->bind(":now", Core::Clock::nowIso()) || !book->bind(":id", id) || !book->exec()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }

        // Copies archived on their own earlier carry an older stamp and stay
        // archived; a copy whose number went to another copy stays too, for
        // the librarian to restore by hand (it gets a new number then).
        auto copies = m_session.prepare(
            "UPDATE book_copies SET archived_at = NULL "
            "WHERE book_id = :id AND archived_at = :stamp AND local_id IS NOT NULL");
        if (!copies) {
            return RepoSql::sqlFailure(copies.error().detail);
        }
        if (!copies->bind(":id", id) || !copies->bind(":stamp", stamp) || !copies->exec()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return Core::Status::ok();
    });
}

Core::Status CatalogRepository::canPurgeBook(const std::int64_t id) const
{
    auto read = m_session.prepare("SELECT archived_at IS NOT NULL FROM books WHERE id = :id");
    if (!read) {
        return RepoSql::sqlFailure(read.error().detail);
    }
    if (!read->bind(":id", id)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (!read->next()) {
        if (!read->ok()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return RepoSql::notFound("error.book.notFound");
    }
    if (read->integer(0) == 0) {
        return RepoSql::validation("error.book.notArchived");
    }

    // Every copy, whatever its archive flag. The Archive's Copies column is
    // scoped to archived rows; this check must not depend on which list asked.
    auto copies = m_session.prepare("SELECT COUNT(*) FROM book_copies WHERE book_id = :id");
    if (!copies) {
        return RepoSql::sqlFailure(copies.error().detail);
    }
    if (!copies->bind(":id", id) || !copies->next()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (copies->integer(0) > 0) {
        return RepoSql::validation("error.book.hasCopies");
    }
    return Core::Status::ok();
}

Core::Status CatalogRepository::purgeBook(const std::int64_t id)
{
    const auto removed = m_session.transaction([&]() -> Core::Status {
        if (const Core::Status gate = canPurgeBook(id); !gate) {
            return gate;
        }

        auto remove = m_session.prepare("DELETE FROM books WHERE id = :id");
        if (!remove) {
            return RepoSql::sqlFailure(remove.error().detail);
        }
        if (!remove->bind(":id", id) || !remove->exec()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (remove->changes() <= 0) {
            return RepoSql::notFound("error.book.notFound");
        }
        return Core::Status::ok();
    });
    if (!removed) {
        return removed;
    }

    // Outside the transaction: a filesystem removal cannot be rolled back, so
    // it waits until the row is certainly gone.
    const std::filesystem::path bookDir =
        std::filesystem::path(m_resourcesDirectory) / "books" / std::to_string(id);
    if (std::filesystem::exists(bookDir)) {
        std::error_code error;
        std::filesystem::remove_all(bookDir, error);
    }
    return Core::Status::ok();
}

Core::Status CatalogRepository::restoreCopy(const std::int64_t copyId)
{
    return m_session.transaction([&] { return m_copies->restoreCopy(copyId); });
}

Core::Status CatalogRepository::canPurgeCopy(const std::int64_t copyId) const
{
    return m_copies->canPurgeCopy(copyId);
}

Core::Status CatalogRepository::purgeCopy(const std::int64_t copyId)
{
    return m_session.transaction([&] { return m_copies->purgeCopy(copyId); });
}

std::string CatalogRepository::copySourceForLanguage(const std::string& language)
{
    return BookCopyStore::sourceForLanguage(language);
}

Core::Result<std::vector<BookCopyRecord>> CatalogRepository::listCopyRows(const CopyQuery& query) const
{
    return m_copies->listCopyRows(query);
}

Core::Result<int> CatalogRepository::countCopyRows(const CopyQuery& query) const
{
    return m_copies->countCopyRows(query);
}

bool CatalogRepository::archivedBookHasKey(const BookInput& input,
                                           const std::int64_t authorId,
                                           const std::int64_t publisherId) const
{
    // = rather than IS: the UNIQUE key treats NULLs as distinct, so a book with
    // no author never clashes, and this lookup must agree with it.
    auto q = m_session.prepare(
        "SELECT 1 FROM books WHERE archived_at IS NOT NULL AND title = :title "
        "AND author_id = :author_id AND publisher_id = :publisher_id "
        "AND isbn = :isbn AND language = :language LIMIT 1");
    if (!q) {
        return false;
    }
    if (!q->bind(":title", Core::trim(input.title)) || !q->bind(":author_id", authorId)
        || !q->bind(":publisher_id", publisherId)
        || !q->bind(":isbn", blankIsbnSentinel(input.isbn))
        || !q->bind(":language", Core::trim(input.language))) {
        return false;
    }
    return q->next();
}

std::string CatalogRepository::resolveCoverPath(const std::string& storedPath) const
{
    if (storedPath.empty()) {
        return {};
    }
    const std::filesystem::path path(storedPath);
    if (path.is_absolute()) {
        return storedPath;
    }
    return (std::filesystem::path(m_resourcesDirectory) / storedPath).string();
}

Core::Status CatalogRepository::applyCoverImage(const std::int64_t bookId,
                                          const std::string& sourceFilePath)
{
    if (sourceFilePath.empty()) {
        return RepoSql::validation("error.cover.emptyPath");
    }

    const std::filesystem::path source(sourceFilePath);
    std::error_code error;
    if (!std::filesystem::exists(source, error) || !std::filesystem::is_regular_file(source, error)) {
        return RepoSql::validation("error.cover.missingFile");
    }

    const std::filesystem::path bookDir =
        std::filesystem::path(m_resourcesDirectory) / "books" / std::to_string(bookId);
    std::filesystem::create_directories(bookDir, error);
    if (error) {
        return RepoSql::validation("error.cover.copyFailed");
    }

    std::string extension = source.extension().string();
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(0, 1);
    }
    if (extension.empty()) {
        extension = "jpg";
    }
    const std::string relativePath = "books/" + std::to_string(bookId) + "/cover." + extension;
    const std::filesystem::path targetPath =
        std::filesystem::path(m_resourcesDirectory) / relativePath;

    if (std::filesystem::exists(targetPath, error)) {
        std::filesystem::remove(targetPath, error);
    }
    if (!std::filesystem::copy_file(source, targetPath, error)) {
        return RepoSql::validation("error.cover.copyFailed");
    }

    auto update = m_session.prepare(
        "UPDATE books SET cover_image_path = :path, updated_at = :updated_at WHERE id = :id");
    if (!update) {
        return RepoSql::sqlFailure(update.error().detail);
    }
    if (!update->bind(":path", relativePath) || !update->bind(":updated_at", Core::Clock::nowIso())
        || !update->bind(":id", bookId) || !update->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    return Core::Status::ok();
}

Core::Status CatalogRepository::setCoverImage(const std::int64_t bookId, const std::string& sourceFilePath)
{
    return applyCoverImage(bookId, sourceFilePath);
}

Core::Result<std::int64_t> CatalogRepository::saveNewBook(const BookWrite& write)
{
    std::int64_t bookId = 0;
    const Core::Status work = m_session.transaction([&] {
        BookInput input = write.book;
        if (write.copies.empty() && input.initialCopyCount < 1) {
            return RepoSql::validation("error.book.minCopies");
        }
        if (write.releaseFromCopyId > 0) {
            if (const auto released = m_copies->releaseArchivedNumber(
                    write.releaseFromCopyId, write.copies, Core::trim(write.book.language));
                !released) {
                return released;
            }
        }
        if (!write.copies.empty()) {
            input.initialCopyCount = 1;
        }

        const auto inserted = insertBookRow(input);
        if (!inserted) {
            return Core::asStatus(inserted);
        }
        bookId = inserted.value();

        if (write.copies.empty()) {
            if (const auto copies =
                    m_copies->addCopies(bookId, Core::trim(input.language), input.initialCopyCount);
                !copies) {
                return copies;
            }
        } else if (const auto copies = m_copies->applyCopies(bookId, write.copies); !copies) {
            return copies;
        }

        if (!write.coverSourcePath.empty()) {
            if (const auto cover = applyCoverImage(bookId, write.coverSourcePath); !cover) {
                return cover;
            }
        }
        return Core::Status::ok();
    });
    if (!work) {
        return Core::Result<std::int64_t>::fail(work.error().kind, work.error().key, work.error().detail);
    }
    return Core::Result<std::int64_t>::ok(bookId);
}

Core::Status CatalogRepository::saveExistingBook(const std::int64_t id, const BookWrite& write)
{
    return m_session.transaction([&] {
        if (write.releaseFromCopyId > 0) {
            if (const auto released = m_copies->releaseArchivedNumber(
                    write.releaseFromCopyId, write.copies, Core::trim(write.book.language));
                !released) {
                return released;
            }
        }
        if (const auto updated = applyBookFields(id, write.book); !updated) {
            return updated;
        }
        if (const auto copies = m_copies->applyCopies(id, write.copies); !copies) {
            return copies;
        }
        if (!write.coverSourcePath.empty()) {
            if (const auto cover = applyCoverImage(id, write.coverSourcePath); !cover) {
                return cover;
            }
        }
        return Core::Status::ok();
    });
}

Core::Result<std::vector<BookCopyRecord>> CatalogRepository::listCopies(const std::int64_t bookId) const
{
    return m_copies->listCopies(bookId);
}

Core::Status CatalogRepository::saveCopies(const std::int64_t bookId,
                                     const std::vector<BookCopyInput>& copies)
{
    return m_copies->saveCopies(bookId, copies);
}

void CatalogRepository::suggestCopyIdentifiers(const std::string& language,
                                               std::string* outSource,
                                               std::string* outLocalId,
                                               std::string* outGlobalCopyId) const
{
    m_copies->suggestCopyIdentifiers(language, outSource, outLocalId, outGlobalCopyId);
}

Core::Result<std::vector<std::string>> CatalogRepository::listFreeLocalNumbers(const std::string& source,
                                                                         const int limit) const
{
    return m_copies->freeLocalNumbers(source, limit);
}

Core::Result<std::vector<CategoryRecord>> CatalogRepository::listCategories() const
{
    return m_categories->listCategories();
}

Core::Result<std::vector<CategoryRecord>> CatalogRepository::listAllCategories() const
{
    return m_categories->listAllCategories();
}

Core::Result<std::int64_t> CatalogRepository::createCategory(const std::string& code,
                                                       const std::string& label)
{
    return m_categories->createCategory(code, label);
}

Core::Status CatalogRepository::updateCategory(const std::int64_t id,
                                         const std::string& code,
                                         const std::string& label)
{
    return m_categories->updateCategory(id, code, label);
}

Core::Status CatalogRepository::deleteCategory(const std::int64_t id)
{
    return m_categories->deleteCategory(id);
}

Core::Result<std::vector<std::string>> CatalogRepository::listAuthorNames() const
{
    return m_names->listNames(NamedEntityStore::Kind::Author);
}

Core::Result<std::vector<std::string>> CatalogRepository::listPublisherNames() const
{
    return m_names->listNames(NamedEntityStore::Kind::Publisher);
}

Core::Result<std::vector<LanguageRecord>> CatalogRepository::listBookLanguages(
    const ArchiveScope scope) const
{
    std::string scopeCondition;
    if (scope == ArchiveScope::Live) {
        scopeCondition = " AND b.archived_at IS NULL";
    } else if (scope == ArchiveScope::Archived) {
        scopeCondition = " AND b.archived_at IS NOT NULL";
    }
    auto q = m_session.prepare(
        "SELECT b.language, COUNT(*) AS book_count FROM books b"
        " WHERE LENGTH(TRIM(b.title)) > 0 AND LENGTH(TRIM(b.language)) > 0"
        + scopeCondition
        + " GROUP BY b.language ORDER BY b.language COLLATE NOCASE");
    if (!q) {
        return RepoSql::sqlResult<std::vector<LanguageRecord>>(q.error().detail);
    }

    std::vector<LanguageRecord> languages;
    while (q->next()) {
        LanguageRecord language;
        language.code = q->text(0);
        language.bookCount = q->integer(1);
        languages.push_back(std::move(language));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<LanguageRecord>>(m_session.lastError());
    }
    return Core::Result<std::vector<LanguageRecord>>::ok(std::move(languages));
}

}  // namespace VLMS::Repositories
