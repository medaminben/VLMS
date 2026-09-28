#include "TestDatabase.h"

#include <VLMS/Database/SqlText.h>

#include <VLMS/Database/SqliteSession.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Test {

namespace {

std::filesystem::path makeTempRoot()
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    static int counter = 0;
    std::filesystem::path root = std::filesystem::temp_directory_path()
        / ("vlms_test_" + std::to_string(stamp) + "_" + std::to_string(++counter));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    return error ? std::filesystem::path{} : root;
}

bool bindValue(VLMS::SqliteStatement& stmt, const std::string& key, const SqlValue& value)
{
    const std::string name = (!key.empty() && key.front() == ':') ? key : (":" + key);
    if (value.isNull()) {
        return static_cast<bool>(stmt.bindNull(name));
    }
    if (value.kind() == SqlValue::Kind::Integer) {
        return static_cast<bool>(stmt.bind(name, value.toInt64()));
    }
    return static_cast<bool>(stmt.bind(name, value.toString()));
}

bool runScript(VLMS::SqliteSession& session, const std::string& script, std::string* error)
{
    for (const std::string& statement : VLMS::SqlText::splitStatements(script)) {
        if (!session.exec(statement)) {
            if (error != nullptr) {
                *error = session.lastError() + "\nSQL: " + statement.substr(0, 200);
            }
            return false;
        }
    }
    return true;
}

std::string readFile(const std::string& path, std::string* error)
{
    std::ifstream file(path);
    if (!file) {
        if (error != nullptr) {
            *error = "Could not read fixture script " + path;
        }
        return {};
    }
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

}  // namespace

TestDatabase::TestDatabase(Mode mode, std::string sourcePath)
{
    m_root = makeTempRoot();
    if (m_root.empty()) {
        m_lastError = "Could not create a temporary directory.";
        return;
    }

    std::error_code error;
    std::filesystem::create_directories(m_root / "database", error);
    std::filesystem::create_directories(m_root / "resources" / "books", error);
    std::filesystem::create_directories(m_root / "resources" / "members", error);
    if (error) {
        m_lastError = "Could not create the temporary data layout.";
        return;
    }

    switch (mode) {
    case Mode::FreshSchema:
        break;
    case Mode::FromSqlFile:
        if (!materialiseFromSqlFile(sourcePath)) {
            return;
        }
        break;
    case Mode::FromCopyOf:
        if (!materialiseFromCopy(sourcePath)) {
            return;
        }
        break;
    }

    m_database = std::make_unique<Database>(dataDirectory());
    m_opened = m_database->open();
    if (!m_opened) {
        m_lastError = "Database::open() failed for " + databasePath();
    }
}

TestDatabase::~TestDatabase()
{
    m_database.reset();
    if (!m_root.empty()) {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }
}

VLMS::SqliteSession& TestDatabase::session() const
{
    return m_database->session();
}

std::string TestDatabase::dataDirectory() const
{
    return (m_root / "database").string();
}

std::string TestDatabase::resourcesDirectory() const
{
    return (m_root / "resources").string();
}

std::string TestDatabase::databasePath() const
{
    return (m_root / "database" / "vlms.db").string();
}

std::string TestDatabase::dataFile(std::string_view name)
{
    const char* dir = std::getenv("VLMS_TEST_DATA_DIR");
    std::filesystem::path base = dir != nullptr ? dir : std::filesystem::path{};
    return (base / std::string(name)).string();
}

bool TestDatabase::materialiseFromSqlFile(const std::string& scriptPath)
{
    const std::string script = readFile(scriptPath, &m_lastError);
    if (script.empty() && !m_lastError.empty()) {
        return false;
    }

    auto opened = VLMS::SqliteSession::open(databasePath());
    if (!opened) {
        m_lastError = "Could not open the seed database: " + opened.error().detail;
        return false;
    }
    if (!opened.value()->exec("PRAGMA foreign_keys = ON")) {
        m_lastError = opened.value()->lastError();
        return false;
    }
    return runScript(*opened.value(), script, &m_lastError);
}

bool TestDatabase::materialiseFromCopy(const std::string& sourceDbPath)
{
    std::error_code error;
    if (!std::filesystem::exists(sourceDbPath, error)) {
        m_lastError = "Source database not found: " + sourceDbPath;
        return false;
    }
    std::filesystem::copy_file(sourceDbPath, databasePath(),
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        m_lastError = "Could not copy " + sourceDbPath;
        return false;
    }
    std::filesystem::permissions(databasePath(),
                                 std::filesystem::perms::owner_read
                                     | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add, error);
    return true;
}

bool TestDatabase::exec(const std::string& sql) const
{
    return execBound(sql, {});
}

bool TestDatabase::execBound(const std::string& sql, const SqlBinds& binds) const
{
    auto query = m_database->session().prepare(sql);
    if (!query) {
        m_lastError = query.error().detail;
        return false;
    }
    for (const SqlBind& bind : binds) {
        if (!bindValue(*query, bind.first, bind.second)) {
            m_lastError = m_database->session().lastError();
            return false;
        }
    }
    if (!query->exec()) {
        m_lastError = query.error().detail;
        return false;
    }
    return true;
}

SqlValue TestDatabase::scalar(const std::string& sql, const SqlBinds& binds) const
{
    auto query = m_database->session().prepare(sql);
    if (!query) {
        m_lastError = query.error().detail;
        return {};
    }
    for (const SqlBind& bind : binds) {
        if (!bindValue(*query, bind.first, bind.second)) {
            m_lastError = m_database->session().lastError();
            return {};
        }
    }
    if (!query->next()) {
        m_lastError = m_database->session().lastError();
        return {};
    }
    if (query->isNull(0)) {
        return {};
    }
    return SqlValue(query->text(0));
}

int TestDatabase::count(std::string_view table) const
{
    return scalar("SELECT COUNT(*) FROM " + std::string(table)).toInt();
}

std::vector<std::string> TestDatabase::queryTexts(const std::string& sql) const
{
    auto query = m_database->session().prepare(sql);
    std::vector<std::string> values;
    if (!query) {
        m_lastError = query.error().detail;
        return values;
    }
    while (query->next()) {
        values.push_back(query->text(0));
    }
    return values;
}

std::vector<std::string> TestDatabase::tableNames() const
{
    return queryTexts(
        "SELECT name FROM sqlite_master WHERE type='table' "
        "AND name NOT LIKE 'sqlite_%' ORDER BY name");
}

bool TestDatabase::tableExists(std::string_view name) const
{
    return scalar("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name = :name",
                  {{"name", std::string(name)}})
               .toInt()
        > 0;
}

std::vector<std::string> TestDatabase::columnNames(std::string_view table) const
{
    auto query = m_database->session().prepare("PRAGMA table_info(" + std::string(table) + ")");
    std::vector<std::string> names;
    if (!query) {
        return names;
    }
    while (query->next()) {
        names.push_back(query->text(1));
    }
    return names;
}

int TestDatabase::userVersion() const
{
    return scalar("PRAGMA user_version").toInt();
}

}  // namespace Test
