#include <VLMS/Core/SqlText.h>

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

using namespace VLMS;

/**
 * escapeLike and nullableText used to be copy-pasted into three repositories'
 * anonymous namespaces, where they could only be tested through a full
 * database round trip -- which is why the injection suite proves escaping by
 * seeding "abc" and "a%c" and searching for "%". Those tests stay; they prove
 * the escaping is actually *reached* by the query. These prove the function
 * itself, including the cases no repository currently exercises.
 */

TEST(test_core_SqlText, EscapeLikeNeutralisesWildcards)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"100%", "100\\%"},
        {"a_b", "a\\_b"},
        {"a\\b", "a\\\\b"},
        {"%_", "\\%\\_"},
        {"%%", "\\%\\%"},
        {"كتاب%", "كتاب\\%"},
        // apostrophe is not a LIKE wildcard
        {"L'Étranger", "L'Étranger"},
        // semicolon is not a LIKE wildcard
        {"; DROP TABLE books;--", "; DROP TABLE books;--"},
    };
    for (const auto& [input, expected] : cases) {
        SCOPED_TRACE(input);
        EXPECT_EQ(SqlText::escapeLike(input), expected);
    }
}

TEST(test_core_SqlText, EscapeLikeDoublesTheBackslashBeforeTheWildcards)
{
    // The ordering trap. If % and _ were escaped before the backslash, the
    // backslashes this function introduces would themselves be escaped on the
    // second pass: "100%" would come out as "100\\%", where the pattern reads
    // a literal backslash followed by the % wildcard -- so a search for "100%"
    // would match every row starting with a backslash and none of the rows
    // actually containing "100%".
    EXPECT_EQ(SqlText::escapeLike("100%"), "100\\%");
    EXPECT_EQ(SqlText::escapeLike("100\\%"), "100\\\\\\%");

    // A pre-escaped pattern must not survive untouched, or a caller escaping
    // twice would silently produce a pattern that means something else.
    EXPECT_NE(SqlText::escapeLike("\\%"), "\\%");
}

TEST(test_core_SqlText, EscapeLikeIsIdempotentOnTextWithoutWildcards)
{
    for (const char* input : {"Les Misérables", "الأدب العربي", "9782070360024", "", "   ",
                              "\" OR \"\"=\""}) {
        SCOPED_TRACE(input);
        EXPECT_EQ(SqlText::escapeLike(input), input);
    }
}

TEST(test_core_SqlText, NullableTextTrims)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"  Sfax", "Sfax"},
        {"Sfax  ", "Sfax"},
        {"\t Sfax ", "Sfax"},
        // internal spaces are kept
        {"  Ksour Essef  ", "Ksour Essef"},
    };
    for (const auto& [input, expected] : cases) {
        SCOPED_TRACE(input);
        EXPECT_EQ(SqlText::nullableText(input).value_or(std::string()), expected);
    }
}

TEST(test_core_SqlText, NullableTextReturnsNullForBlankInput)
{
    for (const char* input : {"", "   ", "\t", "\n"}) {
        SCOPED_TRACE(input);
        EXPECT_FALSE(SqlText::nullableText(input).has_value());
    }
}

TEST(test_core_SqlText, NullableTextNeverReturnsAnEmptyString)
{
    for (const char* input : {"", "   ", "\t", "\n"}) {
        SCOPED_TRACE(input);

        // The distinction this function exists to enforce. A QVariant holding an
        // empty QString binds as '', not as NULL, and a column carrying both for
        // the same meaning needs every query to test for both -- which is the test
        // that gets forgotten.
        const auto bound = SqlText::nullableText(input);
        EXPECT_FALSE(bound.has_value()) << "blank input must bind as SQL NULL, never as ''";
    }
}

// ---------------------------------------------------------------------------
// splitStatements
//
// tst_sql_script proves this end to end, by appending each awkward case to the
// real schema and asking whether Database::open() survives it. These are the
// same corners asked directly, because splitStatements is public API now: the
// test harness applies fixtures through it too, so a regression here would
// break the thing that would otherwise report the regression.
// ---------------------------------------------------------------------------

TEST(test_core_SqlText, SplitStatementsCountsTopLevelSemicolons)
{
    const std::vector<std::pair<std::string, int>> cases = {
        {"SELECT 1; SELECT 2;", 2},
        {"SELECT 1; SELECT 2", 2},
        {"SELECT 1;;\n\n;\nSELECT 2;", 2},
        {"INSERT INTO t VALUES ('a; b'); SELECT 1;", 2},
        {"SELECT 1; -- one; two\nSELECT 2;", 2},
        {"/* one; two */ SELECT 1;", 1},
        {"CREATE TRIGGER t AFTER INSERT ON x BEGIN UPDATE x SET a = 1; END;", 1},
        {"CREATE TRIGGER t AFTER INSERT ON x BEGIN "
         "UPDATE x SET a = CASE WHEN 1 THEN 1 ELSE 0 END; END; SELECT 1;",
         2},
        {"SELECT 'L''Etranger; tome 1'; SELECT 2;", 2},
        {"", 0},
        {"-- nothing here\n/* nor here */\n", 0},
    };
    for (const auto& [script, expected] : cases) {
        SCOPED_TRACE(script);
        EXPECT_EQ(int(SqlText::splitStatements(script).size()), expected);
    }
}

TEST(test_core_SqlText, SplitStatementsDropsComments)
{
    // Comment-only fragments must not survive: SQLite answers "No query" for
    // one, which reads as a broken schema rather than as a comment.
    const std::vector<std::string> statements =
        SqlText::splitStatements("-- a note\nSELECT 1; /* another */ SELECT 2;");
    EXPECT_EQ(statements.size(), 2u);
    for (const std::string& statement : statements) {
        EXPECT_EQ(statement.find("note"), std::string::npos) << statement;
        EXPECT_EQ(statement.find("another"), std::string::npos) << statement;
    }
}

TEST(test_core_SqlText, SplitStatementsKeepsStringLiteralsWhole)
{
    const std::vector<std::string> statements =
        SqlText::splitStatements("INSERT INTO t VALUES ('first; second', 'L''Etranger');");
    ASSERT_EQ(statements.size(), 1u);
    EXPECT_NE(statements.front().find("'first; second'"), std::string::npos) << statements.front();
    EXPECT_NE(statements.front().find("'L''Etranger'"), std::string::npos) << statements.front();
}
