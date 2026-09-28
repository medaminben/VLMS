#include "LoanSql.h"

#include <VLMS/Core/Clock.h>
#include <VLMS/Repositories/LoanTypes.h>
#include <VLMS/Database/SqlText.h>

#include "MemberSql.h"
#include <VLMS/Database/SqliteSession.h>
#include <VLMS/Core/Text.h>

using VLMS::SqlText::escapeLike;
using VLMS::trim;

namespace VLMS::Repositories::LoanSql {

std::string isOverdue(const std::string_view prefix)
{
    const std::string p(prefix);
    return "(" + p + "returned_at IS NULL AND (date(" + p + "due_at) IS NULL OR date(" + p
        + "due_at) < " + todayPlaceholder() + "))";
}

void bindTodayIfPresent(SqliteStatement& query, const std::string_view sql)
{
    if (sql.find(todayPlaceholder()) != std::string_view::npos) {
        query.bind(todayPlaceholder(), Clock::todayIso());
    }
}

namespace {

constexpr const char* kLoanAlias = "l.";

}  // namespace

std::string filterClause(const LoanQuery& query)
{
    std::string sql;

    if (query.archive == ArchiveScope::Live) {
        sql += " AND l.archived_at IS NULL ";
    } else if (query.archive == ArchiveScope::Archived) {
        sql += " AND l.archived_at IS NOT NULL ";
    }

    std::string filterClauses;
    for (const std::string& filter : query.filters) {
        std::string clause;
        if (filter == LoanFilter::kOpen) {
            // Open, Overdue, Returned are three states: past its due date a
            // loan is Overdue and no longer Open, as the Status column says.
            clause = "(l.returned_at IS NULL AND NOT " + isOverdue(kLoanAlias) + ")";
        } else if (filter == LoanFilter::kOverdue) {
            clause = isOverdue(kLoanAlias);
        } else if (filter == LoanFilter::kReturned) {
            clause = "(l.returned_at IS NOT NULL)";
        }
        if (clause.empty()) {
            continue;
        }
        if (!filterClauses.empty()) {
            filterClauses += " OR ";
        }
        filterClauses += clause;
    }
    if (!filterClauses.empty()) {
        sql += " AND (" + filterClauses + ") ";
    }

    if (query.memberId > 0) {
        sql += " AND l.member_id = :member_id ";
    }

    // Every loan query joins book_copies, so one book's history reaches all of
    // its copies — including copies since archived, which still lent the book.
    if (query.bookId > 0) {
        sql += " AND bc.book_id = :book_id ";
    }

    if (query.copyId > 0) {
        sql += " AND l.book_copy_id = :copy_id ";
    }

    sql += MemberSql::facetClause(query.member);

    if (!query.loanYears.empty()) {
        sql += " AND substr(l.borrowed_at, 1, 4) IN (";
        for (std::size_t i = 0; i < query.loanYears.size(); ++i) {
            sql += (i > 0 ? "," : "") + (":loan_year_" + std::to_string(i));
        }
        sql += ") ";
    }

    if (!trim(query.search).empty()) {
        sql += " AND (m.membership_number LIKE :search ESCAPE '\\' "
               "OR m.first_name LIKE :search ESCAPE '\\' "
               "OR m.last_name LIKE :search ESCAPE '\\' "
               "OR m.full_name LIKE :search ESCAPE '\\' "
               "OR (m.first_name || ' ' || m.last_name) LIKE :search ESCAPE '\\' "
               "OR b.title LIKE :search ESCAPE '\\' "
               "OR bc.local_id LIKE :search ESCAPE '\\' "
               "OR bc.global_copy_id LIKE :search ESCAPE '\\' "
               "OR COALESCE(a.name, '') LIKE :search ESCAPE '\\') ";
    }

    return sql;
}

void bindFilters(SqliteStatement& query, const LoanQuery& queryData)
{
    if (queryData.memberId > 0) {
        query.bind(":member_id", queryData.memberId);
    }
    if (queryData.bookId > 0) {
        query.bind(":book_id", queryData.bookId);
    }
    if (queryData.copyId > 0) {
        query.bind(":copy_id", queryData.copyId);
    }
    MemberSql::bindFacets(query, queryData.member);
    for (std::size_t i = 0; i < queryData.loanYears.size(); ++i) {
        query.bind(":loan_year_" + std::to_string(i), queryData.loanYears[i]);
    }
    const std::string search = trim(queryData.search);
    if (!search.empty()) {
        query.bind(":search", "%" + escapeLike(search) + "%");
    }
}

namespace {

const char* direction(const bool ascending)
{
    return ascending ? " ASC" : " DESC";
}

std::string withDirection(const std::string& expression, const bool ascending)
{
    return expression + direction(ascending);
}

}  // namespace

std::string orderExpressions(const LoanQuery& query)
{
    const bool asc = query.sortAscending;
    const std::string& column = query.sortColumn;
    if (column.empty() && query.archive == ArchiveScope::Archived) {
        return "l.archived_at DESC, l.id DESC";
    }
    if (column == LoanSort::kMember) {
        return withDirection("TRIM(m.first_name || ' ' || m.last_name) COLLATE NOCASE", asc) + ", "
            + withDirection("l.id", asc);
    }
    if (column == LoanSort::kNumber) {
        return withDirection("CAST(m.membership_number AS INTEGER)", asc) + ", "
            + withDirection("m.membership_number COLLATE NOCASE", asc) + ", "
            + withDirection("l.id", asc);
    }
    if (column == LoanSort::kTitle) {
        return withDirection("b.title COLLATE NOCASE", asc) + ", " + withDirection("l.id", asc);
    }
    if (column == LoanSort::kBorrowed) {
        return withDirection("l.borrowed_at", asc) + ", " + withDirection("l.id", asc);
    }
    if (column == LoanSort::kDue) {
        return withDirection("l.due_at", asc) + ", " + withDirection("l.id", asc);
    }
    if (column == LoanSort::kStatus) {
        return withDirection("CASE WHEN l.returned_at IS NOT NULL THEN 2 WHEN "
                                 + isOverdue("l.") + " THEN 0 ELSE 1 END",
                             asc)
            + ", " + withDirection("l.due_at", asc) + ", " + withDirection("l.id", asc);
    }
    if (column == LoanSort::kReturned) {
        return withDirection("COALESCE(l.returned_at, '')", asc) + ", " + withDirection("l.id", asc);
    }
    if (column == LoanSort::kArchivedAt) {
        return withDirection("COALESCE(l.archived_at, '')", asc) + ", " + withDirection("l.id", asc);
    }
    return "CASE WHEN l.returned_at IS NULL THEN 0 ELSE 1 END ASC, "
           "CASE WHEN "
        + isOverdue("l.") + " THEN 0 ELSE 1 END ASC, "
                            "l.due_at ASC, l.id DESC";
}

std::string orderClause(const LoanQuery& query)
{
    return " ORDER BY " + orderExpressions(query);
}

}  // namespace VLMS::Repositories::LoanSql
