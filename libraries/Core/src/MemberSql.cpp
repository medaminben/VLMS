#include "MemberSql.h"

#include <VLMS/Core/SqlText.h>

#include "LoanSql.h"

#include "SqliteSession.h"
#include "Text.h"

#include <string_view>
#include <vector>

using VLMS::SqlText::escapeLike;
using VLMS::trim;
namespace LoanSql = VLMS::LoanSql;

namespace VLMS::MemberSql {
namespace {

void appendInClause(std::string& sql,
                    std::string_view expression,
                    std::string_view prefix,
                    const std::vector<std::string>& values)
{
    if (values.empty()) {
        return;
    }
    sql += " AND ";
    sql += expression;
    sql += " IN (";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            sql += ',';
        }
        sql += ':';
        sql += prefix;
        sql += std::to_string(i);
    }
    sql += ") ";
}

void bindInValues(SqliteStatement& query,
                  std::string_view prefix,
                  const std::vector<std::string>& values)
{
    for (std::size_t i = 0; i < values.size(); ++i) {
        query.bind(':' + std::string(prefix) + std::to_string(i), values[i]);
    }
}

MemberFacets facetsOf(const MemberQuery& query)
{
    return {query.statuses, query.sexes, query.inscriptionYears, query.ageGroups, query.cities};
}

const char* direction(const bool ascending)
{
    return ascending ? " ASC" : " DESC";
}

std::string withDirection(const std::string& expression, const bool ascending)
{
    return expression + direction(ascending);
}

}  // namespace

std::string isActive(const std::string_view alias)
{
    const std::string column = std::string(alias) + "active_until";
    return "(" + column + " IS NOT NULL AND " + column + " >= " + LoanSql::todayPlaceholder() + ")";
}

std::string statusExpression(const std::string_view alias)
{
    return "(CASE WHEN " + isActive(alias) + " THEN '" + std::string(MemberStatus::kActive)
        + "' ELSE '" + std::string(MemberStatus::kNonActive) + "' END)";
}

std::string facetClause(const MemberFacets& facets)
{
    std::string sql;
    appendInClause(sql, statusExpression("m."), "status_", facets.statuses);
    appendInClause(sql, "m.sex", "sex_", facets.sexes);
    appendInClause(sql, "substr(m.registered_at, 1, 4)", "year_", facets.inscriptionYears);
    appendInClause(sql, "m.age_group", "age_group_", facets.ageGroups);
    appendInClause(sql, "trim(m.city) COLLATE NOCASE", "city_", facets.cities);
    return sql;
}

void bindFacets(SqliteStatement& query, const MemberFacets& facets)
{
    bindInValues(query, "status_", facets.statuses);
    bindInValues(query, "sex_", facets.sexes);
    bindInValues(query, "year_", facets.inscriptionYears);
    bindInValues(query, "age_group_", facets.ageGroups);
    bindInValues(query, "city_", facets.cities);
}

std::string filterClause(const MemberQuery& query)
{
    std::string sql;
    if (query.archive == ArchiveScope::Live) {
        sql += " AND m.archived_at IS NULL ";
    } else if (query.archive == ArchiveScope::Archived) {
        sql += " AND m.archived_at IS NOT NULL ";
    }
    if (!trim(query.search).empty()) {
        sql += " AND (m.membership_number LIKE :search ESCAPE '\\' "
               "OR m.first_name LIKE :search ESCAPE '\\' "
               "OR m.last_name LIKE :search ESCAPE '\\' "
               "OR m.full_name LIKE :search ESCAPE '\\' "
               "OR m.occupation LIKE :search ESCAPE '\\' "
               "OR m.phone LIKE :search ESCAPE '\\' "
               "OR m.city LIKE :search ESCAPE '\\') ";
    }
    sql += facetClause(facetsOf(query));
    return sql;
}

void bindFilters(SqliteStatement& query, const MemberQuery& queryData)
{
    const std::string search = trim(queryData.search);
    if (!search.empty()) {
        query.bind(":search", "%" + escapeLike(search) + "%");
    }
    bindFacets(query, facetsOf(queryData));
}

std::string orderExpressions(const MemberQuery& query)
{
    const bool asc = query.sortAscending;
    const std::string& column = query.sortColumn;
    if (column.empty() && query.archive == ArchiveScope::Archived) {
        return "m.archived_at DESC, m.id DESC";
    }
    if (column == MemberSort::kNumber) {
        return withDirection("CAST(m.membership_number AS INTEGER)", asc) + ", "
            + withDirection("m.membership_number COLLATE NOCASE", asc) + ", "
            + withDirection("m.id", asc);
    }
    if (column == MemberSort::kName) {
        return withDirection("m.last_name COLLATE NOCASE", asc) + ", "
            + withDirection("m.first_name COLLATE NOCASE", asc) + ", "
            + withDirection("m.id", asc);
    }
    if (column == MemberSort::kPhone) {
        return withDirection("m.phone COLLATE NOCASE", asc) + ", " + withDirection("m.id", asc);
    }
    if (column == MemberSort::kCity) {
        return withDirection("m.city COLLATE NOCASE", asc) + ", " + withDirection("m.id", asc);
    }
    if (column == MemberSort::kStatus) {
        // The last active day, not the label: ascending puts the longest
        // expired first and the newest registration last.
        return withDirection("COALESCE(m.active_until, '')", asc) + ", " + withDirection("m.id", asc);
    }
    if (column == MemberSort::kLoans) {
        return withDirection("("
                             "SELECT COUNT(*) FROM loans l "
                             "WHERE l.member_id = m.id AND l.returned_at IS NULL"
                             ")",
                             asc)
            + ", " + withDirection("m.id", asc);
    }
    if (column == MemberSort::kAllLoans) {
        return withDirection("(SELECT COUNT(*) FROM loans l WHERE l.member_id = m.id)", asc) + ", "
            + withDirection("m.id", asc);
    }
    if (column == MemberSort::kArchivedAt) {
        return withDirection("COALESCE(m.archived_at, '')", asc) + ", " + withDirection("m.id", asc);
    }
    return "m.last_name COLLATE NOCASE ASC, m.first_name COLLATE NOCASE ASC, m.id ASC";
}

std::string orderClause(const MemberQuery& query)
{
    return " ORDER BY " + orderExpressions(query);
}

}  // namespace VLMS::MemberSql
