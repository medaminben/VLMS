#pragma once

#include <VLMS/Core/Result.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace VLMS {

class SqliteSession;

class SqliteStatement {
public:
    SqliteStatement() = default;
    SqliteStatement(sqlite3* db, sqlite3_stmt* stmt);
    SqliteStatement(SqliteStatement&& other) noexcept;
    SqliteStatement& operator=(SqliteStatement&& other) noexcept;
    ~SqliteStatement();

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    [[nodiscard]] bool valid() const { return m_stmt != nullptr; }

    Status bind(int index, std::int64_t value);
    Status bind(int index, std::string_view value);
    Status bindNull(int index);
    Status bind(const std::string& name, std::int64_t value);
    Status bind(const std::string& name, std::string_view value);
    Status bindNull(const std::string& name);
    Status bindOptional(const std::string& name, const std::optional<std::string>& value);

    /// INSERT/UPDATE/DELETE: step once and expect DONE.
    Status exec();
    /// SELECT: true when a row is ready. False means done or error; check ok().
    bool next();
    [[nodiscard]] bool ok() const { return m_ok; }
    [[nodiscard]] int changes() const;

    [[nodiscard]] bool isNull(int column) const;
    [[nodiscard]] std::int64_t int64(int column) const;
    [[nodiscard]] int integer(int column) const;
    [[nodiscard]] std::string text(int column) const;

    void reset();

private:
    Status bindIndex(const std::string& name, int* index);
    void captureError();

    sqlite3* m_db = nullptr;
    sqlite3_stmt* m_stmt = nullptr;
    bool m_ok = false;
    std::string m_error;
};

class SqliteSession {
public:
    static Result<std::unique_ptr<SqliteSession>> open(const std::string& path);
    ~SqliteSession();

    SqliteSession(const SqliteSession&) = delete;
    SqliteSession& operator=(const SqliteSession&) = delete;

    Status exec(const std::string& sql);
    Result<SqliteStatement> prepare(const std::string& sql);
    Status transaction(const std::function<Status()>& work);

    [[nodiscard]] std::int64_t lastInsertRowId() const;
    [[nodiscard]] const std::string& lastError() const { return m_error; }
    [[nodiscard]] sqlite3* handle() const { return m_db; }

private:
    explicit SqliteSession(sqlite3* db);
    void captureError();

    sqlite3* m_db = nullptr;
    std::string m_error;
};

}  // namespace VLMS
