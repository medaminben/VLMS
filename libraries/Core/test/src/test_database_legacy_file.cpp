#include "SqliteSession.h"

#include <VLMS/Core/Database.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

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

std::vector<fs::path> supersededCopies(const fs::path& dir)
{
    std::vector<fs::path> found;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().filename().string().rfind("vlms.db.superseded-", 0) == 0) {
            found.push_back(entry.path());
        }
    }
    return found;
}

}  // namespace

/**
 * Builds released before the rename to VLMS stored their data in
 * klms_lite.db. Database::open() carries that file over to vlms.db, and must
 * prefer it over a vlms.db the installer may have dropped beside it.
 */
class test_core_DatabaseLegacyFile : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_dir = fs::temp_directory_path()
            / ("vlms-legacy-file-" + std::to_string(stamp));
        fs::create_directories(m_dir);
    }

    void TearDown() override
    {
        std::error_code ignored;
        fs::remove_all(m_dir, ignored);
    }

    fs::path m_dir;
};

TEST_F(test_core_DatabaseLegacyFile, LegacyDatabaseFileIsRenamed)
{
    writeMarkerDatabase(m_dir / "klms_lite.db", "live");

    {
        Database database(m_dir.string());
        ASSERT_TRUE(database.open());
    }

    EXPECT_FALSE(fs::exists(m_dir / "klms_lite.db"));
    EXPECT_EQ(readMarker(m_dir / "vlms.db"), "live");
}

TEST_F(test_core_DatabaseLegacyFile, LegacyDatabaseFileWinsOverABundledOne)
{
    writeMarkerDatabase(m_dir / "klms_lite.db", "live");
    writeMarkerDatabase(m_dir / "vlms.db", "bundled");

    {
        Database database(m_dir.string());
        ASSERT_TRUE(database.open());
    }

    EXPECT_EQ(readMarker(m_dir / "vlms.db"), "live");
    const auto aside = supersededCopies(m_dir);
    ASSERT_EQ(aside.size(), 1u);
    EXPECT_EQ(readMarker(aside.front()), "bundled");
}

TEST_F(test_core_DatabaseLegacyFile, NoLegacyFileLeavesTheDatabaseAlone)
{
    writeMarkerDatabase(m_dir / "vlms.db", "current");

    {
        Database database(m_dir.string());
        ASSERT_TRUE(database.open());
    }

    EXPECT_EQ(readMarker(m_dir / "vlms.db"), "current");
    EXPECT_TRUE(supersededCopies(m_dir).empty());
}
