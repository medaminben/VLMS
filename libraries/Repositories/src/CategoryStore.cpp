#include "CategoryStore.h"

#include "RepoSql.h"
#include <VLMS/Database/SqliteSession.h>
#include <VLMS/Core/Text.h>

using VLMS::Result;
using VLMS::Status;
using VLMS::trim;
namespace RepoSql = VLMS::RepoSql;

namespace {

CategoryRecord readCategoryRow(VLMS::SqliteStatement& q)
{
    CategoryRecord category;
    category.id = q.int64(0);
    category.code = q.text(1);
    category.label = q.text(2);
    category.bookCount = q.integer(3);
    return category;
}

}  // namespace

CategoryStore::CategoryStore(VLMS::SqliteSession& session)
    : m_session(session)
{
}

Result<std::vector<CategoryRecord>> CategoryStore::listCategories() const
{
    auto q = m_session.prepare(R"SQL(
        SELECT
            c.id,
            c.code,
            COALESCE(NULLIF(TRIM(c.label), ''), c.code) AS label,
            COUNT(b.id) AS book_count
        FROM categories c
        INNER JOIN books b ON b.category_id = c.id
        WHERE LENGTH(TRIM(b.title)) > 0 AND b.archived_at IS NULL
        GROUP BY c.id
        HAVING COUNT(b.id) > 0
        ORDER BY COUNT(b.id) DESC, c.code COLLATE NOCASE
    )SQL");
    if (!q) {
        return RepoSql::sqlResult<std::vector<CategoryRecord>>(q.error().detail);
    }
    std::vector<CategoryRecord> categories;
    while (q->next()) {
        categories.push_back(readCategoryRow(*q));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<CategoryRecord>>(m_session.lastError());
    }
    return Result<std::vector<CategoryRecord>>::ok(std::move(categories));
}

Result<std::vector<CategoryRecord>> CategoryStore::listAllCategories() const
{
    auto q = m_session.prepare(R"SQL(
        SELECT
            c.id,
            c.code,
            COALESCE(NULLIF(TRIM(c.label), ''), c.code) AS label,
            COUNT(b.id) AS book_count
        FROM categories c
        LEFT JOIN books b ON b.category_id = c.id
        GROUP BY c.id
        ORDER BY c.code COLLATE NOCASE
    )SQL");
    if (!q) {
        return RepoSql::sqlResult<std::vector<CategoryRecord>>(q.error().detail);
    }
    std::vector<CategoryRecord> categories;
    while (q->next()) {
        categories.push_back(readCategoryRow(*q));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<CategoryRecord>>(m_session.lastError());
    }
    return Result<std::vector<CategoryRecord>>::ok(std::move(categories));
}

Result<std::int64_t> CategoryStore::createCategory(const std::string& code, const std::string& label)
{
    const std::string trimmedCode = trim(code);
    if (trimmedCode.empty()) {
        return RepoSql::validationResult<std::int64_t>("error.category.codeRequired");
    }
    auto insert = m_session.prepare("INSERT INTO categories (code, label) VALUES (:code, :label)");
    if (!insert) {
        return RepoSql::sqlResult<std::int64_t>(insert.error().detail);
    }
    if (!insert->bind(":code", trimmedCode) || !insert->bind(":label", trim(label))
        || !insert->exec()) {
        return RepoSql::sqlResult<std::int64_t>(m_session.lastError());
    }
    return Result<std::int64_t>::ok(m_session.lastInsertRowId());
}

Status CategoryStore::updateCategory(const std::int64_t id,
                                     const std::string& code,
                                     const std::string& label)
{
    const std::string trimmedCode = trim(code);
    if (trimmedCode.empty()) {
        return RepoSql::validation("error.category.codeRequired");
    }
    auto update =
        m_session.prepare("UPDATE categories SET code = :code, label = :label WHERE id = :id");
    if (!update) {
        return RepoSql::sqlFailure(update.error().detail);
    }
    if (!update->bind(":code", trimmedCode) || !update->bind(":label", trim(label))
        || !update->bind(":id", id) || !update->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (update->changes() <= 0) {
        return RepoSql::notFound("error.category.notFound");
    }
    return Status::ok();
}

Status CategoryStore::deleteCategory(const std::int64_t id)
{
    auto usage = m_session.prepare("SELECT COUNT(*) FROM books WHERE category_id = :id");
    if (!usage) {
        return RepoSql::sqlFailure(usage.error().detail);
    }
    if (!usage->bind(":id", id) || !usage->next()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (usage->integer(0) > 0) {
        return RepoSql::validation("error.category.hasBooks");
    }

    auto remove = m_session.prepare("DELETE FROM categories WHERE id = :id");
    if (!remove) {
        return RepoSql::sqlFailure(remove.error().detail);
    }
    if (!remove->bind(":id", id) || !remove->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (remove->changes() <= 0) {
        return RepoSql::notFound("error.category.notFound");
    }
    return Status::ok();
}
