#pragma once

#include <VLMS/Core/Result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace VLMS {
class SqliteSession;
}

class NamedEntityStore {
public:
    enum class Kind { Author, Publisher };

    explicit NamedEntityStore(VLMS::SqliteSession& session);

    [[nodiscard]] VLMS::Result<std::int64_t> findOrCreate(Kind kind,
                                                                const std::string& name) const;
    [[nodiscard]] VLMS::Result<std::vector<std::string>> listNames(Kind kind) const;

private:
    [[nodiscard]] static const char* tableName(Kind kind);

    VLMS::SqliteSession& m_session;
};
