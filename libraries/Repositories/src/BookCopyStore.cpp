#include "BookCopyStore.h"

#include "RepoSql.h"
#include <VLMS/Database/SqliteSession.h>
#include <VLMS/Core/Text.h>

#include <VLMS/Core/Clock.h>
#include <VLMS/Database/SqlText.h>
#include <VLMS/Core/Strings.h>

#include <algorithm>
#include <unordered_set>

namespace VLMS::Repositories {

namespace {

std::string normalizedCopySource(const std::string& source)
{
    return Core::trim(source) == "arabic" ? "arabic" : "foreign";
}

std::string globalCopyIdFor(const std::string& source, const std::string& localId)
{
    const std::string prefix = source == "arabic" ? "AR" : "FR";
    return prefix + "-" + localId;
}

std::string appendedRemark(const std::string& notes, const std::string& remark)
{
    return notes.empty() ? remark : notes + "\n" + remark;
}

Core::Status nextCopyNumber(Database::SqliteSession& session,
                      const std::string& source,
                      std::int64_t* outNumber)
{
    auto query = session.prepare(
        "SELECT COALESCE(MAX(CAST(local_id AS INTEGER)), 0) FROM book_copies WHERE source = :source");
    if (!query) {
        return RepoSql::sqlFailure(query.error().detail);
    }
    if (!query->bind(":source", source) || !query->next()) {
        if (!query->ok()) {
            return RepoSql::sqlFailure(session.lastError());
        }
        return RepoSql::validation("error.copy.nextNumber");
    }
    *outNumber = query->int64(0) + 1;
    return Core::Status::ok();
}

Core::Status bindCopyFields(Database::SqliteStatement& query, const BookCopyInput& copy)
{
    if (!query.bind(":global_copy_id", Core::trim(copy.globalCopyId))
        || !query.bind(":source", normalizedCopySource(copy.source))
        || !query.bind(":local_id", Core::trim(copy.localId))
        || !query.bindOptional(":central_id", Database::SqlText::nullableText(copy.centralId))
        || !query.bindOptional(":classification", Database::SqlText::nullableText(copy.classification))
        || !query.bindOptional(":subject", Database::SqlText::nullableText(copy.subject))
        || !query.bindOptional(":notes", Database::SqlText::nullableText(copy.notes))
        || !query.bindOptional(":inventory_status", Database::SqlText::nullableText(copy.inventoryStatus))
        || !query.bindOptional(":compensation", Database::SqlText::nullableText(copy.compensation))
        || !query.bindOptional(":location", Database::SqlText::nullableText(copy.location))
        || !query.bindOptional(":index_code", Database::SqlText::nullableText(copy.indexCode))) {
        return RepoSql::sqlFailure("bind copy fields");
    }
    return Core::Status::ok();
}

/// True when the number `copy` asks for is held by an archived copy: the
/// librarian removed it (or typed it) while the archive still owns it.
bool numberHeldByArchived(Database::SqliteSession& session, const BookCopyInput& copy)
{
    auto q = session.prepare(
        "SELECT 1 FROM book_copies WHERE archived_at IS NOT NULL AND "
        "((source = :source AND local_id = :local_id) OR global_copy_id = :global_copy_id) "
        "LIMIT 1");
    if (!q) {
        return false;
    }
    if (!q->bind(":source", normalizedCopySource(copy.source))
        || !q->bind(":local_id", Core::trim(copy.localId))
        || !q->bind(":global_copy_id", Core::trim(copy.globalCopyId))) {
        return false;
    }
    return q->next();
}

Core::Status describeCopyConstraint(Database::SqliteSession& session,
                              const std::string& sqliteError,
                              const BookCopyInput& copy)
{
    const bool globalClash = sqliteError.find("global_copy_id") != std::string::npos;
    const bool localClash = sqliteError.find("local_id") != std::string::npos;
    if ((globalClash || localClash) && numberHeldByArchived(session, copy)) {
        return RepoSql::validation("error.copy.numberHeldByArchived", Core::trim(copy.localId));
    }
    if (globalClash) {
        return RepoSql::validation("error.copy.duplicateGlobal", Core::trim(copy.globalCopyId));
    }
    if (localClash) {
        return RepoSql::validation("error.copy.duplicateLocal", Core::trim(copy.localId));
    }
    return RepoSql::sqlFailure(sqliteError);
}

std::string copyFilterClause(const CopyQuery& query)
{
    std::string sql;
    if (query.archive == ArchiveScope::Live) {
        sql += " AND bc.archived_at IS NULL ";
    } else if (query.archive == ArchiveScope::Archived) {
        sql += " AND bc.archived_at IS NOT NULL ";
    }
    if (!Core::trim(query.search).empty()) {
        sql += " AND (COALESCE(bc.local_id, '') LIKE :search ESCAPE '\\' "
               "OR COALESCE(bc.global_copy_id, '') LIKE :search ESCAPE '\\' "
               "OR b.title LIKE :search ESCAPE '\\' "
               "OR COALESCE(bc.notes, '') LIKE :search ESCAPE '\\') ";
    }
    // The title's filters, as the Catalogue applies them: a copy is still
    // shelved under the title it came from.
    const auto appendIn = [&sql](const std::string& expression, const std::string& prefix,
                                 const std::vector<std::string>& values) {
        if (values.empty()) {
            return;
        }
        sql += " AND " + expression + " IN (";
        for (std::size_t i = 0; i < values.size(); ++i) {
            sql += (i > 0 ? "," : "") + (":" + prefix + std::to_string(i));
        }
        sql += ") ";
    };
    if (!query.categoryCodes.empty()) {
        sql += " AND b.category_id IN (SELECT cf.id FROM categories cf WHERE cf.code IN (";
        for (std::size_t i = 0; i < query.categoryCodes.size(); ++i) {
            sql += (i > 0 ? "," : "") + (":category_code_" + std::to_string(i));
        }
        sql += ")) ";
    }
    appendIn("b.language", "language_", query.languages);
    if (query.coverFilter == CoverFilter::WithCover) {
        sql += " AND b.cover_image_path IS NOT NULL AND TRIM(b.cover_image_path) != '' ";
    } else if (query.coverFilter == CoverFilter::WithoutCover) {
        sql += " AND (b.cover_image_path IS NULL OR TRIM(b.cover_image_path) = '') ";
    }
    return sql;
}

/// Binds what copyFilterClause wrote. False when a bind failed.
bool bindCopyFilters(Database::SqliteStatement& q, const CopyQuery& query)
{
    const std::string search = Core::trim(query.search);
    if (!search.empty() && !q.bind(":search", "%" + Database::SqlText::escapeLike(search) + "%")) {
        return false;
    }
    for (std::size_t i = 0; i < query.categoryCodes.size(); ++i) {
        if (!q.bind(":category_code_" + std::to_string(i), query.categoryCodes[i])) {
            return false;
        }
    }
    for (std::size_t i = 0; i < query.languages.size(); ++i) {
        if (!q.bind(":language_" + std::to_string(i), query.languages[i])) {
            return false;
        }
    }
    return true;
}

std::string copyOrderClause(const CopyQuery& query)
{
    const std::string dir = query.sortAscending ? " ASC" : " DESC";
    const std::string& column = query.sortColumn;
    if (column == CopySort::kLocalId) {
        return " ORDER BY CAST(bc.local_id AS INTEGER)" + dir + ", bc.local_id" + dir + ", bc.id"
            + dir;
    }
    if (column == CopySort::kSource) {
        return " ORDER BY bc.source" + dir + ", bc.id" + dir;
    }
    if (column == CopySort::kTitle) {
        return " ORDER BY b.title COLLATE NOCASE" + dir + ", bc.id" + dir;
    }
    if (column == CopySort::kArchivedAt) {
        return " ORDER BY COALESCE(bc.archived_at, '')" + dir + ", bc.id" + dir;
    }
    if (column == CopySort::kLoans) {
        return " ORDER BY (SELECT COUNT(*) FROM loans l WHERE l.book_copy_id = bc.id)" + dir
            + ", bc.id" + dir;
    }
    return " ORDER BY bc.archived_at DESC, bc.id DESC";
}

}  // namespace

BookCopyStore::BookCopyStore(Database::SqliteSession& session)
    : m_session(session)
{
}

Core::Result<std::vector<BookCopyRecord>> BookCopyStore::listCopies(const std::int64_t bookId) const
{
    auto query = m_session.prepare(R"SQL(
        SELECT
            bc.id, bc.book_id, bc.global_copy_id, bc.source, bc.local_id,
            COALESCE(bc.central_id, '')       AS central_id,
            COALESCE(bc.classification, '')   AS classification,
            COALESCE(bc.subject, '')          AS subject,
            COALESCE(bc.notes, '')            AS notes,
            COALESCE(bc.inventory_status, '') AS inventory_status,
            COALESCE(bc.compensation, '')     AS compensation,
            COALESCE(bc.location, '')         AS location,
            COALESCE(bc.index_code, '')       AS index_code,
            COALESCE(bc.source_row, 0)        AS source_row,
            CASE WHEN active_loan.id IS NULL THEN 0 ELSE 1 END AS on_loan
        FROM book_copies bc
        LEFT JOIN loans active_loan
            ON active_loan.book_copy_id = bc.id
           AND active_loan.returned_at IS NULL
        WHERE bc.book_id = :book_id AND bc.archived_at IS NULL
        ORDER BY bc.id
    )SQL");
    if (!query) {
        return RepoSql::sqlResult<std::vector<BookCopyRecord>>(query.error().detail);
    }
    if (!query->bind(":book_id", bookId)) {
        return RepoSql::sqlResult<std::vector<BookCopyRecord>>(m_session.lastError());
    }

    std::vector<BookCopyRecord> copies;
    while (query->next()) {
        BookCopyRecord copy;
        copy.id = query->int64(0);
        copy.bookId = query->int64(1);
        copy.globalCopyId = query->text(2);
        copy.source = query->text(3);
        copy.localId = query->text(4);
        copy.centralId = query->text(5);
        copy.classification = query->text(6);
        copy.subject = query->text(7);
        copy.notes = query->text(8);
        copy.inventoryStatus = query->text(9);
        copy.compensation = query->text(10);
        copy.location = query->text(11);
        copy.indexCode = query->text(12);
        copy.sourceRow = query->integer(13);
        copy.onLoan = query->integer(14) != 0;
        copies.push_back(std::move(copy));
    }
    if (!query->ok()) {
        return RepoSql::sqlResult<std::vector<BookCopyRecord>>(m_session.lastError());
    }
    return Core::Result<std::vector<BookCopyRecord>>::ok(std::move(copies));
}

Core::Result<std::vector<BookCopyRecord>> BookCopyStore::listCopyRows(const CopyQuery& query) const
{
    std::string sql = R"SQL(
        SELECT bc.id, bc.book_id, COALESCE(bc.global_copy_id, ''), bc.source,
               COALESCE(bc.local_id, ''), COALESCE(bc.central_id, ''),
               COALESCE(bc.notes, ''), b.title, COALESCE(bc.archived_at, ''),
               (SELECT COUNT(*) FROM loans l WHERE l.book_copy_id = bc.id),
               COALESCE(b.cover_image_path, '')
        FROM book_copies bc
        INNER JOIN books b ON b.id = bc.book_id
        WHERE 1 = 1
    )SQL";
    sql += copyFilterClause(query) + copyOrderClause(query) + " LIMIT :limit OFFSET :offset";

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<std::vector<BookCopyRecord>>(q.error().detail);
    }
    if (!bindCopyFilters(*q, query)
        || !q->bind(":limit", static_cast<std::int64_t>(query.limit))
        || !q->bind(":offset", static_cast<std::int64_t>(query.offset))) {
        return RepoSql::sqlResult<std::vector<BookCopyRecord>>(m_session.lastError());
    }

    std::vector<BookCopyRecord> copies;
    while (q->next()) {
        BookCopyRecord copy;
        copy.id = q->int64(0);
        copy.bookId = q->int64(1);
        copy.globalCopyId = q->text(2);
        copy.source = q->text(3);
        copy.localId = q->text(4);
        copy.centralId = q->text(5);
        copy.notes = q->text(6);
        copy.bookTitle = q->text(7);
        copy.archivedAt = q->text(8);
        copy.loanCount = q->integer(9);
        copy.coverImagePath = q->text(10);
        copies.push_back(std::move(copy));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<BookCopyRecord>>(m_session.lastError());
    }
    return Core::Result<std::vector<BookCopyRecord>>::ok(std::move(copies));
}

Core::Result<int> BookCopyStore::countCopyRows(const CopyQuery& query) const
{
    const std::string sql =
        "SELECT COUNT(*) FROM book_copies bc INNER JOIN books b ON b.id = bc.book_id WHERE 1 = 1"
        + copyFilterClause(query);
    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<int>(q.error().detail);
    }
    if (!bindCopyFilters(*q, query)) {
        return RepoSql::sqlResult<int>(m_session.lastError());
    }
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlResult<int>(m_session.lastError());
        }
        return Core::Result<int>::ok(0);
    }
    return Core::Result<int>::ok(q->integer(0));
}

Core::Status BookCopyStore::addCopies(const std::int64_t bookId, const std::string& language, const int count)
{
    if (count <= 0) {
        return Core::Status::ok();
    }

    const std::string source = sourceForLanguage(language);

    std::int64_t nextNumber = 0;
    if (const auto numbered = nextCopyNumber(m_session, source, &nextNumber); !numbered) {
        return numbered;
    }

    auto insert = m_session.prepare(
        "INSERT INTO book_copies (book_id, global_copy_id, source, local_id) "
        "VALUES (:book_id, :global_copy_id, :source, :local_id)");
    if (!insert) {
        return RepoSql::sqlFailure(insert.error().detail);
    }

    for (int added = 0; added < count; ++added) {
        const std::string localId = std::to_string(nextNumber + added);
        insert->reset();
        if (!insert->bind(":book_id", bookId)
            || !insert->bind(":global_copy_id", globalCopyIdFor(source, localId))
            || !insert->bind(":source", source) || !insert->bind(":local_id", localId)
            || !insert->exec()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
    }

    return Core::Status::ok();
}

void BookCopyStore::suggestCopyIdentifiers(const std::string& language,
                                           std::string* outSource,
                                           std::string* outLocalId,
                                           std::string* outGlobalCopyId) const
{
    const std::string source = sourceForLanguage(language);

    std::int64_t nextNumber = 1;
    nextCopyNumber(m_session, source, &nextNumber);
    const std::string localId = std::to_string(nextNumber);

    if (outSource != nullptr) {
        *outSource = source;
    }
    if (outLocalId != nullptr) {
        *outLocalId = localId;
    }
    if (outGlobalCopyId != nullptr) {
        *outGlobalCopyId = globalCopyIdFor(source, localId);
    }
}

Core::Result<std::vector<std::string>> BookCopyStore::freeLocalNumbers(const std::string& source,
                                                                 const int limit) const
{
    std::vector<std::string> free;
    if (limit <= 0) {
        return Core::Result<std::vector<std::string>>::ok(std::move(free));
    }

    // One sorted read, walked in C++. A recursive CTE with a NOT EXISTS would
    // scan the copies table once per candidate -- CAST(local_id AS INTEGER) is
    // not indexed -- which is ~19,773 scans for the live arabic stock.
    auto query = m_session.prepare(
        "SELECT CAST(local_id AS INTEGER) FROM book_copies "
        "WHERE source = :source AND local_id IS NOT NULL AND trim(local_id) <> '' "
        "ORDER BY 1");
    if (!query) {
        return RepoSql::sqlResult<std::vector<std::string>>(query.error().detail);
    }
    if (!query->bind(":source", normalizedCopySource(source))) {
        return RepoSql::sqlResult<std::vector<std::string>>(m_session.lastError());
    }

    std::int64_t expected = 1;
    while (query->next()) {
        const std::int64_t held = query->int64(0);
        // Duplicates cannot occur -- UNIQUE (source, local_id) -- but a number
        // below `expected` would loop forever if one ever did.
        if (held < expected) {
            continue;
        }
        for (; expected < held && static_cast<int>(free.size()) < limit; ++expected) {
            free.push_back(std::to_string(expected));
        }
        if (static_cast<int>(free.size()) >= limit) {
            break;
        }
        expected = held + 1;
    }
    if (!query->ok()) {
        return RepoSql::sqlResult<std::vector<std::string>>(m_session.lastError());
    }
    return Core::Result<std::vector<std::string>>::ok(std::move(free));
}

std::string BookCopyStore::sourceForLanguage(const std::string& language)
{
    return Core::trim(language) == "ar" ? "arabic" : "foreign";
}

Core::Status BookCopyStore::restoreCopy(const std::int64_t copyId)
{
    std::int64_t bookId = 0;
    std::string source;
    std::string notes;
    bool numberless = false;
    {
        auto read = m_session.prepare(
            "SELECT book_id, source, local_id IS NULL, archived_at IS NULL, COALESCE(notes, '') "
            "FROM book_copies WHERE id = :id");
        if (!read) {
            return RepoSql::sqlFailure(read.error().detail);
        }
        if (!read->bind(":id", copyId)) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (!read->next()) {
            if (!read->ok()) {
                return RepoSql::sqlFailure(m_session.lastError());
            }
            return RepoSql::notFound("error.copy.notFound");
        }
        if (read->integer(3) != 0) {
            return RepoSql::validation("error.copy.notArchived");
        }
        bookId = read->int64(0);
        source = read->text(1);
        numberless = read->integer(2) != 0;
        notes = read->text(4);
    }

    bool bookArchived = false;
    {
        auto book = m_session.prepare("SELECT archived_at IS NOT NULL FROM books WHERE id = :id");
        if (!book) {
            return RepoSql::sqlFailure(book.error().detail);
        }
        if (!book->bind(":id", bookId)) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (!book->next()) {
            if (!book->ok()) {
                return RepoSql::sqlFailure(m_session.lastError());
            }
            return RepoSql::validation("error.copy.bookMissing");
        }
        bookArchived = book->integer(0) != 0;
    }

    if (numberless) {
        std::int64_t next = 0;
        if (const auto numbered = nextCopyNumber(m_session, source, &next); !numbered) {
            return numbered;
        }
        const std::string localId = std::to_string(next);
        auto renumber = m_session.prepare(
            "UPDATE book_copies SET local_id = :local_id, global_copy_id = :global_copy_id, "
            "notes = :notes WHERE id = :id");
        if (!renumber) {
            return RepoSql::sqlFailure(renumber.error().detail);
        }
        if (!renumber->bind(":local_id", localId)
            || !renumber->bind(":global_copy_id", globalCopyIdFor(source, localId))
            || !renumber->bind(":notes",
                               appendedRemark(notes, Core::Strings::t("copy.note.newIndexedAs",
                                                                "number", localId)))
            || !renumber->bind(":id", copyId) || !renumber->exec()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
    }

    auto restore = m_session.prepare("UPDATE book_copies SET archived_at = NULL WHERE id = :id");
    if (!restore) {
        return RepoSql::sqlFailure(restore.error().detail);
    }
    if (!restore->bind(":id", copyId) || !restore->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }

    if (bookArchived) {
        auto book = m_session.prepare(
            "UPDATE books SET archived_at = NULL, updated_at = :now WHERE id = :id");
        if (!book) {
            return RepoSql::sqlFailure(book.error().detail);
        }
        if (!book->bind(":now", Core::Clock::nowIso()) || !book->bind(":id", bookId) || !book->exec()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
    }
    return Core::Status::ok();
}

Core::Status BookCopyStore::canPurgeCopy(const std::int64_t copyId) const
{
    auto read = m_session.prepare("SELECT archived_at IS NOT NULL FROM book_copies WHERE id = :id");
    if (!read) {
        return RepoSql::sqlFailure(read.error().detail);
    }
    if (!read->bind(":id", copyId)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (!read->next()) {
        if (!read->ok()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return RepoSql::notFound("error.copy.notFound");
    }
    if (read->integer(0) == 0) {
        return RepoSql::validation("error.copy.notArchived");
    }

    // Every loan row, archived ones included: an archived loan still points
    // here, and loans.book_copy_id has no ON DELETE clause, so SQLite would
    // refuse anyway -- in English, from a driver the librarian never sees.
    auto loans = m_session.prepare("SELECT COUNT(*) FROM loans WHERE book_copy_id = :id");
    if (!loans) {
        return RepoSql::sqlFailure(loans.error().detail);
    }
    if (!loans->bind(":id", copyId) || !loans->next()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (loans->integer(0) > 0) {
        return RepoSql::validation("error.copy.hasHistory");
    }
    return Core::Status::ok();
}

Core::Status BookCopyStore::purgeCopy(const std::int64_t copyId)
{
    if (const Core::Status gate = canPurgeCopy(copyId); !gate) {
        return gate;
    }

    auto remove = m_session.prepare("DELETE FROM book_copies WHERE id = :id");
    if (!remove) {
        return RepoSql::sqlFailure(remove.error().detail);
    }
    if (!remove->bind(":id", copyId) || !remove->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (remove->changes() <= 0) {
        return RepoSql::notFound("error.copy.notFound");
    }
    return Core::Status::ok();
}

Core::Status BookCopyStore::releaseArchivedNumber(const std::int64_t copyId,
                                            const std::vector<BookCopyInput>& incoming,
                                            const std::string& bookLanguage)
{
    std::string source;
    std::string localId;
    std::string notes;
    {
        auto read = m_session.prepare(
            "SELECT source, local_id, archived_at IS NOT NULL, COALESCE(notes, '') "
            "FROM book_copies WHERE id = :id");
        if (!read) {
            return RepoSql::sqlFailure(read.error().detail);
        }
        if (!read->bind(":id", copyId)) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (!read->next()) {
            if (!read->ok()) {
                return RepoSql::sqlFailure(m_session.lastError());
            }
            return RepoSql::validation("error.copy.reuseStale");
        }
        if (read->integer(2) == 0 || read->isNull(1)) {
            return RepoSql::validation("error.copy.reuseStale");
        }
        source = read->text(0);
        localId = read->text(1);
        notes = read->text(3);
    }

    const bool carried = std::any_of(incoming.begin(), incoming.end(), [&](const BookCopyInput& copy) {
        return copy.id == 0 && normalizedCopySource(copy.source) == source
            && Core::trim(copy.localId) == localId;
    });
    if (!carried) {
        return RepoSql::validation("error.copy.reuseStale");
    }
    if (sourceForLanguage(bookLanguage) != source) {
        return RepoSql::validation("error.copy.sourceMismatch");
    }

    // Released before the live copy is written: while the archived copy holds
    // the number, both unique indexes would refuse the new row.
    auto release = m_session.prepare(
        "UPDATE book_copies SET local_id = NULL, global_copy_id = NULL, notes = :notes "
        "WHERE id = :id");
    if (!release) {
        return RepoSql::sqlFailure(release.error().detail);
    }
    if (!release->bind(":notes", appendedRemark(notes, Core::Strings::t("copy.note.wasIndexedAs",
                                                                  "number", localId)))
        || !release->bind(":id", copyId) || !release->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    return Core::Status::ok();
}

Core::Status BookCopyStore::applyCopies(const std::int64_t bookId, const std::vector<BookCopyInput>& copies)
{
    std::unordered_set<std::string> seenGlobalIds;
    std::unordered_set<std::string> seenSourceLocal;
    for (const BookCopyInput& copy : copies) {
        const std::string localId = Core::trim(copy.localId);
        const std::string globalCopyId = Core::trim(copy.globalCopyId);
        const std::string source = normalizedCopySource(copy.source);

        if (localId.empty() || globalCopyId.empty()) {
            return RepoSql::validation("error.copy.idsRequired");
        }
        if (seenGlobalIds.contains(globalCopyId)) {
            return RepoSql::validation("error.copy.duplicateGlobalInList", globalCopyId);
        }
        const std::string sourceLocal = source + "/" + localId;
        if (seenSourceLocal.contains(sourceLocal)) {
            return RepoSql::validation("error.copy.duplicateLocalInList", localId);
        }
        seenGlobalIds.insert(globalCopyId);
        seenSourceLocal.insert(sourceLocal);
    }

    const auto listed = listCopies(bookId);
    if (!listed) {
        return Core::asStatus(listed);
    }
    const std::vector<BookCopyRecord>& stored = listed.value();

    std::unordered_set<std::int64_t> submittedIds;
    for (const BookCopyInput& copy : copies) {
        if (copy.id > 0) {
            submittedIds.insert(copy.id);
        }
    }

    for (const BookCopyRecord& copy : stored) {
        if (!submittedIds.contains(copy.id) && copy.onLoan) {
            return RepoSql::validation("error.copy.onLoan");
        }
    }

    // A row the librarian removed is archived, not deleted: loans still point
    // at it, and Archive can restore it or hand its number to a new copy. It
    // keeps its number, so re-adding that number in the same save is refused
    // by the unique index and reported as numberHeldByArchived.
    auto archive = m_session.prepare(
        "UPDATE book_copies SET archived_at = :stamp WHERE id = :id AND archived_at IS NULL");
    if (!archive) {
        return RepoSql::sqlFailure(archive.error().detail);
    }
    const std::string stamp = Core::Clock::nowIso();
    for (const BookCopyRecord& copy : stored) {
        if (submittedIds.contains(copy.id)) {
            continue;
        }
        archive->reset();
        if (!archive->bind(":stamp", stamp) || !archive->bind(":id", copy.id) || !archive->exec()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
    }

    auto update = m_session.prepare(R"SQL(
        UPDATE book_copies SET
            global_copy_id = :global_copy_id,
            source = :source,
            local_id = :local_id,
            central_id = :central_id,
            classification = :classification,
            subject = :subject,
            notes = :notes,
            inventory_status = :inventory_status,
            compensation = :compensation,
            location = :location,
            index_code = :index_code
        WHERE id = :id AND book_id = :book_id
    )SQL");
    if (!update) {
        return RepoSql::sqlFailure(update.error().detail);
    }

    auto insert = m_session.prepare(R"SQL(
        INSERT INTO book_copies (
            book_id, global_copy_id, source, local_id, central_id,
            classification, subject, notes, inventory_status, compensation,
            location, index_code
        ) VALUES (
            :book_id, :global_copy_id, :source, :local_id, :central_id,
            :classification, :subject, :notes, :inventory_status, :compensation,
            :location, :index_code
        )
    )SQL");
    if (!insert) {
        return RepoSql::sqlFailure(insert.error().detail);
    }

    for (const BookCopyInput& copy : copies) {
        Database::SqliteStatement& query = copy.id > 0 ? *update : *insert;
        query.reset();
        if (const auto bound = bindCopyFields(query, copy); !bound) {
            return bound;
        }
        if (!query.bind(":book_id", bookId)) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (copy.id > 0 && !query.bind(":id", copy.id)) {
            return RepoSql::sqlFailure(m_session.lastError());
        }

        if (const auto written = query.exec(); !written) {
            return describeCopyConstraint(m_session, written.error().detail, copy);
        }
        if (copy.id > 0 && query.changes() <= 0) {
            return RepoSql::notFound("error.copy.notFound");
        }
    }

    return Core::Status::ok();
}

Core::Status BookCopyStore::saveCopies(const std::int64_t bookId, const std::vector<BookCopyInput>& copies)
{
    return m_session.transaction([&] { return applyCopies(bookId, copies); });
}

}  // namespace VLMS::Repositories
