#pragma once

#include <memory>
#include <string>
#include <vector>

namespace VLMS::Database {

class SqliteSession;

class Connection final {
public:
    static constexpr int kSchemaVersion = 7;

    explicit Connection(std::string dataDirectory);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    bool open();
    [[nodiscard]] const std::string& dataDirectory() const { return m_dataDirectory; }
    [[nodiscard]] std::string databasePath() const;
    [[nodiscard]] SqliteSession& session() const;
    [[nodiscard]] int schemaVersion() const;
    [[nodiscard]] const std::string& lastError() const { return m_lastError; }

private:
    bool applySchema();
    bool migrateLegacyShapesIfNeeded();
    bool migrateCatalogIfNeeded();
    bool migrateBookLanguageIfNeeded();
    bool migrateBookDescriptionIfNeeded();
    bool migrateMemberSexIfNeeded();
    bool migrateMemberEmailIfNeeded();
    bool migrateMemberArchivedIfNeeded();
    bool migrateMemberSpreadsheetColumnsIfNeeded();
    bool migrateArchiveColumnsIfNeeded();
    bool migrateMemberActiveUntilIfNeeded();
    bool upgradeSchemaIfNeeded();
    bool migrateDateConstraintsIfNeeded(bool* applied);
    [[nodiscard]] int countRowsBlockingDateConstraints() const;
    bool rebuildTablesWithDateConstraints();
    bool migratePublicationDatesIfNeeded();
    bool normalizePublicationDatesFromOriginals();
    bool setSchemaVersion(int version);
    bool ensureDefaultEmployee();
    bool execAll(const std::vector<std::string>& statements, const std::string& context);
    bool execSqlScript(const std::string& script, const std::string& context);
    bool tableHasColumn(const std::string& table, const std::string& column) const;
    bool hasAnyTable() const;
    void warn(const std::string& message) const;

    std::string m_dataDirectory;
    std::string m_lastError;
    std::unique_ptr<SqliteSession> m_session;
};

}  // namespace VLMS::Database
