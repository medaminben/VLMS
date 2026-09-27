#pragma once

#include <VLMS/Core/CatalogTypes.h>
#include <VLMS/Core/Result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace VLMS {
class SqliteSession;
}

class CategoryStore {
public:
    explicit CategoryStore(VLMS::SqliteSession& session);

    [[nodiscard]] VLMS::Result<std::vector<CategoryRecord>> listCategories() const;
    [[nodiscard]] VLMS::Result<std::vector<CategoryRecord>> listAllCategories() const;
    [[nodiscard]] VLMS::Result<std::int64_t> createCategory(const std::string& code,
                                                                  const std::string& label);
    [[nodiscard]] VLMS::Status updateCategory(std::int64_t id,
                                                    const std::string& code,
                                                    const std::string& label);
    [[nodiscard]] VLMS::Status deleteCategory(std::int64_t id);

private:
    VLMS::SqliteSession& m_session;
};
