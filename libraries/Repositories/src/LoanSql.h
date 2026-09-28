#pragma once

#include <string>
#include <string_view>

namespace VLMS::Database {
class SqliteStatement;
}  // namespace VLMS::Database

namespace VLMS::Repositories {
struct LoanQuery;
}  // namespace VLMS::Repositories

namespace VLMS::Repositories::LoanSql {

[[nodiscard]] inline const char* todayPlaceholder() { return ":today"; }

[[nodiscard]] std::string isOverdue(std::string_view prefix = {});
void bindTodayIfPresent(Database::SqliteStatement& query, std::string_view sql);
[[nodiscard]] std::string filterClause(const LoanQuery& query);
void bindFilters(Database::SqliteStatement& query, const LoanQuery& queryData);
[[nodiscard]] std::string orderExpressions(const LoanQuery& query);
[[nodiscard]] std::string orderClause(const LoanQuery& query);

}  // namespace VLMS::Repositories::LoanSql
