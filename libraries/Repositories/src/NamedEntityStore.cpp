#include "NamedEntityStore.h"

#include "RepoSql.h"
#include <VLMS/Database/SqliteSession.h>
#include <VLMS/Core/Text.h>

using VLMS::Result;
using VLMS::trim;

namespace VLMS::Repositories {

NamedEntityStore::NamedEntityStore(Database::SqliteSession& session)
    : m_session(session)
{
}

const char* NamedEntityStore::tableName(const Kind kind)
{
    switch (kind) {
    case Kind::Author:
        return "authors";
    case Kind::Publisher:
        return "publishers";
    }
    return "authors";
}

Result<std::int64_t> NamedEntityStore::findOrCreate(const Kind kind, const std::string& name) const
{
    const std::string trimmed = trim(name);
    if (trimmed.empty()) {
        return Result<std::int64_t>::ok(0);
    }

    const std::string table = tableName(kind);
    auto find = m_session.prepare("SELECT id FROM " + table + " WHERE name = :name LIMIT 1");
    if (!find) {
        return RepoSql::sqlResult<std::int64_t>(find.error().detail);
    }
    if (!find->bind(":name", trimmed)) {
        return RepoSql::sqlResult<std::int64_t>(m_session.lastError());
    }
    if (find->next()) {
        return Result<std::int64_t>::ok(find->int64(0));
    }

    auto insert = m_session.prepare("INSERT INTO " + table + " (name) VALUES (:name)");
    if (!insert) {
        return RepoSql::sqlResult<std::int64_t>(insert.error().detail);
    }
    if (!insert->bind(":name", trimmed) || !insert->exec()) {
        return RepoSql::sqlResult<std::int64_t>(m_session.lastError());
    }
    return Result<std::int64_t>::ok(m_session.lastInsertRowId());
}

Result<std::vector<std::string>> NamedEntityStore::listNames(const Kind kind) const
{
    auto q = m_session.prepare(std::string("SELECT name FROM ") + tableName(kind)
                               + " ORDER BY name COLLATE NOCASE");
    if (!q) {
        return RepoSql::sqlResult<std::vector<std::string>>(q.error().detail);
    }
    std::vector<std::string> names;
    while (q->next()) {
        names.push_back(q->text(0));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<std::string>>(m_session.lastError());
    }
    return Result<std::vector<std::string>>::ok(std::move(names));
}

}  // namespace VLMS::Repositories
