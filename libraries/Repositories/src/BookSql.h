#pragma once

#include <VLMS/Repositories/CatalogTypes.h>

#include <string>

namespace VLMS::Database {
class SqliteStatement;
}  // namespace VLMS::Database

namespace VLMS::Repositories::BookSql {

[[nodiscard]] std::string filterClause(const BookQuery& query);
void bindFilters(Database::SqliteStatement& query, const BookQuery& queryData);
/// The copy number a numeric search matched, as a SELECT column. The literal
/// '' when the search is not a number, so the column list keeps a fixed shape.
[[nodiscard]] std::string matchedLocalIdColumn(const BookQuery& query);
[[nodiscard]] std::string orderExpressions(const BookQuery& query);
[[nodiscard]] std::string orderClause(const BookQuery& query);

}  // namespace VLMS::Repositories::BookSql
