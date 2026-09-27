#pragma once

#include <VLMS/Core/MemberTypes.h>

#include <string>
#include <string_view>

namespace VLMS {
class SqliteStatement;
}

namespace VLMS::MemberSql {

/// True while a member's year is running: active_until is today or later.
/// Never NULL, so NOT works on it. Uses :today -- bind it with
/// LoanSql::bindTodayIfPresent. `alias` is "m." or "".
[[nodiscard]] std::string isActive(std::string_view alias);
/// 'active' or 'non_active', worked out from active_until. Status is not
/// stored anywhere. Uses :today.
[[nodiscard]] std::string statusExpression(std::string_view alias);

/// Sex, year, age group, city and status, on members aliased `m`. The loan
/// queries join members as `m` too, so they filter the borrower with it.
[[nodiscard]] std::string facetClause(const MemberFacets& facets);
void bindFacets(SqliteStatement& query, const MemberFacets& facets);

[[nodiscard]] std::string filterClause(const MemberQuery& query);
void bindFilters(SqliteStatement& query, const MemberQuery& queryData);
[[nodiscard]] std::string orderExpressions(const MemberQuery& query);
[[nodiscard]] std::string orderClause(const MemberQuery& query);

}  // namespace VLMS::MemberSql
