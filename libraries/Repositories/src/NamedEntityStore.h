#pragma once

#include <VLMS/Core/Result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace VLMS::Database {
class SqliteSession;
}  // namespace VLMS::Database

namespace VLMS::Repositories {

class NamedEntityStore {
public:
    enum class Kind { Author, Publisher };

    explicit NamedEntityStore(Database::SqliteSession& session);

    [[nodiscard]] Core::Result<std::int64_t> findOrCreate(Kind kind,
                                                                const std::string& name) const;
    [[nodiscard]] Core::Result<std::vector<std::string>> listNames(Kind kind) const;

private:
    [[nodiscard]] static const char* tableName(Kind kind);

    Database::SqliteSession& m_session;
};

}  // namespace VLMS::Repositories
