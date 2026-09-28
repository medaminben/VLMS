#pragma once

#include <string>
#include <string_view>

struct LoanQuery;

namespace VLMS {
class SqliteStatement;
}

namespace VLMS::LoanSql {

[[nodiscard]] inline const char* todayPlaceholder() { return ":today"; }

[[nodiscard]] std::string isOverdue(std::string_view prefix = {});
void bindTodayIfPresent(SqliteStatement& query, std::string_view sql);
[[nodiscard]] std::string filterClause(const ::LoanQuery& query);
void bindFilters(SqliteStatement& query, const ::LoanQuery& queryData);
[[nodiscard]] std::string orderExpressions(const ::LoanQuery& query);
[[nodiscard]] std::string orderClause(const ::LoanQuery& query);

}  // namespace VLMS::LoanSql
