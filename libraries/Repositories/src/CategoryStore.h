#pragma once

#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Core/Result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace VLMS::Database {
class SqliteSession;
}  // namespace VLMS::Database

namespace VLMS::Repositories {

class CategoryStore {
public:
    explicit CategoryStore(Database::SqliteSession& session);

    [[nodiscard]] VLMS::Result<std::vector<CategoryRecord>> listCategories() const;
    [[nodiscard]] VLMS::Result<std::vector<CategoryRecord>> listAllCategories() const;
    [[nodiscard]] VLMS::Result<std::int64_t> createCategory(const std::string& code,
                                                                  const std::string& label);
    [[nodiscard]] VLMS::Status updateCategory(std::int64_t id,
                                                    const std::string& code,
                                                    const std::string& label);
    [[nodiscard]] VLMS::Status deleteCategory(std::int64_t id);

private:
    Database::SqliteSession& m_session;
};

}  // namespace VLMS::Repositories
