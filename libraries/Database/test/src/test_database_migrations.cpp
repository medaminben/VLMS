#include <VLMS/Database/SqliteSession.h>
#include "TestDatabase.h"

#include <VLMS/Database/Database.h>

#include <gtest/gtest.h>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace VLMS::Test;

namespace {

/// Opens a fixture from tests/data and fails the calling test if it did not
/// materialise -- a missing or malformed .sql file otherwise shows up as a
/// puzzling assertion about a table three lines later.
std::unique_ptr<TestDatabase> openFixture(const std::string& name)
{
    const std::string path = TestDatabase::dataFile(name);
    if (!std::filesystem::exists(path)) {
        return nullptr;
    }
    auto db = std::make_unique<TestDatabase>(TestDatabase::Mode::FromSqlFile, path);
    return db;
}

/// The vlms.db.bak-* files in `directory`: what open() set aside before it
/// migrated.
std::vector<std::filesystem::path> backupsIn(const std::string& directory)
{
    std::vector<std::filesystem::path> found;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string().rfind("vlms.db.bak-", 0) == 0) {
            found.push_back(entry.path());
        }
    }
    return found;
}

/// One text value read straight from a database file, outside Database.
std::string readText(const std::filesystem::path& path, const std::string& sql)
{
    auto opened = VLMS::SqliteSession::open(path.string());
    if (!opened) {
        return {};
    }
    auto stmt = opened.value()->prepare(sql);
    if (!stmt || !stmt.value().next()) {
        return {};
    }
    return stmt.value().text(0);
}

std::string simplified(const std::string& text)
{
    std::string out;
    bool pendingSpace = false;
    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            if (!out.empty()) {
                pendingSpace = true;
            }
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

std::vector<std::string> checkLines(const std::string& createSql)
{
    std::vector<std::string> checks;
    std::istringstream in(createSql);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("CHECK") != std::string::npos) {
            checks.push_back(simplified(line));
        }
    }
    return checks;
}

}  // namespace

/**
 * The four migrateXIfNeeded() steps in Database::open(), one fixture per
 * historical shape.
 *
 * Every other suite drives these too -- TestDatabase always goes through the
 * real open() -- but only against a database that schema.sql just created, so
 * every detector finds nothing to do and the migration bodies never run. These
 * are the tests that actually execute them.
 *
 * Two properties matter more than the column lists: a rebuild must carry row
 * ids across unchanged, because book_copies.book_id points at them; and the
 * whole chain must be idempotent, because it runs on every single launch.
 */
class test_core_DatabaseMigrations : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        const char* dir = std::getenv("VLMS_TEST_DATA_DIR");
        ASSERT_TRUE(dir != nullptr && dir[0] != '\0')
            << "VLMS_TEST_DATA_DIR is not set; check create_test()";
        for (const std::string& name : fixtureNames()) {
            EXPECT_TRUE(std::filesystem::exists(TestDatabase::dataFile(name))) << name;
        }
    }

    void SetUp() override
    {
        std::error_code error;
        m_scratch = std::filesystem::temp_directory_path()
            / ("vlms_mig_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
               + "_" + std::to_string(++s_roots));
        std::filesystem::create_directories(m_scratch, error);
        ASSERT_FALSE(error) << error.message();
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(m_scratch, error);
    }

    static std::vector<std::string> fixtureNames()
    {
        return {
            "legacy_precatalog.sql",
            "legacy_book_language_check.sql",
            "legacy_no_book_description.sql",
            "legacy_no_member_sex.sql",
        };
    }

    /// A copy of `fixture` with `extraSql` appended, written to the scratch
    /// directory. Lets a test alter a fixture's starting state without adding
    /// a near-duplicate file to tests/data.
    [[nodiscard]] std::string fixtureWith(const std::string& fixture, const std::string& extraSql)
    {
        const std::string sql = fixtureText(fixture);
        if (sql.empty()) {
            return {};
        }

        const auto path = m_scratch / ("fixture_" + std::to_string(++m_counter) + ".sql");
        std::ofstream target(path);
        if (!target) {
            return {};
        }
        target << sql << "\n" << extraSql << "\n";
        target.close();
        return path.string();
    }

    /// pre_constraint_dates.sql with the dirty_dates.sql fragment applied.
    [[nodiscard]] std::string dirtyFixture()
    {
        return fixtureWith("pre_constraint_dates.sql", fixtureText("dirty_dates.sql"));
    }

    /// The text of a fixture file, or an empty string.
    [[nodiscard]] static std::string fixtureText(const std::string& fixture)
    {
        std::ifstream source(TestDatabase::dataFile(fixture));
        if (!source) {
            return {};
        }
        std::ostringstream out;
        out << source.rdbuf();
        return out.str();
    }

    std::filesystem::path m_scratch;
    int m_counter = 0;
    static int s_roots;
};

int test_core_DatabaseMigrations::s_roots = 0;

TEST_F(test_core_DatabaseMigrations, EveryFixtureOpens)
{
    for (const std::string& fixture : fixtureNames()) {
        SCOPED_TRACE(fixture);

        const auto db = openFixture(fixture);
        ASSERT_NE(db, nullptr);
        ASSERT_TRUE(db->isValid()) << db->lastError();

        // ensureDefaultEmployee() runs last in the chain and is the cheapest proof
        // that open() got all the way to the end rather than returning early.
        EXPECT_EQ(db->count("employees"), 1);
    }
}

// ---------------------------------------------------------------------------
// Shape 1 -- the catalog rework
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, PrecatalogDatabaseGainsTheNormalisedCatalog)
{
    const auto db = openFixture("legacy_precatalog.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    for (const char* table : {"authors", "publishers", "categories", "books", "book_copies", "loans"}) {
        EXPECT_TRUE(db->tableExists(table)) << table;
    }

    // The normalised shape, not the flat one it replaced.
    EXPECT_TRUE(contains(db->columnNames("books"), "author_id"));
    EXPECT_TRUE(!contains(db->columnNames("books"), "author"));
    EXPECT_TRUE(contains(db->columnNames("loans"), "book_copy_id"));
}

TEST_F(test_core_DatabaseMigrations, PrecatalogMigrationDiscardsTheOldCatalogRows)
{
    const auto db = openFixture("legacy_precatalog.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    // Pinned, not endorsed. migrateCatalogIfNeeded() opens with DROP TABLE IF
    // EXISTS on loans, book_copies, books and categories, so a database on the
    // pre-catalog shape loses its whole catalog and circulation history on the
    // first launch after the upgrade. The fixture seeds one book, one category
    // and one loan precisely so that this is written down somewhere.
    EXPECT_EQ(db->count("books"), 0);
    EXPECT_EQ(db->count("categories"), 0);
    EXPECT_EQ(db->count("loans"), 0);
}

TEST_F(test_core_DatabaseMigrations, PrecatalogMigrationLeavesMembersAndEmployeesAlone)
{
    const auto db = openFixture("legacy_precatalog.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    EXPECT_EQ(db->count("members"), 2);
    EXPECT_EQ(db->scalar("SELECT membership_number FROM members WHERE id = 2").toString(),
              std::string("M-0002"));
    EXPECT_EQ(db->count("employees"), 1);
}

// ---------------------------------------------------------------------------
// Shape 2 -- books.language CHECK (the one real table rebuild)
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, BookLanguageMigrationIsNotBlockedByItsOwnDetectorQuery)
{
    const auto db = openFixture("legacy_book_language_check.sql");
    ASSERT_NE(db, nullptr);

    // Finding 11, found by this suite. The detector reads books' CREATE TABLE
    // out of sqlite_master and, until it was scoped, was still sitting on that
    // row when the rebuild reached DROP TABLE books. SQLite refuses a DROP
    // while a read is open on the schema -- "database table is locked" -- so
    // open() returned false and the app would not start at all against a
    // database on this shape. Every other assertion about shape 2 depends on
    // this one, so it is stated separately: unscope the detector in
    // Database::migrateBookLanguageIfNeeded and this is the test that names why
    // the other five went red.
    EXPECT_TRUE(db->isValid()) << db->lastError();
}

TEST_F(test_core_DatabaseMigrations, BookLanguageMigrationRemovesTheConstraint)
{
    const auto db = openFixture("legacy_book_language_check.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    const std::string createSql =
        db->scalar("SELECT sql FROM sqlite_master WHERE type='table' AND name='books'").toString();
    EXPECT_TRUE(createSql.find("language IN") == std::string::npos) << createSql;

    // The point of removing it: English is a language this library stocks.
    EXPECT_TRUE(db->exec("INSERT INTO books (title, author_id, publisher_id, language) "
                         "VALUES ('An English Title', 1, 1, 'en')"))
        << db->lastError();
}

TEST_F(test_core_DatabaseMigrations, BookLanguageMigrationThatFailsLeavesBooksAsTheyWere)
{
    // Any view on books makes the final RENAME fail, and it fails after
    // DROP TABLE books has already run. Statement by statement, that left
    // only books_new behind: the catalog under a name nothing reads, and a
    // books_new that blocked every later launch. The rebuild is one
    // transaction, so the failure has to put everything back.
    const std::string fixture = fixtureWith("legacy_book_language_check.sql",
                                            "CREATE VIEW book_titles AS SELECT title FROM books;");
    ASSERT_FALSE(fixture.empty());
    const TestDatabase db(TestDatabase::Mode::FromSqlFile, fixture);
    ASSERT_FALSE(db.isValid());

    const std::filesystem::path file = db.databasePath();
    EXPECT_EQ(readText(file, "SELECT count(*) FROM books"), "2");
    EXPECT_EQ(readText(file, "SELECT count(*) FROM book_copies"), "3");
    EXPECT_EQ(readText(file, "SELECT count(*) FROM sqlite_master WHERE name = 'books_new'"), "0");
}

// ---------------------------------------------------------------------------
// The copy set aside before a migration
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, AnOlderDatabaseIsCopiedAsideBeforeItIsMigrated)
{
    const auto db = openFixture("legacy_book_language_check.sql");
    ASSERT_TRUE(db != nullptr && db->isValid()) << (db ? db->lastError() : "");

    const auto backups = backupsIn(db->dataDirectory());
    ASSERT_EQ(backups.size(), 1u);

    // The copy is the database as it was: old version, old constraint, every row.
    EXPECT_EQ(readText(backups.front(), "PRAGMA user_version"), "0");
    EXPECT_NE(readText(backups.front(),
                       "SELECT sql FROM sqlite_master WHERE type='table' AND name='books'")
                  .find("language IN ('ar', 'fr')"),
              std::string::npos);
    EXPECT_EQ(readText(backups.front(), "SELECT count(*) FROM books"), "2");
}

TEST_F(test_core_DatabaseMigrations, AMigrationThatFailsStillLeavesTheCopy)
{
    const std::string fixture = fixtureWith("legacy_book_language_check.sql",
                                            "CREATE VIEW book_titles AS SELECT title FROM books;");
    ASSERT_FALSE(fixture.empty());
    const TestDatabase db(TestDatabase::Mode::FromSqlFile, fixture);
    ASSERT_FALSE(db.isValid());

    const auto backups = backupsIn(db.dataDirectory());
    ASSERT_EQ(backups.size(), 1u);
    EXPECT_EQ(readText(backups.front(), "SELECT count(*) FROM books"), "2");
}

TEST_F(test_core_DatabaseMigrations, ADatabaseThatStaysOnItsVersionIsCopiedOnce)
{
    // Rows that break the date constraints stop the upgrade short of a
    // version stamp, and the app starts anyway. Every launch then finds the
    // same old version; a copy per launch would fill the disk.
    const TestDatabase db(TestDatabase::Mode::FromSqlFile, dirtyFixture());
    ASSERT_TRUE(db.isValid()) << db.lastError();
    ASSERT_EQ(db.userVersion(), 0);

    Database again(db.dataDirectory());
    ASSERT_TRUE(again.open()) << again.lastError();

    EXPECT_EQ(backupsIn(db.dataDirectory()).size(), 1u);
}

TEST_F(test_core_DatabaseMigrations, ANewDatabaseIsNotCopied)
{
    const TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();

    EXPECT_TRUE(backupsIn(db.dataDirectory()).empty());
}

TEST_F(test_core_DatabaseMigrations, ACurrentDatabaseIsNotCopiedOnTheNextLaunch)
{
    const std::string directory = (m_scratch / "current").string();
    for (int launch = 0; launch < 2; ++launch) {
        Database database(directory);
        ASSERT_TRUE(database.open()) << database.lastError();
    }

    EXPECT_TRUE(backupsIn(directory).empty());
}

TEST_F(test_core_DatabaseMigrations, BookLanguageMigrationPreservesEveryRowAndItsId)
{
    const auto db = openFixture("legacy_book_language_check.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    EXPECT_EQ(db->count("books"), 2);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM books WHERE id IN (4, 9)").toInt(), 2);

    EXPECT_EQ(db->scalar("SELECT title FROM books WHERE id = 4").toString(),
              std::string("Al-Muqaddima"));
    EXPECT_EQ(db->scalar("SELECT isbn FROM books WHERE id = 9").toString(),
              std::string("9782070360024"));
    // Carried through the rebuild and then normalised by the publication date
    // migration further down the chain, with the cataloguer's wording kept.
    EXPECT_EQ(db->scalar("SELECT publication_date FROM books WHERE id = 9").toString(),
              std::string("2001-02"));
    EXPECT_EQ(db->scalar("SELECT publication_date_original FROM books WHERE id = 9").toString(),
              std::string("February 2001"));

    // created_at/updated_at are copied rather than re-defaulted: a rebuild that
    // let the DEFAULT fire would silently restamp the entire catalog as new.
    EXPECT_EQ(db->scalar("SELECT created_at FROM books WHERE id = 4").toString(),
              std::string("2025-06-14 09:00:00"));

    // The rebuild's table definition carries `description`, which is why
    // migrateBookDescriptionIfNeeded finds nothing to do straight afterwards.
    EXPECT_TRUE(contains(db->columnNames("books"), "description"));
}

TEST_F(test_core_DatabaseMigrations, BookLanguageMigrationKeepsCopiesAttachedToTheirBooks)
{
    const auto db = openFixture("legacy_book_language_check.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    EXPECT_EQ(db->count("book_copies"), 3);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM book_copies WHERE book_id = 4").toInt(), 2);

    // The rebuild drops and recreates `books` with foreign keys off. If it had
    // renumbered anything, these copies would now point at nothing -- and
    // foreign_key_check is the only thing that would ever say so.
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM book_copies c "
                         "LEFT JOIN books b ON b.id = c.book_id WHERE b.id IS NULL")
                  .toInt(),
              0);
}

TEST_F(test_core_DatabaseMigrations, BookLanguageMigrationRecreatesTheBookIndexes)
{
    const auto db = openFixture("legacy_book_language_check.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    // DROP TABLE takes the table's indexes with it. The migration recreates
    // three of them explicitly; if that block is ever dropped from the script
    // the catalog silently degrades to full scans instead of failing.
    for (const char* index : {"idx_books_title", "idx_books_category", "idx_books_author"}) {
        EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM sqlite_master "
                             "WHERE type='index' AND name = :name",
                             {{"name", index}})
                      .toInt(),
                  1);
    }
}

TEST_F(test_core_DatabaseMigrations, BookLanguageMigrationLeavesForeignKeysEnabled)
{
    const auto db = openFixture("legacy_book_language_check.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    // The migration script switches foreign keys off to do the rebuild and back
    // on at the end. If the trailing PRAGMA is ever lost, the app keeps running
    // for the rest of the session with referential integrity disabled and
    // nothing reports it.
    EXPECT_EQ(db->scalar("PRAGMA foreign_keys").toInt(), 1);
}

// ---------------------------------------------------------------------------
// Shape 3 -- books.description
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, BookDescriptionMigrationAddsTheColumnAsNull)
{
    const auto db = openFixture("legacy_no_book_description.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    EXPECT_TRUE(contains(db->columnNames("books"), "description"));
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM books WHERE description IS NULL").toInt(), 2);
}

TEST_F(test_core_DatabaseMigrations, BookDescriptionMigrationRewritesNoRows)
{
    const auto db = openFixture("legacy_no_book_description.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    // ALTER TABLE ADD COLUMN, not a rebuild: ids, values and timestamps are
    // exactly what the fixture wrote.
    EXPECT_EQ(db->count("books"), 2);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM books WHERE id IN (4, 9)").toInt(), 2);
    EXPECT_EQ(db->scalar("SELECT updated_at FROM books WHERE id = 9").toString(),
              std::string("2025-06-14 09:05:00"));
}

// ---------------------------------------------------------------------------
// Shape 4 -- members.sex
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, MemberSexMigrationAddsTheColumnAsNull)
{
    const auto db = openFixture("legacy_no_member_sex.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    EXPECT_TRUE(contains(db->columnNames("members"), "sex"));
    EXPECT_EQ(db->count("members"), 2);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM members WHERE sex IS NULL").toInt(), 2);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM members WHERE id IN (1, 7)").toInt(), 2);
}

TEST_F(test_core_DatabaseMigrations, MemberSexMigrationEnforcesItsCheckConstraint)
{
    const auto db = openFixture("legacy_no_member_sex.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    EXPECT_TRUE(db->exec("UPDATE members SET sex = 'male' WHERE id = 7"));
    EXPECT_TRUE(!db->exec("UPDATE members SET sex = 'unknown' WHERE id = 7"))
        << "the CHECK added by ALTER TABLE must still refuse a bad value";
    EXPECT_EQ(db->scalar("SELECT sex FROM members WHERE id = 7").toString(), std::string("male"));
}

// ---------------------------------------------------------------------------
// The chain
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, ChainIsIdempotentAcrossTwoOpens)
{
    for (const std::string& fixture : fixtureNames()) {
        SCOPED_TRACE(fixture);

        const auto db = openFixture(fixture);
        ASSERT_NE(db, nullptr);
        ASSERT_TRUE(db->isValid()) << db->lastError();

        const std::vector<std::string> tablesAfterFirst = db->tableNames();
        const int booksAfterFirst = db->tableExists("books") ? db->count("books") : -1;
        const int membersAfterFirst = db->count("members");
        const std::vector<std::string> bookColumns = db->columnNames("books");
        const std::vector<std::string> memberColumns = db->columnNames("members");

        // Every launch runs the whole chain again. A detector that re-fires on an
        // already-migrated database would rebuild `books` on every start -- and the
        // catalog migration would drop it.
        ASSERT_TRUE(db->database().open()) << "second open() failed";

        EXPECT_EQ(db->tableNames(), tablesAfterFirst);
        EXPECT_EQ(db->count("books"), booksAfterFirst);
        EXPECT_EQ(db->count("members"), membersAfterFirst);
        EXPECT_EQ(db->columnNames("books"), bookColumns);
        EXPECT_EQ(db->columnNames("members"), memberColumns);
        EXPECT_EQ(db->count("employees"), 1);
    }
}

TEST_F(test_core_DatabaseMigrations, LegacyDatabaseDoesNotGainTablesItNeverHad)
{
    const auto db = openFixture("legacy_no_member_sex.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    // applySchema() returns early as soon as `members` exists, so schema.sql is
    // never applied to a database that has been through an earlier version.
    // member_status_history is declared there and nowhere else, so a legacy
    // database simply does not have it -- and since nothing reads that table
    // (see the plan's out-of-scope list) nobody has noticed. Pinned so that a
    // future feature which does read it starts from a known fact rather than a
    // crash on a librarian's machine.
    EXPECT_TRUE(!db->tableExists("member_status_history"));
}

// ---------------------------------------------------------------------------
// Schema version
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, EveryFixtureStartsAtVersionZero)
{
    for (const std::string& fixture : fixtureNames()) {
        SCOPED_TRACE(fixture);

        // If a fixture ever stamped itself, its migration would be skipped and the
        // assertions about it would pass for the wrong reason.
        const std::string text = fixtureText(fixture);
        EXPECT_FALSE(text.empty());
        EXPECT_TRUE(text.find("user_version") == std::string::npos)
            << "a legacy fixture must not declare a schema version";
    }
}

TEST_F(test_core_DatabaseMigrations, LegacyDatabaseIsStampedOnceMigrated)
{
    for (const std::string& fixture : fixtureNames()) {
        SCOPED_TRACE(fixture);

        const auto db = openFixture(fixture);
        ASSERT_NE(db, nullptr);
        ASSERT_TRUE(db->isValid()) << db->lastError();

        // The stamp is what stops the four detectors running a PRAGMA table_info
        // apiece on every launch for the rest of the database's life.
        EXPECT_EQ(db->userVersion(), Database::kSchemaVersion);
    }
}

TEST_F(test_core_DatabaseMigrations, AStampedDatabaseSkipsTheLegacyDetectors)
{
    // A database still on the pre-sex shape, but claiming to be current. The
    // only honest way to observe "the detectors did not run" is to lie to them
    // and check that nothing happened.
    const std::string path = fixtureWith("legacy_no_member_sex.sql", "PRAGMA user_version = 1;");
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    EXPECT_TRUE(!contains(db.columnNames("members"), "sex"))
        << "a stamped database must dispatch on its version, not sniff its shape";
}

// ---------------------------------------------------------------------------
// members.email -- the first upgrade that dispatches on the version rather
// than on the shape
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, LegacyDatabaseGainsTheEmailColumn)
{
    const auto db = openFixture("legacy_no_member_sex.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    EXPECT_TRUE(contains(db->columnNames("members"), "email"));
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM members WHERE email IS NULL").toInt(),
              db->count("members"));
    EXPECT_EQ(db->userVersion(), Database::kSchemaVersion);
}

TEST_F(test_core_DatabaseMigrations, StampedVersionOneDatabaseIsUpgradedRatherThanSniffed)
{
    // Version 1 is the shape before email existed. Nothing about this database
    // is sniffed: the number alone says what it is missing, which is the whole
    // point of stamping, and the sex detector must stay skipped.
    const std::string path = fixtureWith("legacy_no_member_sex.sql", "PRAGMA user_version = 1;");
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    EXPECT_TRUE(contains(db.columnNames("members"), "email"));
    EXPECT_TRUE(!contains(db.columnNames("members"), "sex"))
        << "the versioned upgrade must not drag the legacy detectors in with it";
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
}

TEST_F(test_core_DatabaseMigrations, ConstraintRebuildCarriesEmailAddressesAcross)
{
    // The rebuild copies members column by column, so an address only survives
    // if that INSERT names it. A database that already has the column and
    // still needs the constraints is exactly the case that would lose them.
    const std::string path = fixtureWith(
        "pre_constraint_dates.sql",
        "ALTER TABLE members ADD COLUMN email TEXT;\n"
        "UPDATE members SET email = 'amina@example.org' WHERE id = 3;");
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    EXPECT_EQ(db.scalar("SELECT email FROM members WHERE id = 3").toString(),
              std::string("amina@example.org"));
    EXPECT_TRUE(db.scalar("SELECT email FROM members WHERE id = 8").isNull());
}

TEST_F(test_core_DatabaseMigrations, FreshDatabaseAlreadyHasTheEmailColumn)
{
    const TestDatabase fresh;
    ASSERT_TRUE(fresh.isValid()) << fresh.lastError();
    EXPECT_TRUE(contains(fresh.columnNames("members"), "email"));
}

// ---------------------------------------------------------------------------
// The date constraints and their pre-flight (D3, D4)
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, ConstraintMigrationAppliesToACleanDatabase)
{
    const auto db = openFixture("pre_constraint_dates.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    const std::string createSql =
        db->scalar("SELECT sql FROM sqlite_master WHERE type='table' AND name='loans'").toString();
    EXPECT_TRUE(createSql.find("date(due_at) IS due_at") != std::string::npos) << createSql;

    EXPECT_EQ(db->userVersion(), Database::kSchemaVersion);
}

TEST_F(test_core_DatabaseMigrations, ConstraintMigrationPreservesEveryRow)
{
    const auto db = openFixture("pre_constraint_dates.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    // A rebuild that renumbered would leave every loan pointing at the wrong
    // member and every status history row orphaned, and nothing in the app
    // would report it -- the ids would still be integers.
    EXPECT_EQ(db->count("loans"), 3);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM loans WHERE id IN (2, 5, 6)").toInt(), 3);
    EXPECT_EQ(db->scalar("SELECT notes FROM loans WHERE id = 2").toString(),
              std::string("returned early"));
    EXPECT_EQ(db->scalar("SELECT returned_at FROM loans WHERE id = 5").toString(), std::string());

    EXPECT_EQ(db->count("members"), 2);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM members WHERE id IN (3, 8)").toInt(), 2);
    EXPECT_EQ(db->scalar("SELECT membership_number FROM members WHERE id = 8").toString(),
              std::string("M-0008"));
    EXPECT_EQ(db->scalar("SELECT date_of_birth FROM members WHERE id = 3").toString(),
              std::string("1990-05-12"));

    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM loans l "
                         "LEFT JOIN members m ON m.id = l.member_id WHERE m.id IS NULL")
                  .toInt(),
              0);
}

TEST_F(test_core_DatabaseMigrations, ConstraintMigrationDoesNotCascadeAwayStatusHistory)
{
    const auto db = openFixture("pre_constraint_dates.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    // member_status_history references members ON DELETE CASCADE. The rebuild
    // DROPs members, so with foreign keys left on, these two rows would be
    // deleted as a side effect and the migration would still report success.
    // This is the assertion that the PRAGMA before the transaction is doing
    // something.
    EXPECT_EQ(db->count("member_status_history"), 2);
}

TEST_F(test_core_DatabaseMigrations, ConstraintMigrationRecreatesEveryIndex)
{
    const auto db = openFixture("pre_constraint_dates.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    for (const char* index : {"idx_loans_member",
                              "idx_loans_copy",
                              "idx_loans_borrowed_at",
                              "idx_loans_open",
                              "idx_loans_one_open_per_copy",
                              "idx_members_name",
                              "idx_members_number"}) {
        EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM sqlite_master "
                             "WHERE type='index' AND name = :name",
                             {{"name", index}})
                      .toInt(),
                  1)
            << index;
    }
}

TEST_F(test_core_DatabaseMigrations, ConstraintMigrationLeavesForeignKeysEnabled)
{
    const auto db = openFixture("pre_constraint_dates.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    // Foreign keys are switched off for the rebuild. If the restore is ever
    // lost, the app runs for the rest of the session with referential
    // integrity disabled and nothing anywhere reports it.
    EXPECT_EQ(db->scalar("PRAGMA foreign_keys").toInt(), 1);
}

TEST_F(test_core_DatabaseMigrations, ConstraintMigrationMatchesWhatSchemaSqlDeclares)
{
    const auto migrated = openFixture("pre_constraint_dates.sql");
    ASSERT_TRUE(migrated != nullptr && migrated->isValid());
    const TestDatabase fresh;
    ASSERT_TRUE(fresh.isValid()) << fresh.lastError();

    // The rebuild's CREATE TABLE is written in C++ and schema.sql's is written
    // in SQL, and a database can arrive at either. Comparing the constraint
    // text is what stops the two from drifting into two different shapes with
    // the same version number.
    for (const char* table : {"loans", "members"}) {
        SCOPED_TRACE(table);
        const SqlBinds bind{{"name", table}};
        const std::string sql = "SELECT sql FROM sqlite_master WHERE type='table' AND name = :name";
        const std::vector<std::string> migratedChecks = checkLines(migrated->scalar(sql, bind).toString());
        const std::vector<std::string> freshChecks = checkLines(fresh.scalar(sql, bind).toString());
        EXPECT_TRUE(!migratedChecks.empty()) << table;
        EXPECT_EQ(migratedChecks, freshChecks);
    }
}

TEST_F(test_core_DatabaseMigrations, NewLoansViolatingCheckAreRejectedAfterMigration)
{
    const auto db = openFixture("pre_constraint_dates.sql");
    ASSERT_TRUE(db != nullptr && db->isValid());

    // A migrated database has to be as strict as a fresh one, or the constraint
    // work only protects installations that started after it.
    EXPECT_TRUE(!db->exec("INSERT INTO loans (member_id, book_copy_id, borrowed_at, due_at) "
                          "VALUES (3, 13, '2019', '2019-06-01')"));
    EXPECT_TRUE(!db->exec("INSERT INTO loans (member_id, book_copy_id, borrowed_at, due_at) "
                          "VALUES (3, 13, '2025-01-01', '2025-01-01')"));
    EXPECT_TRUE(!db->exec(
        "INSERT INTO members (membership_number, first_name, last_name, date_of_birth) "
        "VALUES ('M-9999', 'Test', 'Person', '2024-02-30')"));

    // ...and no stricter: copy 13's only loan was returned, so it is free.
    EXPECT_TRUE(db->exec("INSERT INTO loans (member_id, book_copy_id, borrowed_at, due_at) "
                         "VALUES (3, 13, '2025-08-01', '2025-08-15')"))
        << db->lastError();
}

TEST_F(test_core_DatabaseMigrations, ConstraintMigrationIsSkippedWhenViolatingRowsExist)
{
    const std::string path = dirtyFixture();
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    // Six rows the constraints would reject, one per constraint. Rebuilding
    // would fail halfway through, and refusing to open would leave a librarian
    // with a dead app because of a typo made in 2019. So: warn, skip, start.
    const std::string createSql =
        db.scalar("SELECT sql FROM sqlite_master WHERE type='table' AND name='loans'").toString();
    EXPECT_TRUE(createSql.find("date(due_at) IS due_at") == std::string::npos) << createSql;

    // Left at 0 deliberately: the shape claimed has to be the shape present.
    EXPECT_EQ(db.userVersion(), 0);
}

TEST_F(test_core_DatabaseMigrations, SkippedConstraintMigrationLeavesTheDatabaseUsable)
{
    const std::string path = dirtyFixture();
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    // Nothing touched, including the offending rows -- a migration that
    // "helpfully" deleted or rewrote them would be destroying a librarian's
    // records to satisfy a constraint.
    EXPECT_EQ(db.count("loans"), 8);
    EXPECT_EQ(db.count("members"), 3);
    EXPECT_EQ(db.scalar("SELECT due_at FROM loans WHERE id = 20").toString(),
              std::string("14/08/2020"));
    EXPECT_EQ(db.count("employees"), 1);
}

TEST_F(test_core_DatabaseMigrations, SkippedConstraintMigrationRunsOnceTheDataIsCorrected)
{
    const std::string path = dirtyFixture();
    ASSERT_TRUE(!path.empty());

    TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();
    EXPECT_EQ(db.userVersion(), 0);

    // The skip is a retry, not a surrender. This is the librarian fixing the
    // six rows -- through sqlite3, or a future repair screen -- and restarting.
    EXPECT_TRUE(db.exec("UPDATE loans SET due_at = '2020-08-14' WHERE id = 20"));
    EXPECT_TRUE(db.exec("UPDATE loans SET borrowed_at = '2019-01-01' WHERE id = 21"));
    EXPECT_TRUE(db.exec("UPDATE loans SET borrowed_at = '2024-02-29' WHERE id = 22"));
    EXPECT_TRUE(db.exec("UPDATE loans SET due_at = '2025-03-17' WHERE id = 23"));
    EXPECT_TRUE(db.exec("DELETE FROM loans WHERE id = 24"));
    EXPECT_TRUE(db.exec("UPDATE members SET date_of_birth = '1900-03-01' WHERE id = 12"));

    ASSERT_TRUE(db.database().open()) << "the next launch must migrate";
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
    EXPECT_EQ(db.count("loans"), 7);
}

TEST_F(test_core_DatabaseMigrations, ConstraintMigrationIsRolledBackWhenAStepFails)
{
    // A loans_new table already sitting in the database -- left behind by an
    // earlier crash -- makes the rebuild's first CREATE TABLE fail. Everything
    // after it must not have happened.
    const std::string path = fixtureWith(
        "pre_constraint_dates.sql",
        "CREATE TABLE loans_new (id INTEGER PRIMARY KEY, wrong TEXT);");
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);

    // open() reports the failure rather than starting on a half-migrated
    // database: unlike a pre-flight violation, this is not something the
    // librarian can correct by editing a row.
    EXPECT_TRUE(!db.isValid()) << "a failed rebuild must fail the open";

    // And the original table is intact, still without constraints, still at 0.
    EXPECT_EQ(db.scalar("SELECT COUNT(*) FROM loans").toInt(), 3);
    EXPECT_EQ(db.userVersion(), 0);
}

// ---------------------------------------------------------------------------
// Publication date normalisation (D5)
// ---------------------------------------------------------------------------

namespace {

/// Book rows covering the shapes the production catalog actually contains.
std::string publicationDateSeed()
{
    return "INSERT INTO books (id, title, author_id, publisher_id, language, publication_date) VALUES"
           " (101, 'Bare year', 1, 1, 'ar', '2014'),"
           " (102, 'Month day year', 1, 1, 'ar', 'Nov 06, 1996'),"
           " (103, 'Month year', 1, 1, 'fr', 'February 2001'),"
           " (104, 'Already ISO', 1, 1, 'ar', '2003-03-18'),"
           " (105, 'Day month year', 1, 1, 'fr', '20 May 2013'),"
           " (106, 'Year month name', 1, 1, 'ar', '2011 March'),"
           " (107, 'Year month number', 1, 1, 'ar', '2000 03'),"
           " (108, 'Marc decade', 1, 1, 'ar', '201u'),"
           " (109, 'Ambiguous slashes', 1, 1, 'fr', '1/7/2021'),"
           " (110, 'No date', 1, 1, 'ar', NULL);";
}

}  // namespace

TEST_F(test_core_DatabaseMigrations, PublicationDatesAreNormalisedOnMigration)
{
    const std::string path = fixtureWith("pre_constraint_dates.sql", publicationDateSeed());
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    const auto stored = [&db](int id) {
        return db.scalar("SELECT publication_date FROM books WHERE id = :id", {{"id", id}})
            .toString();
    };

    EXPECT_EQ(stored(101), std::string("2014"));
    EXPECT_EQ(stored(102), std::string("1996-11-06"));
    EXPECT_EQ(stored(103), std::string("2001-02"));
    EXPECT_EQ(stored(104), std::string("2003-03-18"));
    EXPECT_EQ(stored(105), std::string("2013-05-20"));
    EXPECT_EQ(stored(106), std::string("2011-03"));
    EXPECT_EQ(stored(107), std::string("2000-03"));
    EXPECT_TRUE(db.scalar("SELECT publication_date FROM books WHERE id = 110").toString().empty());
}

TEST_F(test_core_DatabaseMigrations, PublicationDateOriginalsAreKept)
{
    const std::string path = fixtureWith("pre_constraint_dates.sql", publicationDateSeed());
    ASSERT_TRUE(!path.empty());

    TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    EXPECT_EQ(db.scalar("SELECT publication_date_original FROM books WHERE id = 102").toString(),
              std::string("Nov 06, 1996"));

    // The reason the column exists: this is a one-way transform over the whole
    // catalog that discards the wording a cataloguer chose, and some of what it
    // rewrites is ambiguous. One UPDATE has to be able to undo all of it.
    EXPECT_TRUE(db.exec("UPDATE books SET publication_date = publication_date_original "
                        "WHERE publication_date_original IS NOT NULL"));
    EXPECT_EQ(db.scalar("SELECT publication_date FROM books WHERE id = 103").toString(),
              std::string("February 2001"));
}

TEST_F(test_core_DatabaseMigrations, UnnormalisablePublicationDatesSurviveVerbatim)
{
    const std::string path = fixtureWith("pre_constraint_dates.sql", publicationDateSeed());
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    // '201u' is MARC notation for "sometime in the 2010s" and '1/7/2021' could
    // be January or July. A migration that guessed at either would be
    // destroying information to make a column tidier.
    EXPECT_EQ(db.scalar("SELECT publication_date FROM books WHERE id = 108").toString(),
              std::string("201u"));
    EXPECT_EQ(db.scalar("SELECT publication_date FROM books WHERE id = 109").toString(),
              std::string("1/7/2021"));
}

TEST_F(test_core_DatabaseMigrations, PublicationDateMigrationDoesNotRunTwice)
{
    // A database whose constraint migration the pre-flight declined stays at
    // version 0, so the whole legacy chain -- this migration included -- runs
    // again on the next launch. A second backfill would overwrite every
    // original with the normalised value and quietly destroy what the column
    // is for.
    const std::string path =
        fixtureWith("pre_constraint_dates.sql", fixtureText("dirty_dates.sql") + publicationDateSeed());
    ASSERT_TRUE(!path.empty());

    TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();
    EXPECT_EQ(db.userVersion(), 0);
    EXPECT_EQ(db.scalar("SELECT publication_date_original FROM books WHERE id = 102").toString(),
              std::string("Nov 06, 1996"));

    EXPECT_TRUE(db.database().open());
    EXPECT_TRUE(db.database().open());

    EXPECT_EQ(db.scalar("SELECT publication_date_original FROM books WHERE id = 102").toString(),
              std::string("Nov 06, 1996"));
    EXPECT_EQ(db.scalar("SELECT publication_date FROM books WHERE id = 102").toString(),
              std::string("1996-11-06"));
}

TEST_F(test_core_DatabaseMigrations, FreshDatabaseAlreadyHasThePublicationDateOriginalColumn)
{
    const TestDatabase fresh;
    ASSERT_TRUE(fresh.isValid()) << fresh.lastError();

    // A migrated database and a fresh one both report version 1, so they have
    // to be the same shape. If schema.sql lacked this column, any future code
    // reading it would work on every upgraded installation and fail on every
    // new one.
    EXPECT_TRUE(contains(fresh.columnNames("books"), "publication_date_original"));
}

// --- re-normalisation for imported catalogs (v2 -> v3) ---------------------
//
// scripts/import_catalog.py loads the real library catalog straight
// into SQLite, so publication_date arrives holding whatever the cataloguer
// typed. The script cannot normalise it -- normalizePublicationDate() is C++,
// and porting it to Python would fork rules that tst_date_text pins down --
// so it writes the raw text into both columns, stamps the database at version
// 2, and leaves this upgrade to do the transform on first open.
//
// The fixture is that shape: publication_date_original already present (so the
// version-0 detector has nothing to do) and the version already stamped.

namespace {

/// A version-2 database holding an imported catalog: dates raw in both columns.
constexpr auto kImportedCatalogSql = R"SQL(
ALTER TABLE books ADD COLUMN publication_date_original TEXT;
INSERT INTO authors (id, name) VALUES (900, 'Ibn Khaldun');
INSERT INTO books (id, title, author_id, language, publication_date, publication_date_original)
VALUES
    (900, 'Normalisable', 900, 'ar', 'Nov 06, 1996', 'Nov 06, 1996'),
    (901, 'Already ISO',  900, 'ar', '2014',         '2014'),
    (902, 'Hijri year',   900, 'ar', '1432 هجرياً',   '1432 هجرياً'),
    (903, 'MARC decade',  900, 'fr', '201u',         '201u'),
    (904, 'Placeholder',  900, 'fr', '*****',        '*****'),
    (905, 'No date',      900, 'fr', NULL,           NULL);
PRAGMA user_version = 2;
)SQL";

}  // namespace

TEST_F(test_core_DatabaseMigrations, ImportedCatalogGetsItsPublicationDatesNormalised)
{
    const std::string path = fixtureWith("pre_constraint_dates.sql", kImportedCatalogSql);
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    EXPECT_EQ(db.scalar("SELECT publication_date FROM books WHERE id = 900").toString(),
              std::string("1996-11-06"));
    EXPECT_EQ(db.scalar("SELECT publication_date FROM books WHERE id = 901").toString(),
              std::string("2014"));
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
}

/// A publication date column legitimately holds things that are not dates. The
/// normaliser returns those unchanged, and the migration must not "tidy" them
/// into NULL on the way past.
TEST_F(test_core_DatabaseMigrations, ImportedCatalogKeepsUnnormalisableDatesVerbatim)
{
    const std::string path = fixtureWith("pre_constraint_dates.sql", kImportedCatalogSql);
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    EXPECT_EQ(db.scalar("SELECT publication_date FROM books WHERE id = 902").toString(),
              std::string("1432 هجرياً"));
    EXPECT_EQ(db.scalar("SELECT publication_date FROM books WHERE id = 903").toString(),
              std::string("201u"));
    EXPECT_EQ(db.scalar("SELECT publication_date FROM books WHERE id = 904").toString(),
              std::string("*****"));
    EXPECT_TRUE(db.scalar("SELECT publication_date FROM books WHERE id = 905").isNull());
}

TEST_F(test_core_DatabaseMigrations, ImportedCatalogKeepsItsOriginals)
{
    const std::string path = fixtureWith("pre_constraint_dates.sql", kImportedCatalogSql);
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    // The whole transform stays reversible with one UPDATE.
    EXPECT_EQ(db.scalar("SELECT publication_date_original FROM books WHERE id = 900").toString(),
              std::string("Nov 06, 1996"));
    EXPECT_EQ(db.scalar("SELECT COUNT(*) FROM books WHERE id = 900 "
                        "AND publication_date != publication_date_original")
                  .toInt(),
              1);
}

/// It reads the originals and writes the derived column, so a second pass has
/// nothing left to change -- which is what makes it safe on every launch.
TEST_F(test_core_DatabaseMigrations, VersionThreeUpgradeIsIdempotent)
{
    const std::string path = fixtureWith("pre_constraint_dates.sql", kImportedCatalogSql);
    ASSERT_TRUE(!path.empty());

    static const std::string kDatesQuery =
        "SELECT GROUP_CONCAT(COALESCE(publication_date, '<null>'), '|') "
        "FROM (SELECT publication_date FROM books ORDER BY id)";

    TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    const std::string firstPass = db.scalar(kDatesQuery).toString();
    EXPECT_TRUE(!firstPass.empty());

    // Every launch runs the chain again. This one is now stamped at 3, so the
    // step should not even fire -- but if it ever did, reading the untouched
    // originals means it would compute the same answer.
    ASSERT_TRUE(db.database().open()) << "second open() failed";

    EXPECT_EQ(db.scalar(kDatesQuery).toString(), firstPass);
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
}

// --- member spreadsheet columns (v4 -> v5) ---------------------------------

namespace {

constexpr auto kVersionFourMembersSql = R"SQL(
PRAGMA user_version = 4;
)SQL";

}  // namespace

TEST_F(test_core_DatabaseMigrations, FreshDatabaseAlreadyHasMemberSpreadsheetColumns)
{
    const TestDatabase fresh;
    ASSERT_TRUE(fresh.isValid()) << fresh.lastError();

    const std::vector<std::string> columns = fresh.columnNames("members");
    EXPECT_TRUE(contains(columns, "occupation"));
    EXPECT_TRUE(contains(columns, "age_group"));
    EXPECT_TRUE(contains(columns, "full_name"));
    EXPECT_TRUE(contains(columns, "source_row"));
}

TEST_F(test_core_DatabaseMigrations, VersionFourDatabaseGainsMemberSpreadsheetColumns)
{
    const std::string path = fixtureWith("pre_constraint_dates.sql", kVersionFourMembersSql);
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    const std::vector<std::string> columns = db.columnNames("members");
    EXPECT_TRUE(contains(columns, "occupation"));
    EXPECT_TRUE(contains(columns, "age_group"));
    EXPECT_TRUE(contains(columns, "full_name"));
    EXPECT_TRUE(contains(columns, "source_row"));
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
    EXPECT_EQ(db.count("members"), 2);
}

TEST_F(test_core_DatabaseMigrations, MembershipNumberStaysUniqueAfterSpreadsheetUpgrade)
{
    const TestDatabase fresh;
    ASSERT_TRUE(fresh.isValid()) << fresh.lastError();

    ASSERT_TRUE(fresh.exec(
        "INSERT INTO members (membership_number, first_name, last_name) "
        "VALUES ('1', 'Amina', 'One')"))
        << fresh.lastError();
    EXPECT_TRUE(!fresh.exec(
        "INSERT INTO members (membership_number, first_name, last_name) "
        "VALUES ('1', 'Zineb', 'Two')"));
    EXPECT_TRUE(fresh.exec(
        "INSERT INTO members (membership_number, first_name, last_name) "
        "VALUES ('1b', 'Zineb', 'Two')"))
        << fresh.lastError();
}

// ---------------------------------------------------------------------------
// v5 -> v6: archive columns and nullable copy numbers
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, VersionFiveGainsArchiveColumnsAndNullableCopyNumbers)
{
    const auto db = openFixture("schema_v5.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    EXPECT_EQ(db->scalar("PRAGMA user_version").toInt(), Database::kSchemaVersion);
    for (const std::string table : {"books", "book_copies", "loans"}) {
        EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM pragma_table_info('" + table
                             + "') WHERE name = 'archived_at'")
                      .toInt(),
                  1)
            << table;
    }
    for (const std::string column : {"local_id", "global_copy_id"}) {
        EXPECT_EQ(db->scalar("SELECT \"notnull\" FROM pragma_table_info('book_copies') "
                             "WHERE name = '" + column + "'")
                      .toInt(),
                  0)
            << column;
    }
}

TEST_F(test_core_DatabaseMigrations, VersionSixMigrationKeepsCopiesAndLoanReferences)
{
    const auto db = openFixture("schema_v5.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    EXPECT_EQ(db->scalar("SELECT global_copy_id FROM book_copies WHERE id = 1").toString(), "AR-7");
    EXPECT_EQ(db->scalar("SELECT local_id FROM book_copies WHERE id = 1").toString(), "7");
    EXPECT_EQ(db->scalar("SELECT book_copy_id FROM loans WHERE id = 1").toInt(), 1);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM pragma_foreign_key_check").toInt(), 0);
    EXPECT_EQ(db->scalar("PRAGMA foreign_keys").toInt(), 1);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' "
                         "AND name IN ('idx_books_archived', 'idx_copies_archived', "
                         "'idx_loans_archived', 'idx_copies_book', 'idx_copies_local', "
                         "'idx_copies_central')")
                  .toInt(),
              6);
}

TEST_F(test_core_DatabaseMigrations, VersionSixColumnOrderMatchesAFreshDatabase)
{
    const auto migrated = openFixture("schema_v5.sql");
    ASSERT_NE(migrated, nullptr);
    ASSERT_TRUE(migrated->isValid()) << migrated->lastError();
    TestDatabase fresh;
    ASSERT_TRUE(fresh.isValid()) << fresh.lastError();

    for (const std::string table : {"books", "book_copies", "loans"}) {
        const std::string columns = "SELECT group_concat(name, ',') FROM "
                                    "(SELECT name FROM pragma_table_info('" + table
            + "') ORDER BY cid)";
        EXPECT_EQ(migrated->scalar(columns).toString(), fresh.scalar(columns).toString()) << table;
    }
}

TEST_F(test_core_DatabaseMigrations, ArchivedCopiesMayShareNullNumbersButLiveOnesMayNot)
{
    TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    ASSERT_TRUE(db.exec("INSERT INTO books (id, title, language) VALUES (1, 'T', 'ar')"));
    ASSERT_TRUE(db.exec("INSERT INTO book_copies (book_id, global_copy_id, source, local_id, "
                        "archived_at) VALUES (1, NULL, 'arabic', NULL, '2026-09-19 10:00:00')"));
    EXPECT_TRUE(db.exec("INSERT INTO book_copies (book_id, global_copy_id, source, local_id, "
                        "archived_at) VALUES (1, NULL, 'arabic', NULL, '2026-09-19 10:00:00')"));
    ASSERT_TRUE(db.exec("INSERT INTO book_copies (book_id, global_copy_id, source, local_id) "
                        "VALUES (1, 'AR-5', 'arabic', '5')"));
    EXPECT_FALSE(db.exec("INSERT INTO book_copies (book_id, global_copy_id, source, local_id) "
                         "VALUES (1, 'AR-5b', 'arabic', '5')"));
    EXPECT_FALSE(db.exec("INSERT INTO book_copies (book_id, global_copy_id, source, local_id) "
                         "VALUES (1, 'AR-5', 'arabic', '6')"));
}

// ---------------------------------------------------------------------------
// v7: members.active_until
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, VersionSixGainsActiveUntilOneYearAfterRegistration)
{
    const auto db = openFixture("schema_v6.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    EXPECT_EQ(db->userVersion(), Database::kSchemaVersion);
    EXPECT_EQ(db->scalar("SELECT active_until FROM members WHERE id = 1").toString(), "2026-09-23");
    EXPECT_EQ(db->scalar("SELECT active_until FROM members WHERE id = 2").toString(), "2026-09-22");
    EXPECT_EQ(db->scalar("SELECT active_until FROM members WHERE id = 3").toString(), "2025-02-28");
}

TEST_F(test_core_DatabaseMigrations, VersionSevenIgnoresTheStoredStatus)
{
    const auto db = openFixture("schema_v6.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    // Member 2 is stored 'active' but registered 2025-09-23: the year is the
    // rule, so the date is what registration gives, whatever status said.
    EXPECT_EQ(db->scalar("SELECT active_until FROM members WHERE id = 2").toString(), "2026-09-22");
}

TEST_F(test_core_DatabaseMigrations, VersionSevenMigrationKeepsStatusHistory)
{
    const auto db = openFixture("schema_v6.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    EXPECT_EQ(db->count("member_status_history"), 3);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM pragma_foreign_key_check").toInt(), 0);
    EXPECT_EQ(db->scalar("PRAGMA foreign_keys").toInt(), 1);
}

TEST_F(test_core_DatabaseMigrations, VersionSevenMigrationRunsOnce)
{
    const auto db = openFixture("schema_v6.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    // A renewal after the upgrade must survive the next launch.
    ASSERT_TRUE(db->exec("UPDATE members SET active_until = '2030-01-01' WHERE id = 1"));
    ASSERT_TRUE(db->database().open()) << "second open() failed";
    EXPECT_EQ(db->scalar("SELECT active_until FROM members WHERE id = 1").toString(), "2030-01-01");
}

/// Version 7 was stamped while status was still stored and active_until was
/// already filled. open() must still drop the column and its index, and must
/// leave the history and the stored date alone.
TEST_F(test_core_DatabaseMigrations, VersionSevenAlreadyStampedStillDropsStatus)
{
    const std::string path = fixtureWith("schema_v6.sql", R"SQL(
ALTER TABLE members ADD COLUMN active_until TEXT
    CHECK (date(active_until) IS active_until);
UPDATE members SET active_until = '2031-06-15';
PRAGMA user_version = 7;
)SQL");
    ASSERT_TRUE(!path.empty());

    const TestDatabase db(TestDatabase::Mode::FromSqlFile, path);
    ASSERT_TRUE(db.isValid()) << db.lastError();

    EXPECT_TRUE(!contains(db.columnNames("members"), "status"));
    EXPECT_EQ(db.scalar("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' "
                        "AND name = 'idx_members_status'")
                  .toInt(),
              0);
    EXPECT_EQ(db.count("member_status_history"), 3);
    EXPECT_EQ(db.scalar("SELECT new_status FROM member_status_history WHERE member_id = 3").toString(),
              "non_active");
    EXPECT_EQ(db.scalar("SELECT active_until FROM members WHERE id = 1").toString(), "2031-06-15");
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);

    ASSERT_TRUE(db.database().open()) << "second open() failed";
    EXPECT_EQ(db.scalar("SELECT active_until FROM members WHERE id = 1").toString(), "2031-06-15");
    EXPECT_TRUE(!contains(db.columnNames("members"), "status"));
}

TEST_F(test_core_DatabaseMigrations, VersionSevenColumnOrderMatchesAFreshDatabase)
{
    const auto migrated = openFixture("schema_v6.sql");
    ASSERT_NE(migrated, nullptr);
    ASSERT_TRUE(migrated->isValid()) << migrated->lastError();
    TestDatabase fresh;
    ASSERT_TRUE(fresh.isValid()) << fresh.lastError();

    const std::string columns = "SELECT group_concat(name, ',') FROM "
                                "(SELECT name FROM pragma_table_info('members') ORDER BY cid)";
    EXPECT_EQ(migrated->scalar(columns).toString(), fresh.scalar(columns).toString());
}

TEST_F(test_core_DatabaseMigrations, ActiveUntilRefusesADayThatDoesNotExist)
{
    TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();

    EXPECT_FALSE(db.exec("INSERT INTO members (membership_number, first_name, last_name, "
                         "active_until) VALUES ('90', 'A', 'B', '2026-02-30')"));
    EXPECT_TRUE(db.exec("INSERT INTO members (membership_number, first_name, last_name, "
                        "active_until) VALUES ('91', 'A', 'B', '2026-02-28')"));
}

TEST_F(test_core_DatabaseMigrations, VersionSevenDropsTheStatusColumnAndItsIndex)
{
    const auto db = openFixture("schema_v6.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    EXPECT_FALSE(contains(db->columnNames("members"), "status"));
    EXPECT_TRUE(contains(db->columnNames("members"), "active_until"));
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM sqlite_master WHERE name = 'idx_members_status'")
                  .toInt(),
              0);
    // DROP COLUMN rewrites the table; it must not take the history with it.
    EXPECT_EQ(db->count("member_status_history"), 3);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM pragma_foreign_key_check").toInt(), 0);
}

TEST_F(test_core_DatabaseMigrations, FreshDatabaseHasNoStatusColumn)
{
    const TestDatabase fresh;
    ASSERT_TRUE(fresh.isValid()) << fresh.lastError();
    EXPECT_FALSE(contains(fresh.columnNames("members"), "status"));
    EXPECT_TRUE(contains(fresh.columnNames("members"), "active_until"));
}

TEST_F(test_core_DatabaseMigrations, EveryLegacyShapeEndsWithoutStatus)
{
    for (const std::string& fixture : fixtureNames()) {
        SCOPED_TRACE(fixture);
        const auto db = openFixture(fixture);
        ASSERT_NE(db, nullptr);
        ASSERT_TRUE(db->isValid()) << db->lastError();
        EXPECT_FALSE(contains(db->columnNames("members"), "status"));
        EXPECT_TRUE(contains(db->columnNames("members"), "active_until"));
    }
}

TEST_F(test_core_DatabaseMigrations, PreConstraintDatabaseEndsWithoutStatus)
{
    const auto db = openFixture("pre_constraint_dates.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();
    EXPECT_FALSE(contains(db->columnNames("members"), "status"));
    EXPECT_EQ(db->count("member_status_history"), 2);
}
