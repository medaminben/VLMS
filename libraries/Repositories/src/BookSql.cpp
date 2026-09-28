#include "BookSql.h"

#include <VLMS/Database/SqlText.h>

#include <VLMS/Database/SqliteSession.h>
#include <VLMS/Core/Text.h>

using VLMS::SqlText::escapeLike;
using VLMS::trim;

namespace VLMS::Repositories::BookSql {
namespace {

const char* direction(const bool ascending)
{
    return ascending ? " ASC" : " DESC";
}

std::string withDirection(const std::string& expression, const bool ascending)
{
    return expression + direction(ascending);
}

/// The search term when the librarian typed a bare accession number: trimmed,
/// non-empty and nothing but ASCII digits. Anything else — a title, an ISBN, an
/// "AR-1002" — searches the text fields only.
std::string localNumberTerm(const BookQuery& query)
{
    const std::string term = trim(query.search);
    if (term.empty()) {
        return {};
    }
    for (const char c : term) {
        if (c < '0' || c > '9') {
            return {};
        }
    }
    return term;
}

/// Copies are scoped the way bookFrom scopes them: a live book matches its live
/// copies, an archived one the copies archived with it.
const char* copyScope(const ArchiveScope scope)
{
    return scope == ArchiveScope::Archived ? "archived_at IS NOT NULL" : "archived_at IS NULL";
}

/// Exact number first. The prefix arm opens only when no copy in scope carries
/// the number exactly, so a result set never mixes exact and prefix rows — that
/// inner NOT EXISTS is uncorrelated, so SQLite settles it once per query.
std::string localNumberPredicate(const ArchiveScope scope, const std::string& alias)
{
    return alias + ".local_id = :local_number OR (" + alias
        + ".local_id LIKE :local_number_prefix ESCAPE '\\' "
          "AND NOT EXISTS (SELECT 1 FROM book_copies bce WHERE bce.local_id = :local_number AND bce."
        + copyScope(scope) + "))";
}

}  // namespace

std::string filterClause(const BookQuery& query)
{
    std::string sql;
    if (query.archive == ArchiveScope::Live) {
        sql += " AND b.archived_at IS NULL ";
    } else if (query.archive == ArchiveScope::Archived) {
        sql += " AND b.archived_at IS NOT NULL ";
    }
    if (!trim(query.search).empty()) {
        sql += " AND (b.title LIKE :search ESCAPE '\\' "
               "OR a.name LIKE :search ESCAPE '\\' "
               "OR b.isbn LIKE :search ESCAPE '\\' ";
        // A number is an extra arm, never a replacement: "1984" still finds the
        // title. IN (...) is uncorrelated, so it is materialised once and drops
        // into countBooks, which has no book_copies join to hang a term on.
        if (const std::string number = localNumberTerm(query); !number.empty()) {
            sql += "OR b.id IN (SELECT bcn.book_id FROM book_copies bcn WHERE bcn."
                + std::string(copyScope(query.archive)) + " AND ("
                + localNumberPredicate(query.archive, "bcn") + ")) ";
        }
        sql += ") ";
    }
    if (!query.categoryCodes.empty()) {
        sql += " AND c.code IN (";
        for (std::size_t i = 0; i < query.categoryCodes.size(); ++i) {
            if (i > 0) {
                sql += ',';
            }
            sql += ":category_code_" + std::to_string(i);
        }
        sql += ") ";
    }
    if (!query.languages.empty()) {
        sql += " AND b.language IN (";
        for (std::size_t i = 0; i < query.languages.size(); ++i) {
            if (i > 0) {
                sql += ',';
            }
            sql += ":language_" + std::to_string(i);
        }
        sql += ") ";
    }
    if (query.coverFilter == CoverFilter::WithCover) {
        sql += " AND b.cover_image_path IS NOT NULL AND TRIM(b.cover_image_path) != '' ";
    } else if (query.coverFilter == CoverFilter::WithoutCover) {
        sql += " AND (b.cover_image_path IS NULL OR TRIM(b.cover_image_path) = '') ";
    }
    return sql;
}

void bindFilters(SqliteStatement& query, const BookQuery& queryData)
{
    const std::string search = trim(queryData.search);
    if (!search.empty()) {
        query.bind(":search", "%" + escapeLike(search) + "%");
    }
    if (const std::string number = localNumberTerm(queryData); !number.empty()) {
        query.bind(":local_number", number);
        query.bind(":local_number_prefix", escapeLike(number) + "%");
    }
    for (std::size_t i = 0; i < queryData.categoryCodes.size(); ++i) {
        query.bind(":category_code_" + std::to_string(i), queryData.categoryCodes[i]);
    }
    for (std::size_t i = 0; i < queryData.languages.size(); ++i) {
        query.bind(":language_" + std::to_string(i), queryData.languages[i]);
    }
}

std::string matchedLocalIdColumn(const BookQuery& query)
{
    const std::string number = localNumberTerm(query);
    if (number.empty()) {
        return "'' AS matched_local_id";
    }
    // The lowest matching number, so a book whose copies all match still reports
    // one stable answer. Same predicate as the filter: the cell and the row set
    // cannot disagree about which copy put the book on screen.
    return "COALESCE((SELECT bcm.local_id FROM book_copies bcm WHERE bcm.book_id = b.id AND bcm."
        + std::string(copyScope(query.archive)) + " AND ("
        + localNumberPredicate(query.archive, "bcm")
        + ") ORDER BY CAST(bcm.local_id AS INTEGER) LIMIT 1), '') AS matched_local_id";
}

std::string orderExpressions(const BookQuery& query)
{
    const bool asc = query.sortAscending;
    const std::string& column = query.sortColumn;
    if (column.empty() && query.archive == ArchiveScope::Archived) {
        return "b.archived_at DESC, b.id DESC";
    }
    if (column == BookSort::kTitle) {
        return withDirection("b.title COLLATE NOCASE", asc) + ", " + withDirection("b.id", asc);
    }
    if (column == BookSort::kAuthor) {
        return withDirection("COALESCE(a.name, '') COLLATE NOCASE", asc) + ", "
            + withDirection("b.id", asc);
    }
    if (column == BookSort::kCategory) {
        return withDirection("COALESCE(NULLIF(TRIM(c.label), ''), c.code, '') COLLATE NOCASE",
                             asc)
            + ", " + withDirection("b.id", asc);
    }
    if (column == BookSort::kLocalNumber) {
        // Copy-less books are pinned last whichever way the column is sorted; the
        // lowest number is what the cell shows, so it is what the column sorts on.
        return std::string("CASE WHEN COUNT(bc.id) = 0 THEN 1 ELSE 0 END ASC, ")
            + withDirection("MIN(CAST(bc.local_id AS INTEGER))", asc) + ", "
            + withDirection("b.id", asc);
    }
    if (column == BookSort::kCopies) {
        return withDirection("COUNT(bc.id)", asc) + ", " + withDirection("b.id", asc);
    }
    if (column == BookSort::kAvailable) {
        return withDirection("COALESCE(SUM(CASE WHEN bc.id IS NOT NULL AND active_loan.id IS NULL "
                             "THEN 1 ELSE 0 END), 0)",
                             asc)
            + ", " + withDirection("b.id", asc);
    }
    if (column == BookSort::kArchivedAt) {
        return withDirection("COALESCE(b.archived_at, '')", asc) + ", " + withDirection("b.id", asc);
    }
    return "b.title COLLATE NOCASE ASC, b.id ASC";
}

std::string orderClause(const BookQuery& query)
{
    return " ORDER BY " + orderExpressions(query);
}

}  // namespace VLMS::Repositories::BookSql
