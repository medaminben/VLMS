#include "TestDatabase.h"

#include <VLMS/Core/DateText.h>
#include <VLMS/Database/Connection.h>

#include <VLMS/Database/SqliteSession.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

using namespace VLMS;
using namespace Test;

namespace {

/// Opens `path` on a throwaway session so the test can read "before" numbers.
/// The file is the library's actual catalog; the session is destroyed before
/// TestDatabase copies it.
class ReadOnlySource {
public:
    explicit ReadOnlySource(const std::string& path)
    {
        auto opened = Database::SqliteSession::open(path);
        if (!opened) {
            return;
        }
        m_session = std::move(opened.value());
    }

    [[nodiscard]] bool isOpen() const { return m_session != nullptr; }
    [[nodiscard]] Database::SqliteSession& session() const { return *m_session; }

private:
    std::unique_ptr<Database::SqliteSession> m_session;
};

std::uint32_t rotr(std::uint32_t value, std::uint32_t bits)
{
    return (value >> bits) | (value << (32u - bits));
}

std::array<std::uint8_t, 32> sha256Bytes(const std::vector<std::uint8_t>& data)
{
    static const std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};

    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    std::vector<std::uint8_t> padded = data;
    const std::uint64_t bitLength = static_cast<std::uint64_t>(data.size()) * 8u;
    padded.push_back(0x80);
    while ((padded.size() % 64u) != 56u) {
        padded.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xffu));
    }

    for (std::size_t offset = 0; offset < padded.size(); offset += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(padded[offset + static_cast<std::size_t>(i) * 4]) << 24)
                | (static_cast<std::uint32_t>(padded[offset + static_cast<std::size_t>(i) * 4 + 1]) << 16)
                | (static_cast<std::uint32_t>(padded[offset + static_cast<std::size_t>(i) * 4 + 2]) << 8)
                | static_cast<std::uint32_t>(padded[offset + static_cast<std::size_t>(i) * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = hh + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = S0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::array<std::uint8_t, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        digest[static_cast<std::size_t>(i) * 4] = static_cast<std::uint8_t>((h[i] >> 24) & 0xffu);
        digest[static_cast<std::size_t>(i) * 4 + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xffu);
        digest[static_cast<std::size_t>(i) * 4 + 2] = static_cast<std::uint8_t>((h[i] >> 8) & 0xffu);
        digest[static_cast<std::size_t>(i) * 4 + 3] = static_cast<std::uint8_t>(h[i] & 0xffu);
    }
    return digest;
}

std::array<std::uint8_t, 32> fileDigest(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)),
                                         std::istreambuf_iterator<char>());
    return sha256Bytes(data);
}

std::string join(const std::vector<std::string>& values, const std::string& separator)
{
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out += separator;
        }
        out += values[i];
    }
    return out;
}

std::int64_t maxIdOf(const TestDatabase& db, const std::string& table)
{
    const SqlValue value = db.scalar("SELECT MAX(id) FROM " + table);
    if (value.isNull() || value.toString().empty()) {
        return 0;
    }
    try {
        return std::stoll(value.toString());
    } catch (...) {
        return 0;
    }
}

}  // namespace

/**
 * Phase D against the library's real catalog.
 *
 * Everything else in the suite runs on a database built five minutes ago from
 * schema.sql, or on a fixture written to trigger exactly one migration. This is
 * the only test that answers the question the librarian actually has: what
 * happens to *my* database when I install this?
 *
 * Registered only when VLMS_TEST_REAL_DB names a file, and labelled `realdb` so
 * CI excludes it -- the file is not in the repository. It works on a copy in a
 * temporary directory and checks the original's SHA-256 at the end, because a
 * test that damaged the catalog while verifying that nothing damages the
 * catalog would be a poor joke.
 *
 * The fixture is built once for the whole class rather than per test. Every
 * assertion below reads; none of them writes, so there is no ordering to get
 * wrong, and migrating 9.7 MB nineteen times to prove that is not worth the
 * minute.
 */
class test_core_RealDb : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        const char* path = std::getenv("VLMS_TEST_REAL_DB");
        if (path == nullptr || path[0] == '\0') {
            s_skipped = true;
            s_skipReason = "VLMS_TEST_REAL_DB is not set; check tests/core/CMakeLists";
            return;
        }
        ASSERT_TRUE(std::filesystem::exists(path)) << path;

        s_sourcePath = path;
        s_sourceDigest = fileDigest(s_sourcePath);
        const std::array<std::uint8_t, 32> emptyDigest{};
        ASSERT_NE(s_sourceDigest, emptyDigest);

        {
            const ReadOnlySource source(s_sourcePath);
            ASSERT_TRUE(source.isOpen()) << "could not open the source database read-only";

            auto tables = source.session().prepare(
                "SELECT name FROM sqlite_master WHERE type='table' "
                "AND name NOT LIKE 'sqlite_%' ORDER BY name");
            ASSERT_TRUE(tables);
            while (tables->next()) {
                s_tables.push_back(tables->text(0));
            }
            ASSERT_FALSE(s_tables.empty()) << "the source database has no tables";

            for (const std::string& table : s_tables) {
                auto count = source.session().prepare("SELECT COUNT(*), MAX(id) FROM " + table);
                ASSERT_TRUE(count);
                ASSERT_TRUE(count->next());
                s_rowCountsBefore[table] = count->integer(0);
                s_maxIdsBefore[table] = count->int64(1);
            }
        }

        s_db = std::make_unique<TestDatabase>(TestDatabase::Mode::FromCopyOf, s_sourcePath);
    }

    static void TearDownTestSuite()
    {
        s_db.reset();
    }

    void SetUp() override
    {
        if (s_skipped) {
            GTEST_SKIP() << s_skipReason;
        }
        ASSERT_NE(s_db, nullptr);
    }

    [[nodiscard]] std::string scalarString(const std::string& sql) const
    {
        return s_db->scalar(sql).toString();
    }

    [[nodiscard]] int scalarInt(const std::string& sql) const
    {
        return s_db->scalar(sql).toInt();
    }

    static bool s_skipped;
    static std::string s_skipReason;
    static std::string s_sourcePath;
    static std::array<std::uint8_t, 32> s_sourceDigest;
    static std::vector<std::string> s_tables;
    static std::map<std::string, int> s_rowCountsBefore;
    static std::map<std::string, std::int64_t> s_maxIdsBefore;
    static std::unique_ptr<TestDatabase> s_db;
};

bool test_core_RealDb::s_skipped = false;
std::string test_core_RealDb::s_skipReason;
std::string test_core_RealDb::s_sourcePath;
std::array<std::uint8_t, 32> test_core_RealDb::s_sourceDigest{};
std::vector<std::string> test_core_RealDb::s_tables;
std::map<std::string, int> test_core_RealDb::s_rowCountsBefore;
std::map<std::string, std::int64_t> test_core_RealDb::s_maxIdsBefore;
std::unique_ptr<TestDatabase> test_core_RealDb::s_db;

TEST_F(test_core_RealDb, TheDatabaseOpens)
{
    EXPECT_TRUE(s_db->isValid()) << s_db->lastError();
}

TEST_F(test_core_RealDb, IntegrityCheckIsOk)
{
    ASSERT_TRUE(s_db->isValid());
    EXPECT_EQ(scalarString("PRAGMA integrity_check"), std::string("ok"));
}

TEST_F(test_core_RealDb, ForeignKeyCheckIsEmpty)
{
    ASSERT_TRUE(s_db->isValid());

    auto query = s_db->session().prepare("PRAGMA foreign_key_check");
    ASSERT_TRUE(query);

    std::vector<std::string> offenders;
    while (query->next()) {
        offenders.push_back(query->text(0) + " rowid " + query->text(1));
    }
    EXPECT_TRUE(offenders.empty()) << join(offenders, "; ");
}

TEST_F(test_core_RealDb, EveryTableKeepsItsRowCount)
{
    ASSERT_TRUE(s_db->isValid());

    // The loans and members rebuilds are INSERT ... SELECT into a new table.
    // Losing a row there would be silent: the app would come up, the catalog
    // would look right, and some member's loan history would simply be gone.
    for (const std::string& table : s_tables) {
        SCOPED_TRACE(table);
        EXPECT_EQ(s_db->count(table), s_rowCountsBefore[table]);
    }
}

TEST_F(test_core_RealDb, EveryTableKeepsItsHighestId)
{
    ASSERT_TRUE(s_db->isValid());

    // Row count alone would not catch a rebuild that renumbered: the same
    // number of rows, all of them pointing at the wrong parent.
    for (const std::string& table : s_tables) {
        SCOPED_TRACE(table);
        EXPECT_EQ(maxIdOf(*s_db, table), s_maxIdsBefore[table]);
    }
}

TEST_F(test_core_RealDb, SchemaVersionAdvancesToCurrent)
{
    ASSERT_TRUE(s_db->isValid());

    // The production copy reports 0 going in. Arriving at 1 means the whole
    // legacy chain ran and the constraint pre-flight found nothing to object
    // to -- if it had, this would still be 0 and that would be the finding.
    EXPECT_EQ(s_db->userVersion(), Database::Connection::kSchemaVersion);
}

TEST_F(test_core_RealDb, LoanConstraintsAreInPlace)
{
    ASSERT_TRUE(s_db->isValid());

    const std::string createSql =
        scalarString("SELECT sql FROM sqlite_master WHERE type='table' AND name='loans'");
    EXPECT_TRUE(createSql.find("date(borrowed_at) IS borrowed_at") != std::string::npos)
        << createSql;
    EXPECT_TRUE(createSql.find("date(due_at) > date(borrowed_at)") != std::string::npos)
        << createSql;

    EXPECT_TRUE(!s_db->exec(
                    "INSERT INTO loans (member_id, book_copy_id, borrowed_at, due_at) "
                    "SELECT (SELECT MIN(id) FROM members), (SELECT MIN(id) FROM book_copies), "
                    "'2014', '2014-06-01'"))
        << "a bare year must not be accepted as a borrow date";
}

TEST_F(test_core_RealDb, MemberDateOfBirthConstraintIsInPlace)
{
    ASSERT_TRUE(s_db->isValid());

    // All 13 production values are valid ISO, which is what let the migration
    // run at all; this is the constraint they were checked against.
    EXPECT_EQ(scalarInt("SELECT COUNT(*) FROM members WHERE date(date_of_birth) IS NOT date_of_birth"),
              0);
    EXPECT_TRUE(!s_db->exec(
        "UPDATE members SET date_of_birth = '1900-02-29' WHERE id = (SELECT MIN(id) FROM members)"));
}

TEST_F(test_core_RealDb, OneOpenLoanPerCopyIndexExists)
{
    ASSERT_TRUE(s_db->isValid());
    EXPECT_EQ(scalarInt("SELECT COUNT(*) FROM sqlite_master WHERE type='index' "
                        "AND name='idx_loans_one_open_per_copy'"),
              1);
}

TEST_F(test_core_RealDb, EveryPublicationDateAgreesWithItsOriginal)
{
    ASSERT_TRUE(s_db->isValid());

    auto query = s_db->session().prepare(
        "SELECT id, publication_date, publication_date_original FROM books");
    ASSERT_TRUE(query);

    int rows = 0;
    while (query->next()) {
        ++rows;
        const std::string stored = query->text(1);
        const std::string original = query->text(2);

        // Two things at once, and both matter. Every row kept its original --
        // an ALTER plus UPDATE that missed rows would leave nulls behind and
        // the transform would stop being reversible. And every stored value is
        // what the normaliser produces from that original, which is the actual
        // claim: nothing was rewritten by hand, by accident, or twice.
        EXPECT_FALSE(original.empty())
            << "book " << query->text(0) << " lost its original publication date";
        EXPECT_EQ(stored, Core::DateText::normalizePublicationDate(original));
    }
    EXPECT_EQ(rows, s_rowCountsBefore["books"]);
}

TEST_F(test_core_RealDb, PublicationDatesEndUpInReducedIsoOrVerbatim)
{
    ASSERT_TRUE(s_db->isValid());

    auto query = s_db->session().prepare("SELECT publication_date FROM books");
    ASSERT_TRUE(query);

    static const std::regex reducedIso(R"(^\d{4}(-\d{2}(-\d{2})?)?$)");

    int conforming = 0;
    std::vector<std::string> verbatim;
    while (query->next()) {
        const std::string value = query->text(0);
        if (std::regex_match(value, reducedIso)) {
            ++conforming;
        } else {
            verbatim.push_back(value);
        }
    }

    EXPECT_EQ(conforming + static_cast<int>(verbatim.size()), s_rowCountsBefore["books"]);

    // The measured split for the catalog this test ships against. The plan
    // estimated 3040 and 27 before the parser existed; these are what it
    // actually produces. Guarded on the row total so that pointing
    // VLMS_TEST_REAL_DB at a different database checks the invariant above
    // without failing on numbers that describe someone else's catalog.
    if (s_rowCountsBefore["books"] == 3067) {
        EXPECT_EQ(conforming, 3043);
        EXPECT_EQ(static_cast<int>(verbatim.size()), 24);

        // And what is left is what should be left: MARC notation, a Hijri
        // year, day/month orders nobody can resolve.
        for (const std::string& value : verbatim) {
            EXPECT_FALSE(value.empty()) << "a blank is not a verbatim value";
        }
    }
}

TEST_F(test_core_RealDb, TheSourceFileIsUntouched)
{
    // Last, and the reason the whole suite runs on a copy. TestDatabase copies
    // into a QTemporaryDir and the read-only connection above sets
    // QSQLITE_OPEN_READONLY, but neither is worth trusting on a file that is a
    // library's entire catalog.
    EXPECT_EQ(fileDigest(s_sourcePath), s_sourceDigest);
}
