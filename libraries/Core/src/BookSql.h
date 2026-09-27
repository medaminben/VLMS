#pragma once

#include <VLMS/Core/CatalogTypes.h>

#include <string>

namespace VLMS {
class SqliteStatement;
}

namespace VLMS::BookSql {

[[nodiscard]] std::string filterClause(const BookQuery& query);
void bindFilters(SqliteStatement& query, const BookQuery& queryData);
/// The copy number a numeric search matched, as a SELECT column. The literal
/// '' when the search is not a number, so the column list keeps a fixed shape.
[[nodiscard]] std::string matchedLocalIdColumn(const BookQuery& query);
[[nodiscard]] std::string orderExpressions(const BookQuery& query);
[[nodiscard]] std::string orderClause(const BookQuery& query);

}  // namespace VLMS::BookSql
