#include <VLMS/Database/SqliteSession.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

using namespace VLMS;

using VLMS::ErrorKind;
using VLMS::SqliteSession;

namespace {

class ScopedTempDir {
public:
    ScopedTempDir()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        static int counter = 0;
        m_path = std::filesystem::temp_directory_path()
            / ("vlms_session_" + std::to_string(stamp) + "_" + std::to_string(++counter));
        std::error_code error;
        std::filesystem::create_directories(m_path, error);
        if (error) {
            m_path.clear();
        }
    }

    ~ScopedTempDir()
    {
        if (!m_path.empty()) {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }
    }

    [[nodiscard]] bool isValid() const { return !m_path.empty(); }
    [[nodiscard]] std::string dbPath() const { return (m_path / "t.db").string(); }

private:
    std::filesystem::path m_path;
};

}  // namespace

TEST(test_core_SqliteSession, OpensAFileAndReadsWhatItWrote)
{
    ScopedTempDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string path = dir.dbPath();

    auto opened = SqliteSession::open(path);
    ASSERT_TRUE(opened);
    auto& db = *opened.value();
    ASSERT_TRUE(db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)"));

    auto insert = db.prepare("INSERT INTO t (name) VALUES (:name)");
    ASSERT_TRUE(insert);
    ASSERT_TRUE(insert->bind(":name", std::string_view{"alpha"}));
    ASSERT_TRUE(insert->exec());
    EXPECT_EQ(db.lastInsertRowId(), 1);

    auto select = db.prepare("SELECT id, name FROM t WHERE id = :id");
    ASSERT_TRUE(select);
    ASSERT_TRUE(select->bind(":id", std::int64_t{1}));
    ASSERT_TRUE(select->next());
    EXPECT_EQ(select->int64(0), 1);
    EXPECT_EQ(select->text(1), "alpha");
    EXPECT_FALSE(select->next());
    EXPECT_TRUE(select->ok());
}

TEST(test_core_SqliteSession, PrepareFailureIsSqlNotARow)
{
    ScopedTempDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string path = dir.dbPath();

    auto opened = SqliteSession::open(path);
    ASSERT_TRUE(opened);
    const auto stmt = opened.value()->prepare("SELECT * FROM no_such_table");
    EXPECT_FALSE(stmt);
    EXPECT_EQ(stmt.kind(), ErrorKind::Sql);
    EXPECT_EQ(stmt.error().key, "error.sql");
}

TEST(test_core_SqliteSession, TransactionRollsBackAFailedWrite)
{
    ScopedTempDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string path = dir.dbPath();

    auto opened = SqliteSession::open(path);
    ASSERT_TRUE(opened);
    auto& db = *opened.value();
    ASSERT_TRUE(db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT UNIQUE)"));
    ASSERT_TRUE(db.exec("INSERT INTO t (name) VALUES ('kept')"));

    const auto status = db.transaction([&] {
        auto insert = db.prepare("INSERT INTO t (name) VALUES (:name)");
        if (!insert) {
            return VLMS::asStatus(insert);
        }
        if (const auto bound = insert->bind(":name", std::string_view{"kept"}); !bound) {
            return bound;
        }
        return insert->exec();
    });
    EXPECT_FALSE(status);

    auto count = db.prepare("SELECT COUNT(*) FROM t");
    ASSERT_TRUE(count);
    ASSERT_TRUE(count->next());
    EXPECT_EQ(count->integer(0), 1);
}

TEST(test_core_SqliteSession, NamedBindsAndNullsRoundTrip)
{
    ScopedTempDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string path = dir.dbPath();

    auto opened = SqliteSession::open(path);
    ASSERT_TRUE(opened);
    auto& db = *opened.value();
    ASSERT_TRUE(db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, note TEXT)"));

    auto insert = db.prepare("INSERT INTO t (note) VALUES (:note)");
    ASSERT_TRUE(insert);
    ASSERT_TRUE(insert->bindNull(":note"));
    ASSERT_TRUE(insert->exec());

    auto select = db.prepare("SELECT note FROM t WHERE id = 1");
    ASSERT_TRUE(select);
    ASSERT_TRUE(select->next());
    EXPECT_TRUE(select->isNull(0));
}
