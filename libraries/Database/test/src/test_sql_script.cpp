#include "TestDatabase.h"
#include "TestEnv.h"

#include <VLMS/Core/Clock.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

using namespace VLMS;
using namespace Test;

/**
 * Connection::execSqlScript used to split on a bare ';':
 *
 *     script.split(QChar(';'), Qt::SkipEmptyParts)
 *
 * which shredded any statement containing a semicolon that was not a statement
 * terminator. Nothing in the three trusted inputs tripped it, which is why it
 * was a latent problem and not an outage -- and why these tests were written
 * during Phase B, before D1 replaced the split with a scanner.
 *
 * Note the exercise goes through the real production path rather than calling
 * a helper directly: execSqlScript is in an anonymous namespace, and the
 * question worth answering is "would applying a schema like this work?", not
 * "does a private function tokenise correctly". Each case appends statements
 * to the genuine database/schema.sql, points VLMS_SCHEMA_PATH at the
 * result, and asks whether Connection::open() succeeds.
 */
class test_core_SqlScript : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        static int counter = 0;
        m_scratch = std::filesystem::temp_directory_path()
            / ("vlms_sql_script_" + std::to_string(stamp) + "_" + std::to_string(++counter));
        std::error_code error;
        std::filesystem::create_directories(m_scratch, error);
        ASSERT_FALSE(error) << error.message();

        const char* schemaPath = std::getenv("VLMS_SCHEMA_PATH");
        ASSERT_NE(schemaPath, nullptr);
        std::ifstream schema(schemaPath);
        ASSERT_TRUE(static_cast<bool>(schema)) << schemaPath;
        std::ostringstream out;
        out << schema.rdbuf();
        m_realSchema = out.str();
        ASSERT_NE(m_realSchema.find("CREATE TABLE IF NOT EXISTS loans"), std::string::npos);
    }

    void TearDown() override
    {
        EXPECT_FALSE(Core::Clock::isOverridden());
        if (!m_scratch.empty()) {
            std::error_code error;
            std::filesystem::remove_all(m_scratch, error);
        }
    }

    /// Applies the real schema plus `extraSql` and reports whether open()
    /// succeeded. `probe` names a table the extra SQL is expected to create.
    [[nodiscard]] bool schemaWithExtraApplies(const std::string& extraSql,
                                              const std::string& probe,
                                              std::string* detail);

    std::filesystem::path m_scratch;
    std::string m_realSchema;
    /// The file the last schemaWithExtraApplies() call wrote, so a test can
    /// reopen the same schema and inspect what it produced.
    std::string m_lastSchemaPath;
};

bool test_core_SqlScript::schemaWithExtraApplies(const std::string& extraSql,
                                                 const std::string& probe,
                                                 std::string* detail)
{
    static int counter = 0;
    const std::filesystem::path path =
        m_scratch / ("schema_" + std::to_string(++counter) + ".sql");

    m_lastSchemaPath = path.string();

    std::ofstream file(path);
    if (!file) {
        *detail = "could not write " + m_lastSchemaPath;
        return false;
    }
    file << m_realSchema << "\n\n" << extraSql << "\n";
    file.close();

    const ScopedEnv pinned("VLMS_SCHEMA_PATH", m_lastSchemaPath);
    const TestDatabase db;
    if (!db.isValid()) {
        *detail = db.lastError();
        return false;
    }
    if (!probe.empty() && !db.tableExists(probe)) {
        *detail = "schema applied but '" + probe + "' was not created";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Works today
// ---------------------------------------------------------------------------

TEST_F(test_core_SqlScript, TheUnmodifiedSchemaApplies)
{
    // The control. If this ever fails, every other result in this file is
    // measuring the harness rather than the splitter.
    std::string detail;
    EXPECT_TRUE(schemaWithExtraApplies({}, {}, &detail)) << detail;
}

TEST_F(test_core_SqlScript, FullLineCommentsAreStripped)
{
    std::string detail;
    const std::string extra =
        "-- a comment on its own line\n"
        "CREATE TABLE IF NOT EXISTS probe_comment (id INTEGER PRIMARY KEY);\n"
        "-- and another\n";
    EXPECT_TRUE(schemaWithExtraApplies(extra, "probe_comment", &detail)) << detail;
}

TEST_F(test_core_SqlScript, BlankStatementsBetweenSemicolonsAreSkipped)
{
    std::string detail;
    const std::string extra = "CREATE TABLE IF NOT EXISTS probe_blank (id INTEGER PRIMARY KEY);;\n\n;\n";
    EXPECT_TRUE(schemaWithExtraApplies(extra, "probe_blank", &detail)) << detail;
}

// ---------------------------------------------------------------------------
// What the naive split got wrong (D1)
// ---------------------------------------------------------------------------

TEST_F(test_core_SqlScript, ExecSqlScriptHandlesSemicolonInsideStringLiteral)
{
    std::string detail;
    const std::string extra =
        "CREATE TABLE IF NOT EXISTS probe_literal (\n"
        "    id INTEGER PRIMARY KEY,\n"
        "    label TEXT NOT NULL DEFAULT 'first; second'\n"
        ");\n";

    // The split cut inside the quotes, so the first fragment ended at
    // `DEFAULT 'first` -- an unterminated string, reported as
    // `unrecognized token: "'first"`, which aborted the whole schema.
    ASSERT_TRUE(schemaWithExtraApplies(extra, "probe_literal", &detail)) << detail;

    // The default has to survive intact, not merely parse.
    const ScopedEnv pinned("VLMS_SCHEMA_PATH", m_lastSchemaPath);
    const TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    ASSERT_TRUE(db.exec("INSERT INTO probe_literal (id) VALUES (1)"));
    EXPECT_EQ(db.scalar("SELECT label FROM probe_literal WHERE id = 1").toString(), "first; second");
}

TEST_F(test_core_SqlScript, ExecSqlScriptHandlesCreateTriggerBeginEnd)
{
    std::string detail;
    const std::string extra =
        "CREATE TABLE IF NOT EXISTS probe_trigger (id INTEGER PRIMARY KEY, touched INTEGER);\n"
        "CREATE TRIGGER IF NOT EXISTS probe_trigger_touch\n"
        "AFTER INSERT ON probe_trigger\n"
        "BEGIN\n"
        "    UPDATE probe_trigger SET touched = 1 WHERE id = NEW.id;\n"
        "END;\n";

    // A trigger body legitimately contains statement terminators. The split
    // left `CREATE TRIGGER ... BEGIN UPDATE ...` without its END -- SQLite
    // answered `incomplete input` -- and then a stray `END` on its own.
    ASSERT_TRUE(schemaWithExtraApplies(extra, "probe_trigger", &detail)) << detail;

    // And the trigger works, which the table's existence alone does not say.
    const ScopedEnv pinned("VLMS_SCHEMA_PATH", m_lastSchemaPath);
    const TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    ASSERT_TRUE(db.exec("INSERT INTO probe_trigger (id) VALUES (1)"));
    EXPECT_EQ(db.scalar("SELECT touched FROM probe_trigger WHERE id = 1").toInt(), 1);
}

TEST_F(test_core_SqlScript, ExecSqlScriptHandlesTrailingCommentContainingSemicolon)
{
    std::string detail;
    const std::string extra =
        "CREATE TABLE IF NOT EXISTS probe_trailing (id INTEGER PRIMARY KEY);"
        "  -- see ticket 12; harmless\n";

    // Comments were stripped AFTER splitting, and only when they started a
    // line. A trailing comment holding a semicolon was therefore cut into a
    // comment-only fragment (correctly skipped) and a bare `harmless`, which
    // was then executed as SQL: `near "harmless": syntax error`.
    EXPECT_TRUE(schemaWithExtraApplies(extra, "probe_trailing", &detail)) << detail;
}

TEST_F(test_core_SqlScript, ExecSqlScriptHandlesSemicolonInsideBlockComment)
{
    std::string detail;
    const std::string extra =
        "/* migration note: replaces the old index; keep until v2 */\n"
        "CREATE TABLE IF NOT EXISTS probe_block (id INTEGER PRIMARY KEY);\n";

    // Block comments were not recognised at all -- the stripper only looked
    // for a leading dash pair -- so the fragment before the semicolon was a
    // comment with nothing executable in it, and SQLite said `No query`.
    EXPECT_TRUE(schemaWithExtraApplies(extra, "probe_block", &detail)) << detail;
}

// ---------------------------------------------------------------------------
// The scanner's own corners
// ---------------------------------------------------------------------------

TEST_F(test_core_SqlScript, ExecSqlScriptHandlesDoubledQuoteInsideStringLiteral)
{
    std::string detail;
    const std::string extra =
        "CREATE TABLE IF NOT EXISTS probe_escape (\n"
        "    id INTEGER PRIMARY KEY,\n"
        "    label TEXT NOT NULL DEFAULT 'L''Etranger; tome 1'\n"
        ");\n";

    // A scanner that treated the doubled quote as the end of the literal would
    // be back outside the string at `Etranger`, and would split on the
    // semicolon that follows. Apostrophes are ordinary in the French half of
    // this catalog, so this is the realistic version of the case above.
    ASSERT_TRUE(schemaWithExtraApplies(extra, "probe_escape", &detail)) << detail;

    const ScopedEnv pinned("VLMS_SCHEMA_PATH", m_lastSchemaPath);
    const TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    ASSERT_TRUE(db.exec("INSERT INTO probe_escape (id) VALUES (1)"));
    EXPECT_EQ(db.scalar("SELECT label FROM probe_escape WHERE id = 1").toString(),
              "L'Etranger; tome 1");
}

TEST_F(test_core_SqlScript, ExecSqlScriptHandlesCaseEndInsideATriggerBody)
{
    std::string detail;
    const std::string extra =
        "CREATE TABLE IF NOT EXISTS probe_case (id INTEGER PRIMARY KEY, flag TEXT, touched INTEGER);\n"
        "CREATE TRIGGER IF NOT EXISTS probe_case_touch\n"
        "AFTER INSERT ON probe_case\n"
        "BEGIN\n"
        "    UPDATE probe_case\n"
        "       SET touched = CASE WHEN NEW.flag = 'yes' THEN 1 ELSE 0 END\n"
        "     WHERE id = NEW.id;\n"
        "END;\n";

    // BEGIN opens the body and END closes it, so a CASE expression inside has
    // to be counted too: without that its END closes the trigger early, the
    // semicolon on the next line splits, and the leftover `END` is executed on
    // its own. Nesting, not a flag.
    ASSERT_TRUE(schemaWithExtraApplies(extra, "probe_case", &detail)) << detail;

    const ScopedEnv pinned("VLMS_SCHEMA_PATH", m_lastSchemaPath);
    const TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    ASSERT_TRUE(db.exec("INSERT INTO probe_case (id, flag) VALUES (1, 'yes')"));
    EXPECT_EQ(db.scalar("SELECT touched FROM probe_case WHERE id = 1").toInt(), 1);
}

TEST_F(test_core_SqlScript, ExecSqlScriptStillReportsAGenuineSyntaxError)
{
    std::string detail;
    const std::string extra = "CREATE TABLE probe_broken (id INTEGER PRIMARY KEY;\n";

    // The point of the scanner is to stop misreading valid SQL, not to become
    // forgiving. Broken SQL must still fail the open rather than be quietly
    // skipped, or a typo in a future migration would look like success.
    EXPECT_FALSE(schemaWithExtraApplies(extra, {}, &detail));
}
