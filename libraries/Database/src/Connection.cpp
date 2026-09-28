#include <VLMS/Database/Connection.h>

#include <VLMS/Core/DateText.h>
#include <VLMS/Core/Paths.h>
#include <VLMS/Database/SqlText.h>

#include <VLMS/Database/SqliteSession.h>
#include <VLMS/Core/Text.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace VLMS::Database {

namespace {

std::string bundledSchemaPath()
{
    if (const char* fromEnv = std::getenv("VLMS_SCHEMA_PATH"); fromEnv != nullptr && *fromEnv != '\0') {
        return fromEnv;
    }

    const std::filesystem::path root(Core::Paths::projectRoot());
    const auto besideExe = root / "schema.sql";
    if (std::filesystem::exists(besideExe)) {
        return besideExe.string();
    }

#ifdef VLMS_SCHEMA_PATH
    return VLMS_SCHEMA_PATH;
#else
    return (root / "database" / "schema.sql").string();
#endif
}

std::string readFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in) {
        return {};
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

// Before open() migrates a database it leaves a copy of it beside the file,
// as vlms.db.bak-v<version>-<local time>. One copy per version: a database
// whose upgrade stops short of a new version stamp finds its copy already
// there on the next launch. VACUUM INTO writes a consistent copy through the
// open connection, whatever the journal holds.
//
// Returns an empty string on success, otherwise what went wrong.
std::string copyAsideBeforeMigrating(SqliteSession& session,
                                     const std::filesystem::path& databasePath, int version)
{
    namespace fs = std::filesystem;
    const std::string prefix =
        databasePath.filename().string() + ".bak-v" + std::to_string(version) + "-";
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(databasePath.parent_path(), error)) {
        if (entry.path().filename().string().rfind(prefix, 0) == 0) {
            return {};
        }
    }
    if (error) {
        return "cannot list " + databasePath.parent_path().string() + ": " + error.message();
    }

    char stamp[32] = {};
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&now));
    const fs::path target = databasePath.parent_path() / (prefix + stamp);

    auto vacuum = session.prepare("VACUUM INTO :target");
    if (!vacuum) {
        return vacuum.error().detail;
    }
    if (const Core::Status bound = vacuum->bind(":target", target.string()); !bound) {
        return bound.error().detail;
    }
    if (const Core::Status done = vacuum->exec(); !done) {
        return "cannot copy the database to " + target.string() + ": " + done.error().detail;
    }
    return {};
}

}  // namespace

Connection::Connection(std::string dataDirectory)
    : m_dataDirectory(std::move(dataDirectory))
{
}

Connection::~Connection() = default;

std::string Connection::databasePath() const
{
    return (std::filesystem::path(m_dataDirectory) / "vlms.db").string();
}

SqliteSession& Connection::session() const
{
    if (m_session == nullptr) {
        std::fprintf(stderr, "Database::session() called before a successful open()\n");
        std::abort();
    }
    return *m_session;
}

void Connection::warn(const std::string& message) const
{
    std::fprintf(stderr, "%s\n", message.c_str());
}

bool Connection::execAll(const std::vector<std::string>& statements, const std::string& context)
{
    for (std::size_t i = 0; i < statements.size(); ++i) {
        if (!m_session->exec(statements[i])) {
            warn(context + " failed at statement " + std::to_string(i + 1) + ": "
                 + m_session->lastError() + "\nSQL: " + statements[i].substr(0, 200));
            return false;
        }
    }
    return true;
}

bool Connection::execSqlScript(const std::string& script, const std::string& context)
{
    for (const std::string& statement : SqlText::splitStatements(script)) {
        if (!m_session->exec(statement)) {
            warn(context + " failed: " + m_session->lastError() + "\nSQL: "
                 + statement.substr(0, 200));
            return false;
        }
    }
    return true;
}

bool Connection::tableHasColumn(const std::string& table, const std::string& column) const
{
    auto info = m_session->prepare("PRAGMA table_info(" + table + ")");
    if (!info) {
        return false;
    }
    while (info->next()) {
        if (info->text(1) == column) {
            return true;
        }
    }
    return false;
}

bool Connection::hasAnyTable() const
{
    auto query = m_session->prepare("SELECT 1 FROM sqlite_master WHERE type='table' LIMIT 1");
    return query && query->next();
}

bool Connection::open()
{
    std::error_code error;
    std::filesystem::create_directories(m_dataDirectory, error);

    auto opened = SqliteSession::open(databasePath());
    if (!opened) {
        warn("Failed to open database: " + opened.error().detail);
        m_lastError = opened.error().detail;
        return false;
    }
    m_session = std::move(opened.value());

    if (!m_session->exec("PRAGMA foreign_keys = ON")) {
        m_lastError = m_session->lastError();
        return false;
    }

    // Anything below may rewrite an existing database, so it is copied aside
    // first. A file with no tables is new and has nothing worth keeping.
    if (const int found = schemaVersion(); found < kSchemaVersion && hasAnyTable()) {
        if (const std::string failure =
                copyAsideBeforeMigrating(*m_session, databasePath(), found);
            !failure.empty()) {
            warn("Refusing to migrate without a copy of the database: " + failure);
            m_lastError = failure;
            return false;
        }
    }

    if (!applySchema()) {
        return false;
    }

    const int version = schemaVersion();
    if (version > kSchemaVersion) {
        warn("Database schema version " + std::to_string(version)
             + " is newer than this build understands (" + std::to_string(kSchemaVersion)
             + "): " + databasePath());
        m_lastError = "schema too new";
        return false;
    }

    if (version == 0 && !migrateLegacyShapesIfNeeded()) {
        return false;
    }

    if (!upgradeSchemaIfNeeded()) {
        return false;
    }

    return ensureDefaultEmployee();
}

int Connection::schemaVersion() const
{
    auto query = m_session->prepare("PRAGMA user_version");
    if (!query || !query->next()) {
        return 0;
    }
    return query->integer(0);
}

bool Connection::setSchemaVersion(int version)
{
    if (!m_session->exec("PRAGMA user_version = " + std::to_string(version))) {
        warn("Could not set the schema version: " + m_session->lastError());
        return false;
    }
    return true;
}

bool Connection::migrateLegacyShapesIfNeeded()
{
    if (!migrateCatalogIfNeeded() || !migrateBookLanguageIfNeeded()
        || !migrateBookDescriptionIfNeeded() || !migrateMemberSexIfNeeded()
        || !migrateMemberEmailIfNeeded() || !migrateMemberArchivedIfNeeded()
        || !migratePublicationDatesIfNeeded()) {
        return false;
    }

    bool constraintsApplied = false;
    if (!migrateDateConstraintsIfNeeded(&constraintsApplied)) {
        return false;
    }
    if (!migrateMemberSpreadsheetColumnsIfNeeded()) {
        return false;
    }
    if (!migrateArchiveColumnsIfNeeded()) {
        return false;
    }
    if (!migrateMemberActiveUntilIfNeeded()) {
        return false;
    }
    if (!constraintsApplied) {
        return true;
    }
    return setSchemaVersion(kSchemaVersion);
}

int Connection::countRowsBlockingDateConstraints() const
{
    struct Violation {
        const char* what;
        const char* sql;
    };
    static const Violation checks[] = {
        {"loans.borrowed_at is not a real calendar date",
         "SELECT id FROM loans WHERE date(borrowed_at) IS NOT borrowed_at"},
        {"loans.due_at is not a real calendar date",
         "SELECT id FROM loans WHERE date(due_at) IS NOT due_at"},
        {"loans.returned_at is not a real calendar date",
         "SELECT id FROM loans WHERE date(returned_at) IS NOT returned_at"},
        {"loans due on or before the day they were borrowed",
         "SELECT id FROM loans WHERE NOT (date(due_at) > date(borrowed_at))"},
        {"loans returned before they were borrowed",
         "SELECT id FROM loans WHERE NOT (returned_at IS NULL "
         "OR date(returned_at) >= date(borrowed_at))"},
        {"copies with more than one open loan",
         "SELECT id FROM loans WHERE returned_at IS NULL AND book_copy_id IN ("
         "SELECT book_copy_id FROM loans WHERE returned_at IS NULL "
         "GROUP BY book_copy_id HAVING COUNT(*) > 1)"},
        {"members.date_of_birth is not a real calendar date",
         "SELECT id FROM members WHERE date(date_of_birth) IS NOT date_of_birth"},
    };

    int total = 0;
    for (const Violation& check : checks) {
        auto query = m_session->prepare(check.sql);
        if (!query) {
            warn(std::string("Date constraint pre-flight could not run (") + check.what + "): "
                 + query.error().detail);
            ++total;
            continue;
        }

        std::string ids;
        int found = 0;
        while (query->next()) {
            ++found;
            if (found <= 50) {
                if (!ids.empty()) {
                    ids += ", ";
                }
                ids += query->text(0);
            }
        }
        if (found == 0) {
            continue;
        }
        total += found;
        warn("Date constraint pre-flight: " + std::to_string(found) + " row(s) -- " + check.what
             + " -- ids: " + ids + (found > 50 ? " ..." : ""));
    }
    return total;
}

bool Connection::migratePublicationDatesIfNeeded()
{
    auto info = m_session->prepare("PRAGMA table_info(books)");
    if (!info) {
        warn("Publication date migration check failed: " + info.error().detail);
        return false;
    }
    bool hasBooks = false;
    while (info->next()) {
        hasBooks = true;
        if (info->text(1) == "publication_date_original") {
            return true;
        }
    }
    if (!hasBooks) {
        return true;
    }

    const Core::Status work = m_session->transaction([&] {
        if (!m_session->exec("ALTER TABLE books ADD COLUMN publication_date_original TEXT")
            || !m_session->exec("UPDATE books SET publication_date_original = publication_date")) {
            return Core::Status::fail(Core::ErrorKind::Sql, "error.sql", m_session->lastError());
        }

        auto read = m_session->prepare(
            "SELECT id, publication_date FROM books WHERE publication_date IS NOT NULL");
        if (!read) {
            return Core::asStatus(read);
        }

        struct Rewrite {
            std::int64_t id;
            std::string value;
        };
        std::vector<Rewrite> rewrites;
        while (read->next()) {
            const std::string original = read->text(1);
            const std::string normalised = Core::DateText::normalizePublicationDate(original);
            if (normalised != original) {
                rewrites.push_back({read->int64(0), normalised});
            }
        }

        auto update = m_session->prepare("UPDATE books SET publication_date = :value WHERE id = :id");
        if (!update) {
            return Core::asStatus(update);
        }
        for (const auto& [id, value] : rewrites) {
            update->reset();
            if (!update->bind(":value", value) || !update->bind(":id", id) || !update->exec()) {
                return Core::Status::fail(Core::ErrorKind::Sql, "error.sql", m_session->lastError());
            }
        }
        std::fprintf(stderr,
                     "Publication dates normalised: %zu of the catalog rewritten.\n",
                     rewrites.size());
        return Core::Status::ok();
    });
    if (!work) {
        warn("Publication date migration failed: " + work.error().detail);
        return false;
    }
    return true;
}

bool Connection::migrateDateConstraintsIfNeeded(bool* applied)
{
    *applied = false;
    std::string createSql;
    {
        auto check = m_session->prepare(
            "SELECT sql FROM sqlite_master WHERE type='table' AND name='loans'");
        if (!check) {
            warn("Date constraint migration check failed: " + check.error().detail);
            return false;
        }
        if (!check->next()) {
            *applied = true;
            return true;
        }
        createSql = check->text(0);
    }
    if (createSql.find("date(due_at) IS due_at") != std::string::npos) {
        *applied = true;
        return true;
    }

    const int blocking = countRowsBlockingDateConstraints();
    if (blocking > 0) {
        warn("Skipping the date constraint migration: " + std::to_string(blocking)
             + " row(s) would violate it. The database is unchanged and the app will start.");
        return true;
    }

    m_session->exec("PRAGMA foreign_keys = OFF");
    const bool ok = rebuildTablesWithDateConstraints();
    m_session->exec("PRAGMA foreign_keys = ON");
    *applied = ok;
    return ok;
}

bool Connection::rebuildTablesWithDateConstraints()
{
    const std::vector<std::string> loansRebuild = {
        R"SQL(
            CREATE TABLE loans_new (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                member_id INTEGER NOT NULL REFERENCES members(id),
                book_copy_id INTEGER NOT NULL REFERENCES book_copies(id),
                borrowed_at TEXT NOT NULL DEFAULT (date('now', 'localtime')),
                due_at TEXT NOT NULL,
                returned_at TEXT,
                borrowed_by_employee_id INTEGER REFERENCES employees(id),
                returned_by_employee_id INTEGER REFERENCES employees(id),
                notes TEXT,
                archived_at TEXT CHECK (datetime(archived_at) IS archived_at),
                CHECK (date(borrowed_at) IS borrowed_at),
                CHECK (date(due_at) IS due_at),
                CHECK (date(returned_at) IS returned_at),
                CHECK (date(due_at) > date(borrowed_at)),
                CHECK (returned_at IS NULL OR date(returned_at) >= date(borrowed_at))
            ))SQL",
        "INSERT INTO loans_new (id, member_id, book_copy_id, borrowed_at, due_at, "
        "returned_at, borrowed_by_employee_id, returned_by_employee_id, notes) "
        "SELECT id, member_id, book_copy_id, borrowed_at, due_at, returned_at, "
        "borrowed_by_employee_id, returned_by_employee_id, notes FROM loans",
        "DROP TABLE loans",
        "ALTER TABLE loans_new RENAME TO loans",
        "CREATE INDEX IF NOT EXISTS idx_loans_member ON loans(member_id)",
        "CREATE INDEX IF NOT EXISTS idx_loans_copy ON loans(book_copy_id)",
        "CREATE INDEX IF NOT EXISTS idx_loans_borrowed_at ON loans(borrowed_at)",
        "CREATE INDEX IF NOT EXISTS idx_loans_open ON loans(returned_at)",
        "CREATE INDEX IF NOT EXISTS idx_loans_archived ON loans(archived_at)",
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_loans_one_open_per_copy "
            "ON loans(book_copy_id) WHERE returned_at IS NULL",
    };

    const std::vector<std::string> membersRebuild = {
        R"SQL(
            CREATE TABLE members_new (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                membership_number TEXT NOT NULL UNIQUE,
                first_name TEXT NOT NULL,
                last_name TEXT NOT NULL,
                sex TEXT CHECK (sex IS NULL OR sex IN ('male', 'female')),
                date_of_birth TEXT CHECK (date(date_of_birth) IS date_of_birth),
                phone TEXT,
                address TEXT,
                city TEXT,
                photo_path TEXT,
                id_image_path TEXT,
                notes TEXT,
                registered_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
                updated_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
                email TEXT,
                archived_at TEXT CHECK (datetime(archived_at) IS archived_at),
                occupation TEXT,
                age_group TEXT CHECK (age_group IS NULL OR age_group IN ('youth', 'adult')),
                full_name TEXT,
                source_row INTEGER,
                active_until TEXT CHECK (date(active_until) IS active_until)
            ))SQL",
        "INSERT INTO members_new (id, membership_number, first_name, last_name, sex, "
        "date_of_birth, phone, address, city, photo_path, id_image_path, notes, "
        "registered_at, updated_at, email, archived_at, occupation, age_group, full_name, "
        "source_row, active_until) "
        "SELECT id, membership_number, first_name, last_name, sex, date_of_birth, phone, "
        "address, city, photo_path, id_image_path, notes, registered_at, updated_at, "
        "email, archived_at, NULL, NULL, NULL, NULL, date(registered_at, '+1 year', '-1 day') "
        "FROM members",
        "DROP TABLE members",
        "ALTER TABLE members_new RENAME TO members",
        "CREATE INDEX IF NOT EXISTS idx_members_name ON members(last_name, first_name)",
        "CREATE INDEX IF NOT EXISTS idx_members_number ON members(membership_number)",
        "CREATE INDEX IF NOT EXISTS idx_members_archived ON members(archived_at)",
    };

    const Core::Status work = m_session->transaction([&] {
        if (!execAll(loansRebuild, "Loan date constraint migration")
            || !execAll(membersRebuild, "Member date constraint migration")) {
            return Core::Status::fail(Core::ErrorKind::Sql, "error.sql", m_session->lastError());
        }
        auto fkCheck = m_session->prepare("PRAGMA foreign_key_check");
        if (!fkCheck) {
            return Core::asStatus(fkCheck);
        }
        if (fkCheck->next()) {
            warn("The date constraint migration left a dangling reference in table '"
                 + fkCheck->text(0) + "'; rolling back.");
            return Core::Status::fail(Core::ErrorKind::Sql, "error.sql", "fk check");
        }
        return Core::Status::ok();
    });
    if (!work) {
        warn("Could not finish the date constraint migration: " + work.error().detail);
        return false;
    }
    return true;
}

bool Connection::migrateCatalogIfNeeded()
{
    auto check = m_session->prepare(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='authors'");
    if (check && check->next()) {
        return true;
    }

    m_session->exec("DROP TABLE IF EXISTS loans");
    m_session->exec("DROP TABLE IF EXISTS book_copies");
    m_session->exec("DROP TABLE IF EXISTS books");
    m_session->exec("DROP TABLE IF EXISTS categories");

    const std::string catalogSql = R"SQL(
        CREATE TABLE IF NOT EXISTS authors (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            code TEXT,
            UNIQUE (name, code)
        );
        CREATE TABLE IF NOT EXISTS publishers (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE
        );
        CREATE TABLE IF NOT EXISTS categories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            code TEXT NOT NULL UNIQUE,
            label TEXT
        );
        CREATE TABLE IF NOT EXISTS books (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            author_id INTEGER REFERENCES authors(id),
            publisher_id INTEGER REFERENCES publishers(id),
            category_id INTEGER REFERENCES categories(id),
            isbn TEXT,
            publication_date TEXT,
            place_of_publication TEXT,
            pages TEXT,
            dimensions TEXT,
            language TEXT NOT NULL,
            description TEXT,
            cover_image_path TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
            UNIQUE (title, author_id, publisher_id, isbn, language)
        );
        CREATE INDEX IF NOT EXISTS idx_books_title ON books(title);
        CREATE INDEX IF NOT EXISTS idx_books_category ON books(category_id);
        CREATE INDEX IF NOT EXISTS idx_books_author ON books(author_id);
        CREATE TABLE IF NOT EXISTS book_copies (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
            global_copy_id TEXT NOT NULL UNIQUE,
            source TEXT NOT NULL CHECK (source IN ('arabic', 'foreign')),
            local_id TEXT NOT NULL,
            central_id TEXT,
            classification TEXT,
            subject TEXT,
            notes TEXT,
            inventory_status TEXT,
            compensation TEXT,
            location TEXT,
            index_code TEXT,
            source_row INTEGER,
            UNIQUE (source, local_id)
        );
        CREATE INDEX IF NOT EXISTS idx_copies_book ON book_copies(book_id);
        CREATE INDEX IF NOT EXISTS idx_copies_local ON book_copies(source, local_id);
        CREATE INDEX IF NOT EXISTS idx_copies_central ON book_copies(central_id);
        CREATE TABLE IF NOT EXISTS loans (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            member_id INTEGER NOT NULL REFERENCES members(id),
            book_copy_id INTEGER NOT NULL REFERENCES book_copies(id),
            borrowed_at TEXT NOT NULL DEFAULT (date('now', 'localtime')),
            due_at TEXT NOT NULL,
            returned_at TEXT,
            borrowed_by_employee_id INTEGER REFERENCES employees(id),
            returned_by_employee_id INTEGER REFERENCES employees(id),
            notes TEXT,
            CHECK (returned_at IS NULL OR returned_at >= borrowed_at)
        );
        CREATE INDEX IF NOT EXISTS idx_loans_member ON loans(member_id);
        CREATE INDEX IF NOT EXISTS idx_loans_copy ON loans(book_copy_id);
        CREATE INDEX IF NOT EXISTS idx_loans_borrowed_at ON loans(borrowed_at);
        CREATE INDEX IF NOT EXISTS idx_loans_open ON loans(returned_at);
    )SQL";

    return execSqlScript(catalogSql, "Catalog migration");
}

bool Connection::migrateBookLanguageIfNeeded()
{
    std::string createSql;
    {
        auto check = m_session->prepare(
            "SELECT sql FROM sqlite_master WHERE type='table' AND name='books'");
        if (!check) {
            warn("Book language migration check failed: " + check.error().detail);
            return false;
        }
        if (!check->next()) {
            return true;
        }
        createSql = check->text(0);
    }
    if (createSql.find("language IN ('ar', 'fr')") == std::string::npos) {
        return true;
    }

    const std::string migrationSql = R"SQL(
        CREATE TABLE books_new (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            author_id INTEGER REFERENCES authors(id),
            publisher_id INTEGER REFERENCES publishers(id),
            category_id INTEGER REFERENCES categories(id),
            isbn TEXT,
            publication_date TEXT,
            place_of_publication TEXT,
            pages TEXT,
            dimensions TEXT,
            language TEXT NOT NULL,
            description TEXT,
            cover_image_path TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
            updated_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
            UNIQUE (title, author_id, publisher_id, isbn, language)
        );
        INSERT INTO books_new (
            id, title, author_id, publisher_id, category_id, isbn,
            publication_date, place_of_publication, pages, dimensions,
            language, cover_image_path, created_at, updated_at
        )
        SELECT
            id, title, author_id, publisher_id, category_id, isbn,
            publication_date, place_of_publication, pages, dimensions,
            language, cover_image_path, created_at, updated_at
        FROM books;
        DROP TABLE books;
        ALTER TABLE books_new RENAME TO books;
        CREATE INDEX IF NOT EXISTS idx_books_title ON books(title);
        CREATE INDEX IF NOT EXISTS idx_books_category ON books(category_id);
        CREATE INDEX IF NOT EXISTS idx_books_author ON books(author_id);
    )SQL";

    // One transaction: run statement by statement, a failure after DROP TABLE
    // books left the catalog only in books_new, and books_new then blocked
    // every later launch. book_copies points here, so foreign keys are off for
    // the swap (the pragma is a no-op inside a transaction; left on, the DROP
    // would cascade into book_copies) and foreign_key_check must come back
    // empty before the swap commits.
    if (!m_session->exec("PRAGMA foreign_keys = OFF")) {
        warn("Book language migration could not disable foreign keys: "
             + m_session->lastError());
        return false;
    }
    const Core::Status work = m_session->transaction([&] {
        for (const std::string& statement : SqlText::splitStatements(migrationSql)) {
            if (const Core::Status done = m_session->exec(statement); !done) {
                return done;
            }
        }
        auto check = m_session->prepare("PRAGMA foreign_key_check");
        if (!check) {
            return Core::asStatus(check);
        }
        if (check->next()) {
            return Core::Status::fail(Core::ErrorKind::Sql, "error.sql",
                                "foreign_key_check found rows after the books rebuild");
        }
        return Core::Status::ok();
    });
    const bool keysOn = static_cast<bool>(m_session->exec("PRAGMA foreign_keys = ON"));
    if (!work) {
        warn("Book language migration failed: " + work.error().detail + " "
             + m_session->lastError());
        return false;
    }
    if (!keysOn) {
        warn("Book language migration could not re-enable foreign keys: "
             + m_session->lastError());
        return false;
    }
    return true;
}

bool Connection::migrateBookDescriptionIfNeeded()
{
    auto check = m_session->prepare("PRAGMA table_info(books)");
    if (!check) {
        warn("Book description migration check failed: " + check.error().detail);
        return false;
    }
    while (check->next()) {
        if (check->text(1) == "description") {
            return true;
        }
    }
    if (!m_session->exec("ALTER TABLE books ADD COLUMN description TEXT")) {
        warn("Book description migration failed: " + m_session->lastError());
        return false;
    }
    return true;
}

bool Connection::migrateMemberSexIfNeeded()
{
    if (tableHasColumn("members", "sex")) {
        return true;
    }
    if (!m_session->exec(
            "ALTER TABLE members ADD COLUMN sex TEXT CHECK (sex IS NULL OR sex IN ('male', 'female'))")) {
        warn("Member sex migration failed: " + m_session->lastError());
        return false;
    }
    return true;
}

bool Connection::migrateMemberEmailIfNeeded()
{
    if (tableHasColumn("members", "email")) {
        return true;
    }
    if (!m_session->exec("ALTER TABLE members ADD COLUMN email TEXT")) {
        warn("Member email migration failed: " + m_session->lastError());
        return false;
    }
    return true;
}

bool Connection::migrateMemberArchivedIfNeeded()
{
    if (tableHasColumn("members", "archived_at")) {
        return true;
    }
    if (!m_session->exec("ALTER TABLE members ADD COLUMN archived_at TEXT")) {
        warn("Member archive migration failed: " + m_session->lastError());
        return false;
    }
    if (!m_session->exec("CREATE INDEX IF NOT EXISTS idx_members_archived ON members(archived_at)")) {
        warn("Member archive index creation failed: " + m_session->lastError());
        return false;
    }
    return true;
}

bool Connection::migrateMemberSpreadsheetColumnsIfNeeded()
{
    struct Column {
        const char* name;
        const char* sql;
    };
    static const Column columns[] = {
        {"occupation", "ALTER TABLE members ADD COLUMN occupation TEXT"},
        {"age_group",
         "ALTER TABLE members ADD COLUMN age_group TEXT "
         "CHECK (age_group IS NULL OR age_group IN ('youth', 'adult'))"},
        {"full_name", "ALTER TABLE members ADD COLUMN full_name TEXT"},
        {"source_row", "ALTER TABLE members ADD COLUMN source_row INTEGER"},
    };
    for (const Column& column : columns) {
        if (tableHasColumn("members", column.name)) {
            continue;
        }
        if (!m_session->exec(column.sql)) {
            warn(std::string("Member spreadsheet column '") + column.name
                 + "' migration failed: " + m_session->lastError());
            return false;
        }
    }
    return true;
}

bool Connection::migrateArchiveColumnsIfNeeded()
{
    struct Added {
        const char* table;
        const char* index;
    };
    static const Added added[] = {
        {"books", "CREATE INDEX IF NOT EXISTS idx_books_archived ON books(archived_at)"},
        {"loans", "CREATE INDEX IF NOT EXISTS idx_loans_archived ON loans(archived_at)"},
    };
    for (const Added& table : added) {
        if (tableHasColumn(table.table, "archived_at")) {
            continue;
        }
        // Older fixtures (and some version-0 shapes) never had loans. ALTER
        // would fail with "no such table" and refuse to open the database.
        if (!tableHasColumn(table.table, "id")) {
            continue;
        }
        if (!m_session->exec(std::string("ALTER TABLE ") + table.table
                             + " ADD COLUMN archived_at TEXT "
                               "CHECK (datetime(archived_at) IS archived_at)")
            || !m_session->exec(table.index)) {
            warn(std::string("Archive column migration on ") + table.table
                 + " failed: " + m_session->lastError());
            return false;
        }
    }
    if (tableHasColumn("book_copies", "archived_at")
        || !tableHasColumn("book_copies", "id")) {
        return true;
    }

    // Rebuilt, not altered: local_id and global_copy_id lose NOT NULL so an
    // archived copy can give its number away, and SQLite cannot relax a
    // constraint in place. loans.book_copy_id points here, so foreign keys are
    // off for the swap (the pragma is a no-op inside a transaction) and
    // foreign_key_check must come back empty before the swap commits.
    if (!m_session->exec("PRAGMA foreign_keys = OFF")) {
        warn("Book copy archive migration could not disable foreign keys: "
             + m_session->lastError());
        return false;
    }
    const Core::Status work = m_session->transaction([&] {
        static const char* const steps[] = {
            R"SQL(
                CREATE TABLE book_copies_new (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
                    global_copy_id TEXT UNIQUE,
                    source TEXT NOT NULL CHECK (source IN ('arabic', 'foreign')),
                    local_id TEXT,
                    central_id TEXT,
                    classification TEXT,
                    subject TEXT,
                    notes TEXT,
                    inventory_status TEXT,
                    compensation TEXT,
                    location TEXT,
                    index_code TEXT,
                    source_row INTEGER,
                    archived_at TEXT CHECK (datetime(archived_at) IS archived_at),
                    UNIQUE (source, local_id)
                )
            )SQL",
            "INSERT INTO book_copies_new (id, book_id, global_copy_id, source, local_id, "
            "central_id, classification, subject, notes, inventory_status, compensation, "
            "location, index_code, source_row) "
            "SELECT id, book_id, global_copy_id, source, local_id, central_id, classification, "
            "subject, notes, inventory_status, compensation, location, index_code, source_row "
            "FROM book_copies",
            "DROP TABLE book_copies",
            "ALTER TABLE book_copies_new RENAME TO book_copies",
            "CREATE INDEX IF NOT EXISTS idx_copies_book ON book_copies(book_id)",
            "CREATE INDEX IF NOT EXISTS idx_copies_local ON book_copies(source, local_id)",
            "CREATE INDEX IF NOT EXISTS idx_copies_central ON book_copies(central_id)",
            "CREATE INDEX IF NOT EXISTS idx_copies_archived ON book_copies(archived_at)",
        };
        for (const char* step : steps) {
            if (const Core::Status done = m_session->exec(step); !done) {
                return done;
            }
        }
        auto check = m_session->prepare("PRAGMA foreign_key_check");
        if (!check) {
            return Core::asStatus(check);
        }
        if (check->next()) {
            return Core::Status::fail(Core::ErrorKind::Sql, "error.sql",
                                "foreign_key_check found rows after the book_copies rebuild");
        }
        return Core::Status::ok();
    });
    const bool keysOn = static_cast<bool>(m_session->exec("PRAGMA foreign_keys = ON"));
    if (!work) {
        warn("Book copy archive migration failed: " + work.error().detail + " "
             + m_session->lastError());
        return false;
    }
    if (!keysOn) {
        warn("Book copy archive migration could not re-enable foreign keys: "
             + m_session->lastError());
        return false;
    }
    return true;
}

bool Connection::migrateMemberActiveUntilIfNeeded()
{
    const bool addColumn = !tableHasColumn("members", "active_until");
    const bool dropStatus = tableHasColumn("members", "status");
    if (!addColumn && !dropStatus) {
        return true;
    }
    const Core::Status work = m_session->transaction([&] {
        if (addColumn) {
            if (const Core::Status added = m_session->exec(
                    "ALTER TABLE members ADD COLUMN active_until TEXT "
                    "CHECK (date(active_until) IS active_until)");
                !added) {
                return added;
            }
            // A year from registering, less a day: registered 2025-09-24,
            // active through 2026-09-23. The stored status is deliberately not
            // read -- the year is the rule, and a status nobody expired is not
            // evidence.
            if (const Core::Status filled = m_session->exec(
                    "UPDATE members SET active_until = date(registered_at, '+1 year', '-1 day')");
                !filled) {
                return filled;
            }
        }
        if (dropStatus) {
            // Status is read from active_until now. DROP COLUMN (SQLite 3.35+)
            // refuses an indexed column, so the index goes first; the CHECK is
            // the column's own and goes with it. No rows are deleted, so
            // member_status_history's ON DELETE CASCADE never fires.
            if (const Core::Status dropped = m_session->exec("DROP INDEX IF EXISTS idx_members_status");
                !dropped) {
                return dropped;
            }
            return m_session->exec("ALTER TABLE members DROP COLUMN status");
        }
        return Core::Status::ok();
    });
    if (!work) {
        warn("Member active_until migration failed: " + work.error().detail);
        return false;
    }
    return true;
}

bool Connection::upgradeSchemaIfNeeded()
{
    const int version = schemaVersion();
    if (version == 0) {
        return true;
    }
    // Version 7 was stamped while members.status was still stored. The drop
    // lives in the same migration, which no-ops once active_until exists and
    // status is gone, so a second open does not refill the date or bump the
    // version.
    if (version >= kSchemaVersion) {
        return migrateMemberActiveUntilIfNeeded();
    }
    if (version < 2 && (!migrateMemberEmailIfNeeded() || !setSchemaVersion(2))) {
        return false;
    }
    if (version < 3 && (!normalizePublicationDatesFromOriginals() || !setSchemaVersion(3))) {
        return false;
    }
    if (version < 4 && (!migrateMemberArchivedIfNeeded() || !setSchemaVersion(4))) {
        return false;
    }
    if (version < 5 && (!migrateMemberSpreadsheetColumnsIfNeeded() || !setSchemaVersion(5))) {
        return false;
    }
    if (version < 6 && (!migrateArchiveColumnsIfNeeded() || !setSchemaVersion(6))) {
        return false;
    }
    if (version < 7 && (!migrateMemberActiveUntilIfNeeded() || !setSchemaVersion(7))) {
        return false;
    }
    return true;
}

bool Connection::normalizePublicationDatesFromOriginals()
{
    if (!tableHasColumn("books", "publication_date_original")) {
        return true;
    }

    auto read = m_session->prepare(
        "SELECT id, publication_date, publication_date_original "
        "FROM books WHERE publication_date_original IS NOT NULL");
    if (!read) {
        warn("Publication date normalisation read failed: " + read.error().detail);
        return false;
    }

    struct Rewrite {
        std::int64_t id;
        std::string value;
    };
    std::vector<Rewrite> rewrites;
    while (read->next()) {
        const std::string stored = read->text(1);
        const std::string original = read->text(2);
        const std::string normalised = Core::DateText::normalizePublicationDate(original);
        if (normalised != stored) {
            rewrites.push_back({read->int64(0), normalised});
        }
    }
    if (rewrites.empty()) {
        return true;
    }

    const Core::Status work = m_session->transaction([&] {
        auto update = m_session->prepare("UPDATE books SET publication_date = :value WHERE id = :id");
        if (!update) {
            return Core::asStatus(update);
        }
        for (const auto& [id, value] : rewrites) {
            update->reset();
            if (value.empty()) {
                if (!update->bindNull(":value")) {
                    return Core::Status::fail(Core::ErrorKind::Sql, "error.sql");
                }
            } else if (!update->bind(":value", value)) {
                return Core::Status::fail(Core::ErrorKind::Sql, "error.sql");
            }
            if (!update->bind(":id", id) || !update->exec()) {
                return Core::Status::fail(Core::ErrorKind::Sql, "error.sql", m_session->lastError());
            }
        }
        return Core::Status::ok();
    });
    if (!work) {
        warn("Publication date normalisation failed: " + work.error().detail);
        return false;
    }
    std::fprintf(stderr,
                 "Publication dates normalised from originals: %zu rewritten.\n",
                 rewrites.size());
    return true;
}

bool Connection::applySchema()
{
    auto existsQuery = m_session->prepare(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='members'");
    if (existsQuery && existsQuery->next()) {
        return true;
    }

    const std::string path = bundledSchemaPath();
    const std::string sql = readFile(path);
    if (sql.empty()) {
        warn("Schema file not found: " + path);
        return false;
    }
    return execSqlScript(sql, "Schema apply");
}

bool Connection::ensureDefaultEmployee()
{
    auto countQuery = m_session->prepare("SELECT COUNT(*) FROM employees");
    if (!countQuery || !countQuery->next() || countQuery->integer(0) > 0) {
        return true;
    }

    auto insert = m_session->prepare(
        "INSERT INTO employees (username, password_hash, first_name, last_name) "
        "VALUES (:username, :password_hash, :first_name, :last_name)");
    if (!insert) {
        warn("Default employee insert failed: " + insert.error().detail);
        return false;
    }
    if (!insert->bind(":username", std::string_view{"admin"})
        || !insert->bind(":password_hash", std::string_view{"admin"})
        || !insert->bind(":first_name", std::string_view{"Admin"})
        || !insert->bind(":last_name", std::string_view{"User"})
        || !insert->exec()) {
        warn("Default employee insert failed: " + m_session->lastError());
        return false;
    }
    return true;
}

}  // namespace VLMS::Database
