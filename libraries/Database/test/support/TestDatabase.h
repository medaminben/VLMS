#pragma once

#include "SqlValue.h"

#include <VLMS/Database/Database.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VLMS {
class SqliteSession;
}

namespace VLMS::Test {

/**
 * An isolated SQLite database in a temporary directory.
 *
 * Construction drives the real production path — Database::open() applies
 * database/schema.sql and then runs the whole migrateXIfNeeded chain — so the
 * migration code is exercised by every test that touches a database, not only
 * by the migration suite.
 *
 * Intended lifetime is one test case: hold a unique_ptr member, construct in
 * SetUp, reset in TearDown.
 */
class TestDatabase {
public:
    enum class Mode {
        FreshSchema,  ///< empty directory; open() applies schema.sql
        FromSqlFile,  ///< run a .sql script into the file first, then open()
        FromCopyOf,   ///< copy an existing .db into place, then open()
    };

    explicit TestDatabase(Mode mode = Mode::FreshSchema, std::string sourcePath = {});
    ~TestDatabase();

    TestDatabase(const TestDatabase&) = delete;
    TestDatabase& operator=(const TestDatabase&) = delete;

    [[nodiscard]] bool isValid() const { return m_opened; }
    [[nodiscard]] Database& database() const { return *m_database; }
    [[nodiscard]] VLMS::SqliteSession& session() const;
    [[nodiscard]] std::string rootDirectory() const { return m_root.string(); }
    [[nodiscard]] std::string dataDirectory() const;
    [[nodiscard]] std::string resourcesDirectory() const;
    [[nodiscard]] std::string databasePath() const;

    /// Absolute path of a fixture script under VLMS_TEST_DATA_DIR.
    [[nodiscard]] static std::string dataFile(std::string_view name);

    bool exec(const std::string& sql) const;
    bool execBound(const std::string& sql, const SqlBinds& binds = {}) const;
    [[nodiscard]] SqlValue scalar(const std::string& sql, const SqlBinds& binds = {}) const;
    [[nodiscard]] int count(std::string_view table) const;
    [[nodiscard]] std::vector<std::string> tableNames() const;
    [[nodiscard]] std::vector<std::string> queryTexts(const std::string& sql) const;
    [[nodiscard]] bool tableExists(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> columnNames(std::string_view table) const;
    [[nodiscard]] int userVersion() const;
    [[nodiscard]] const std::string& lastError() const { return m_lastError; }

private:
    bool materialiseFromSqlFile(const std::string& scriptPath);
    bool materialiseFromCopy(const std::string& sourceDbPath);

    std::filesystem::path m_root;
    std::unique_ptr<Database> m_database;
    bool m_opened = false;
    mutable std::string m_lastError;
};

[[nodiscard]] inline bool contains(const std::vector<std::string>& values, std::string_view needle)
{
    for (const std::string& value : values) {
        if (value == needle) {
            return true;
        }
    }
    return false;
}

}  // namespace VLMS::Test
