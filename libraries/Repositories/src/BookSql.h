#pragma once

#include <VLMS/Repositories/CatalogTypes.h>

#include <string>

namespace VLMS {
class SqliteStatement;
}

namespace VLMS::Repositories::BookSql {

[[nodiscard]] std::string filterClause(const BookQuery& query);
void bindFilters(SqliteStatement& query, const BookQuery& queryData);
/// The copy number a numeric search matched, as a SELECT column. The literal
/// '' when the search is not a number, so the column list keeps a fixed shape.
[[nodiscard]] std::string matchedLocalIdColumn(const BookQuery& query);
[[nodiscard]] std::string orderExpressions(const BookQuery& query);
[[nodiscard]] std::string orderClause(const BookQuery& query);

}  // namespace VLMS::Repositories::BookSql
