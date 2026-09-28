#include <VLMS/Database/SqliteSession.h>

#ifdef VLMS_HAS_SQLITE3_H
#include <sqlite3.h>
#else
#include "sqlite3_api.h"
#endif

#include <utility>

namespace VLMS::Database {

namespace {

void* sqliteTransient()
{
    return reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));
}

}  // namespace

SqliteStatement::SqliteStatement(sqlite3* db, sqlite3_stmt* stmt)
    : m_db(db), m_stmt(stmt), m_ok(stmt != nullptr)
{
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : m_db(other.m_db),
      m_stmt(other.m_stmt),
      m_ok(other.m_ok),
      m_error(std::move(other.m_error))
{
    other.m_db = nullptr;
    other.m_stmt = nullptr;
    other.m_ok = false;
}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept
{
    if (this != &other) {
        if (m_stmt != nullptr) {
            sqlite3_finalize(m_stmt);
        }
        m_db = other.m_db;
        m_stmt = other.m_stmt;
        m_ok = other.m_ok;
        m_error = std::move(other.m_error);
        other.m_db = nullptr;
        other.m_stmt = nullptr;
        other.m_ok = false;
    }
    return *this;
}

SqliteStatement::~SqliteStatement()
{
    if (m_stmt != nullptr) {
        sqlite3_finalize(m_stmt);
    }
}

void SqliteStatement::captureError()
{
    m_ok = false;
    m_error = m_db != nullptr ? sqlite3_errmsg(m_db) : "no sqlite connection";
}

Status SqliteStatement::bindIndex(const std::string& name, int* index)
{
    *index = sqlite3_bind_parameter_index(m_stmt, name.c_str());
    if (*index == 0) {
        m_ok = false;
        m_error = "unknown bind name: " + name;
        return Status::fail(ErrorKind::Sql, "error.sql", m_error);
    }
    return Status::ok();
}

Status SqliteStatement::bind(int index, std::int64_t value)
{
    if (sqlite3_bind_int64(m_stmt, index, value) != SQLITE_OK) {
        captureError();
        return Status::fail(ErrorKind::Sql, "error.sql", m_error);
    }
    return Status::ok();
}

Status SqliteStatement::bind(int index, std::string_view value)
{
    if (sqlite3_bind_text(m_stmt,
                          index,
                          value.data(),
                          static_cast<int>(value.size()),
                          reinterpret_cast<void (*)(void*)>(sqliteTransient()))
        != SQLITE_OK) {
        captureError();
        return Status::fail(ErrorKind::Sql, "error.sql", m_error);
    }
    return Status::ok();
}

Status SqliteStatement::bindNull(int index)
{
    if (sqlite3_bind_null(m_stmt, index) != SQLITE_OK) {
        captureError();
        return Status::fail(ErrorKind::Sql, "error.sql", m_error);
    }
    return Status::ok();
}

Status SqliteStatement::bind(const std::string& name, std::int64_t value)
{
    int index = 0;
    if (const Status ready = bindIndex(name, &index); !ready) {
        return ready;
    }
    return bind(index, value);
}

Status SqliteStatement::bind(const std::string& name, std::string_view value)
{
    int index = 0;
    if (const Status ready = bindIndex(name, &index); !ready) {
        return ready;
    }
    return bind(index, value);
}

Status SqliteStatement::bindNull(const std::string& name)
{
    int index = 0;
    if (const Status ready = bindIndex(name, &index); !ready) {
        return ready;
    }
    return bindNull(index);
}

Status SqliteStatement::bindOptional(const std::string& name,
                                     const std::optional<std::string>& value)
{
    if (!value.has_value()) {
        return bindNull(name);
    }
    return bind(name, *value);
}

Status SqliteStatement::exec()
{
    const int rc = sqlite3_step(m_stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        captureError();
        return Status::fail(ErrorKind::Sql, "error.sql", m_error);
    }
    sqlite3_reset(m_stmt);
    return Status::ok();
}

bool SqliteStatement::next()
{
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    captureError();
    return false;
}

int SqliteStatement::changes() const
{
    return m_db != nullptr ? sqlite3_changes(m_db) : 0;
}

bool SqliteStatement::isNull(int column) const
{
    return sqlite3_column_type(m_stmt, column) == SQLITE_NULL;
}

std::int64_t SqliteStatement::int64(int column) const
{
    return sqlite3_column_int64(m_stmt, column);
}

int SqliteStatement::integer(int column) const
{
    return sqlite3_column_int(m_stmt, column);
}

std::string SqliteStatement::text(int column) const
{
    const unsigned char* bytes = sqlite3_column_text(m_stmt, column);
    if (bytes == nullptr) {
        return {};
    }
    const int size = sqlite3_column_bytes(m_stmt, column);
    return {reinterpret_cast<const char*>(bytes), static_cast<std::size_t>(size)};
}

void SqliteStatement::reset()
{
    if (m_stmt != nullptr) {
        sqlite3_reset(m_stmt);
    }
    m_ok = true;
    m_error.clear();
}

SqliteSession::SqliteSession(sqlite3* db)
    : m_db(db)
{
}

SqliteSession::~SqliteSession()
{
    if (m_db != nullptr) {
        sqlite3_close(m_db);
    }
}

void SqliteSession::captureError()
{
    m_error = m_db != nullptr ? sqlite3_errmsg(m_db) : "no sqlite connection";
}

Result<std::unique_ptr<SqliteSession>> SqliteSession::open(const std::string& path)
{
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        const std::string message = db != nullptr ? sqlite3_errmsg(db) : "sqlite3_open failed";
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return Result<std::unique_ptr<SqliteSession>>::fail(ErrorKind::Sql, "error.sql", message);
    }
    return Result<std::unique_ptr<SqliteSession>>::ok(
        std::unique_ptr<SqliteSession>(new SqliteSession(db)));
}

Status SqliteSession::exec(const std::string& sql)
{
    char* error = nullptr;
    if (sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        m_error = error != nullptr ? error : sqlite3_errmsg(m_db);
        sqlite3_free(error);
        return Status::fail(ErrorKind::Sql, "error.sql", m_error);
    }
    return Status::ok();
}

Result<SqliteStatement> SqliteSession::prepare(const std::string& sql)
{
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        captureError();
        return Result<SqliteStatement>::fail(ErrorKind::Sql, "error.sql", m_error);
    }
    return Result<SqliteStatement>::ok(SqliteStatement(m_db, stmt));
}

Status SqliteSession::transaction(const std::function<Status()>& work)
{
    if (const Status begin = exec("BEGIN"); !begin) {
        return begin;
    }
    const Status result = work();
    if (result) {
        if (const Status commit = exec("COMMIT"); !commit) {
            exec("ROLLBACK");
            return commit;
        }
        return result;
    }
    exec("ROLLBACK");
    return result;
}

std::int64_t SqliteSession::lastInsertRowId() const
{
    return sqlite3_last_insert_rowid(m_db);
}

}  // namespace VLMS::Database
