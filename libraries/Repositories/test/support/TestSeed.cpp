#include "TestSeed.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <VLMS/Database/SqliteSession.h>

#include <cstdio>
#include <iomanip>
#include <sstream>

namespace Test {

MemberInput MemberSeed::toInput() const
{
    MemberInput input;
    input.membershipNumber = membershipNumber;
    input.firstName = firstName;
    input.lastName = lastName;
    input.sex = sex;
    input.dateOfBirth = dateOfBirth;
    input.email = email;
    input.phone = phone;
    input.address = address;
    input.city = city;
    input.status = status;
    input.notes = notes;
    input.occupation = occupation;
    input.fullName = fullName;
    return input;
}

BookInput BookSeed::toInput() const
{
    BookInput input;
    input.title = title;
    input.authorName = authorName;
    input.publisherName = publisherName;
    input.categoryId = categoryId;
    input.isbn = isbn;
    input.publicationDate = publicationDate;
    input.placeOfPublication = placeOfPublication;
    input.pages = pages;
    input.dimensions = dimensions;
    input.language = language;
    input.description = description;
    input.initialCopyCount = initialCopyCount;
    return input;
}

MemberSeed uniqueMemberSeed(int index)
{
    MemberSeed seed;
    std::ostringstream number;
    number << "T-" << std::setw(5) << std::setfill('0') << index;
    seed.membershipNumber = number.str();
    seed.firstName = "Member" + std::to_string(index);
    seed.lastName = "Test";
    return seed;
}

BookSeed uniqueBookSeed(int index)
{
    BookSeed seed;
    seed.title = "Test Book " + std::to_string(index);
    std::ostringstream isbn;
    isbn << "978-0-" << std::setw(6) << std::setfill('0') << index;
    seed.isbn = isbn.str();
    return seed;
}

std::int64_t seedMember(TestDatabase& db, const MemberSeed& seed)
{
    MemberRepository repository(db.session(), db.resourcesDirectory());
    const auto created = repository.createMember(seed.toInput());
    if (!created) {
        std::fprintf(stderr, "seedMember failed: %s\n", created.error().key.c_str());
        return 0;
    }
    return created.value();
}

std::int64_t seedBook(TestDatabase& db, const BookSeed& seed)
{
    CatalogRepository repository(db.session(), db.resourcesDirectory());
    const auto created = repository.createBook(seed.toInput());
    if (!created) {
        std::fprintf(stderr, "seedBook failed: %s\n", created.error().key.c_str());
        return 0;
    }
    return created.value();
}

std::int64_t seedCategory(TestDatabase& db, std::string_view code, std::string_view label)
{
    CatalogRepository repository(db.session(), db.resourcesDirectory());
    const auto created = repository.createCategory(std::string(code), std::string(label));
    if (!created) {
        std::fprintf(stderr, "seedCategory failed: %s\n", created.error().key.c_str());
        return 0;
    }
    return created.value();
}

std::vector<std::int64_t> copyIdsOf(const TestDatabase& db, std::int64_t bookId)
{
    auto query = db.session().prepare("SELECT id FROM book_copies WHERE book_id = :book_id ORDER BY id");
    std::vector<std::int64_t> ids;
    if (!query || !query->bind(":book_id", bookId)) {
        return ids;
    }
    while (query->next()) {
        ids.push_back(query->int64(0));
    }
    return ids;
}

std::int64_t rawInsertLoan(const TestDatabase& db,
                           std::int64_t memberId,
                           std::int64_t bookCopyId,
                           const std::string& borrowedAt,
                           const std::string& dueAt,
                           const std::string& returnedAt)
{
    auto query = db.session().prepare(R"SQL(
        INSERT INTO loans (member_id, book_copy_id, borrowed_at, due_at, returned_at)
        VALUES (:member_id, :book_copy_id, :borrowed_at, :due_at, :returned_at)
    )SQL");
    if (!query) {
        std::fprintf(stderr, "rawInsertLoan failed: %s\n", query.error().detail.c_str());
        return 0;
    }
    if (!query->bind(":member_id", memberId)
        || !query->bind(":book_copy_id", bookCopyId)
        || !query->bind(":borrowed_at", borrowedAt)
        || !query->bind(":due_at", dueAt)) {
        std::fprintf(stderr, "rawInsertLoan failed: %s\n", db.session().lastError().c_str());
        return 0;
    }
    if (returnedAt.empty()) {
        if (!query->bindNull(":returned_at")) {
            std::fprintf(stderr, "rawInsertLoan failed: %s\n", db.session().lastError().c_str());
            return 0;
        }
    } else if (!query->bind(":returned_at", returnedAt)) {
        std::fprintf(stderr, "rawInsertLoan failed: %s\n", db.session().lastError().c_str());
        return 0;
    }
    if (!query->exec()) {
        std::fprintf(stderr, "rawInsertLoan failed: %s\n", query.error().detail.c_str());
        return 0;
    }
    return db.session().lastInsertRowId();
}

bool degradeLoansToPreV1Shape(const TestDatabase& db)
{
    const std::vector<std::string> statements = {
        R"SQL(
            CREATE TABLE loans_pre_v1 (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                member_id INTEGER NOT NULL REFERENCES members(id),
                book_copy_id INTEGER NOT NULL REFERENCES book_copies(id),
                borrowed_at TEXT NOT NULL DEFAULT (date('now', 'localtime')),
                due_at TEXT NOT NULL,
                returned_at TEXT,
                borrowed_by_employee_id INTEGER REFERENCES employees(id),
                returned_by_employee_id INTEGER REFERENCES employees(id),
                notes TEXT,
                archived_at TEXT,
                CHECK (returned_at IS NULL OR returned_at >= borrowed_at)
            ))SQL",
        "INSERT INTO loans_pre_v1 (id, member_id, book_copy_id, borrowed_at, due_at, "
        "returned_at, borrowed_by_employee_id, returned_by_employee_id, notes, archived_at) "
        "SELECT id, member_id, book_copy_id, borrowed_at, due_at, returned_at, "
        "borrowed_by_employee_id, returned_by_employee_id, notes, archived_at FROM loans",
        "DROP TABLE loans",
        "ALTER TABLE loans_pre_v1 RENAME TO loans",
        "CREATE INDEX IF NOT EXISTS idx_loans_member ON loans(member_id)",
        "CREATE INDEX IF NOT EXISTS idx_loans_copy ON loans(book_copy_id)",
        "CREATE INDEX IF NOT EXISTS idx_loans_borrowed_at ON loans(borrowed_at)",
        "CREATE INDEX IF NOT EXISTS idx_loans_open ON loans(returned_at)",
    };

    for (const std::string& statement : statements) {
        if (!db.exec(statement)) {
            std::fprintf(stderr, "degradeLoansToPreV1Shape failed: %s\n", db.lastError().c_str());
            return false;
        }
    }
    return true;
}

bool rawSetRegisteredAt(const TestDatabase& db, std::int64_t memberId, const std::string& value)
{
    return db.execBound("UPDATE members SET registered_at = :value WHERE id = :id",
                        {{"value", value}, {"id", memberId}});
}

bool rawSetPublicationDate(const TestDatabase& db, std::int64_t bookId, const std::string& value)
{
    return db.execBound("UPDATE books SET publication_date = :value WHERE id = :id",
                        {{"value", value}, {"id", bookId}});
}

bool rawSetCopyLocalId(const TestDatabase& db, std::int64_t copyId, const std::string& value)
{
    return db.execBound("UPDATE book_copies SET local_id = :value WHERE id = :id",
                        {{"value", value}, {"id", copyId}});
}

std::vector<HostilePayload> hostilePayloads()
{
    return {
        {"dropTable", "'; DROP TABLE books;--"},
        {"dropTableNoQuote", "; DROP TABLE books;--"},
        {"dropLoans", "'; DELETE FROM loans;--"},
        {"unionSelect", "' UNION SELECT 1,2,3--"},
        {"orTrue", "' OR '1'='1"},
        {"commentTail", "admin'--"},
        {"doubleQuote", "\" OR \"\"=\""},
        {"stackedPragma", "'; PRAGMA writable_schema = 1;--"},
        {"percent", "%"},
        {"underscore", "_"},
        {"backslash", "\\"},
        {"backslashPercent", "\\%"},
        {"escapeCombo", "100\\%_x"},
        {"arabic", "كتاب' OR 1=1--"},
        {"frenchApostrophe", "L'Étranger", true},
        {"nulByte", std::string("a\0b", 3)},
        {"newline", "line1\nDROP TABLE books;"},
        {"longQuotes", std::string(10000, '\'')},
        {"emoji", "📚"},
        {"rtlOverride", "abc\u202Edef"},
    };
}

bool schemaIsIntact(const TestDatabase& db,
                    const std::vector<std::string>& expectedTables,
                    std::string* whatChanged)
{
    const std::vector<std::string> actual = db.tableNames();
    for (const std::string& table : expectedTables) {
        if (!contains(actual, table)) {
            if (whatChanged != nullptr) {
                std::string have;
                for (std::size_t i = 0; i < actual.size(); ++i) {
                    if (i > 0) {
                        have += ", ";
                    }
                    have += actual[i];
                }
                *whatChanged = "table '" + table + "' no longer exists (have: " + have + ")";
            }
            return false;
        }
    }
    return true;
}

}  // namespace Test
