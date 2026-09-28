#include <VLMS/Database/SqliteSession.h>

#include <VLMS/Database/Database.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

using VLMS::SqliteSession;

namespace fs = std::filesystem;

namespace {

/// A bare SQLite file holding one marker value, so a test can tell afterwards
/// which physical file ended up under which name.
void writeMarkerDatabase(const fs::path& path, const std::string& value)
{
    auto opened = SqliteSession::open(path.string());
    ASSERT_TRUE(opened) << opened.error().detail;
    ASSERT_TRUE(opened.value()->exec("CREATE TABLE marker (value TEXT)"));
    ASSERT_TRUE(opened.value()->exec("INSERT INTO marker (value) VALUES ('" + value + "')"));
}

std::string readMarker(const fs::path& path)
{
    auto opened = SqliteSession::open(path.string());
    if (!opened) {
        return {};
    }
    auto stmt = opened.value()->prepare("SELECT value FROM marker");
    if (!stmt || !stmt.value().next()) {
        return {};
    }
    return stmt.value().text(0);
}

}  // namespace

/**
 * Database::open() works on vlms.db and nothing else. Another SQLite file in
 * the same directory is never renamed, moved aside or read.
 */
class test_core_DatabaseFile : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_dir = fs::temp_directory_path()
            / ("vlms-database-file-" + std::to_string(stamp));
        fs::create_directories(m_dir);
    }

    void TearDown() override
    {
        std::error_code ignored;
        fs::remove_all(m_dir, ignored);
    }

    fs::path m_dir;
};

TEST_F(test_core_DatabaseFile, AnotherDatabaseBesideItIsLeftAlone)
{
    writeMarkerDatabase(m_dir / "vlms.db", "current");
    writeMarkerDatabase(m_dir / "other.db", "other");

    {
        Database database(m_dir.string());
        ASSERT_TRUE(database.open());
    }

    EXPECT_EQ(readMarker(m_dir / "vlms.db"), "current");
    EXPECT_EQ(readMarker(m_dir / "other.db"), "other");
}

TEST_F(test_core_DatabaseFile, AnotherDatabaseAloneDoesNotBecomeVlmsDb)
{
    writeMarkerDatabase(m_dir / "other.db", "other");

    {
        Database database(m_dir.string());
        ASSERT_TRUE(database.open());
    }

    EXPECT_EQ(readMarker(m_dir / "vlms.db"), "");
    EXPECT_EQ(readMarker(m_dir / "other.db"), "other");
}
