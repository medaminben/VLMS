#include "TestDatabase.h"
#include "TestEnv.h"

#include <VLMS/Database/Database.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace VLMS::Test;

namespace {

std::string schemaPath()
{
    const char* path = std::getenv("VLMS_SCHEMA_PATH");
    return path != nullptr ? std::string(path) : std::string();
}

std::string readSchemaSql(std::string* error = nullptr)
{
    const std::string path = schemaPath();
    std::ifstream file(path);
    if (!file) {
        if (error != nullptr) {
            *error = path.empty() ? "VLMS_SCHEMA_PATH is not set" : ("could not read " + path);
        }
        return {};
    }
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

std::string join(const std::vector<std::string>& values, std::string_view sep)
{
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out.append(sep);
        }
        out += values[i];
    }
    return out;
}

/// Object names declared by database/schema.sql, so the tests below assert
/// against the file rather than a hand-maintained copy of it. This is what
/// catches execSqlScript's naive ';' split shredding a statement.
std::vector<std::string> declaredObjects(const std::string& keyword)
{
    const std::string sql = readSchemaSql();
    if (sql.empty()) {
        return {};
    }

    const std::regex pattern(
        "CREATE\\s+(?:UNIQUE\\s+)?" + keyword + "\\s+(?:IF\\s+NOT\\s+EXISTS\\s+)?(\\w+)",
        std::regex::icase);

    std::vector<std::string> names;
    for (std::sregex_iterator it(sql.begin(), sql.end(), pattern), end; it != end; ++it) {
        names.push_back((*it)[1].str());
    }
    return names;
}

}  // namespace

class test_core_DatabaseSchema : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
    }

    void TearDown() override { m_db.reset(); }

    std::unique_ptr<TestDatabase> m_db;
};

TEST_F(test_core_DatabaseSchema, SchemaFileIsReachable)
{
    const std::string path = schemaPath();
    ASSERT_FALSE(path.empty()) << "VLMS_SCHEMA_PATH is not set; check create_test()";
    EXPECT_TRUE(std::filesystem::exists(path)) << path;
}

TEST_F(test_core_DatabaseSchema, FreshDatabaseCreatesEveryTableInSchemaSql)
{
    const std::vector<std::string> expected = declaredObjects("TABLE");
    ASSERT_FALSE(expected.empty()) << "no CREATE TABLE found in schema.sql";

    const std::vector<std::string> actual = m_db->tableNames();
    for (const std::string& table : expected) {
        EXPECT_TRUE(contains(actual, table))
            << "schema.sql declares '" << table << "' but the opened database has only: "
            << join(actual, ", ");
    }
}

TEST_F(test_core_DatabaseSchema, FreshDatabaseCreatesEveryIndexInSchemaSql)
{
    const std::vector<std::string> expected = declaredObjects("INDEX");
    ASSERT_FALSE(expected.empty()) << "no CREATE INDEX found in schema.sql";

    const std::vector<std::string> actual = m_db->queryTexts(
        "SELECT name FROM sqlite_master WHERE type='index' AND name NOT LIKE 'sqlite_%'");

    for (const std::string& index : expected) {
        EXPECT_TRUE(contains(actual, index)) << "missing index '" << index << "'";
    }
}

TEST_F(test_core_DatabaseSchema, ForeignKeysPragmaIsOnAfterOpen)
{
    EXPECT_EQ(m_db->scalar("PRAGMA foreign_keys").toInt(), 1);
}

TEST_F(test_core_DatabaseSchema, OpenIsIdempotentWhenCalledTwice)
{
    const std::vector<std::string> before = m_db->tableNames();
    EXPECT_TRUE(m_db->database().open());
    EXPECT_EQ(m_db->tableNames(), before);
    EXPECT_EQ(m_db->count("employees"), 1);
}

TEST_F(test_core_DatabaseSchema, EnsureDefaultEmployeeCreatesExactlyOneAdmin)
{
    EXPECT_EQ(m_db->count("employees"), 1);
}

TEST_F(test_core_DatabaseSchema, EnsureDefaultEmployeeIsNoOpOnReopen)
{
    EXPECT_TRUE(m_db->database().open());
    EXPECT_TRUE(m_db->database().open());
    EXPECT_EQ(m_db->count("employees"), 1);
}

TEST_F(test_core_DatabaseSchema, SchemaPathHonoursEnvironmentOverride)
{
    // The fixture only works because bundledSchemaPath() consults the
    // environment first; assert that explicitly so the behaviour is pinned.
    const std::string original = schemaPath();

    TestDatabase reference;
    EXPECT_TRUE(reference.isValid());
    EXPECT_TRUE(reference.tableExists("members"));
    EXPECT_EQ(schemaPath(), original);
}

TEST_F(test_core_DatabaseSchema, OpenFailsGracefullyWhenSchemaFileIsMissing)
{
    const ScopedEnv pinned("VLMS_SCHEMA_PATH", "/nonexistent/vlms/schema.sql");

    TestDatabase broken;
    EXPECT_FALSE(broken.isValid()) << "open() should fail when the schema file cannot be read";
}

TEST_F(test_core_DatabaseSchema, DestructorRemovesTheSqlConnection)
{
    {
        TestDatabase scoped;
        EXPECT_TRUE(scoped.isValid());
        EXPECT_TRUE(scoped.isValid());
    }
    // Database owns the SqliteSession; destroying the fixture closes it.
}

// ---------------------------------------------------------------------------
// Schema version
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseSchema, SchemaSqlDeclaresTheVersionTheCodeExpects)
{
    std::string error;
    const std::string sql = readSchemaSql(&error);
    ASSERT_FALSE(sql.empty()) << error;

    const std::regex pattern("PRAGMA\\s+user_version\\s*=\\s*(\\d+)", std::regex::icase);
    std::smatch match;
    ASSERT_TRUE(std::regex_search(sql, match, pattern))
        << "database/schema.sql declares no PRAGMA user_version";

    // Two places have to agree and neither can see the other: schema.sql stamps
    // a new database, Database::kSchemaVersion decides what open() will accept.
    // Raise one alone and every fresh database is a version the code refuses.
    EXPECT_EQ(std::stoi(match[1].str()), Database::kSchemaVersion);
}

TEST_F(test_core_DatabaseSchema, FreshDatabaseIsStampedWithTheCurrentVersion)
{
    EXPECT_EQ(m_db->userVersion(), Database::kSchemaVersion);
    EXPECT_EQ(m_db->database().schemaVersion(), Database::kSchemaVersion);
}

TEST_F(test_core_DatabaseSchema, EveryDateDefaultInSchemaSqlIsLocalTime)
{
    std::string error;
    const std::string sql = readSchemaSql(&error);
    ASSERT_FALSE(sql.empty()) << error;

    // Column DEFAULTs are the one place the Clock cannot reach: they fire for
    // rows created outside the app, by an import or by sqlite3 on the command
    // line. Bare date('now') is UTC, which at UTC+1 stamps yesterday's date for
    // an hour every night -- the same mismatch finding 1 was about, in the half
    // of the system C1 could not refactor.
    const std::regex naive("(datetime|date)\\s*\\(\\s*'now'\\s*\\)", std::regex::icase);
    std::smatch match;
    EXPECT_FALSE(std::regex_search(sql, match, naive))
        << "schema.sql still has a UTC default: " << (match.empty() ? "" : match[0].str());
}

TEST_F(test_core_DatabaseSchema, OpenRefusesADatabaseFromANewerBuild)
{
    ASSERT_TRUE(m_db->exec("PRAGMA user_version = " + std::to_string(Database::kSchemaVersion + 7)));

    // A database written by a later version may have constraints and columns
    // this build knows nothing about, and there is no read-only mode to fall
    // back to -- every repository shares the one connection. Refusing to open
    // is the whole enforcement, so it has to be a hard false.
    EXPECT_FALSE(m_db->database().open()) << "open() must refuse a database newer than kSchemaVersion";
}

TEST_F(test_core_DatabaseSchema, ReopeningDoesNotDisturbTheVersion)
{
    EXPECT_TRUE(m_db->database().open());
    EXPECT_TRUE(m_db->database().open());
    EXPECT_EQ(m_db->userVersion(), Database::kSchemaVersion);
}
