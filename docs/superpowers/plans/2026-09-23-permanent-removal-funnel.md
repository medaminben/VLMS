# Permanent Removal Funnel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Delete archives everywhere; permanent removal exists only in the Archive, gated on what still points at the record.

**Architecture:** Four `purge*` repository calls, each refusing a record that is not archived and each gated on the rows beneath it (loans for a member or copy, copies for a book, nothing for a loan). The Archive page enables its new `Permanently remove` button from a count it already displays in a column, so the gate is visible on the row; Core enforces the same rules again on the way in. A copy's local number is freed by its own deletion, because "free" is computed — a number no `book_copies` row holds — rather than stored.

**Tech Stack:** Qt 6 Widgets, C++17, SQLite via `VLMS::SqliteSession`, GoogleTest.

## Global Constraints

- **No schema change.** No migration, no new table. Every gate is answered by the existing tables.
- **Repositories are Qt-free.** Core uses `std::string`, `std::vector`, `std::int64_t`, and returns `VLMS::Result<T>` / `Status` — never a raw driver string. Errors carry a key that `Strings::t()` translates.
- **Every new user-visible string is three entries**, added to the Arabic, French and English tables in `libraries/Core/src/Strings.cpp`. `test_core_StringsParity` fails if a key is missing from any table.
- **British English in prose and comments** (catalogue, not catalog) — never in Qt API names or existing identifiers, which stay as they are (`CatalogRepository`, `CatalogPage`).
- **Judge Core tests by `ctest`, never by running the binary.** `./build/bin/test_vlms_core` fails 16 tests on its own: the schema path, data directory and TZ come from per-test `ENVIRONMENT` in `cmake/TestUtils.cmake`.
- Build with `cmake --build build -j`. Run one suite with `ctest --test-dir build -R '<regex>' --output-on-failure`.
- `createBook` rejects `initialCopyCount < 1` with `error.book.minCopies`. To seed a copy-less book, seed one copy and archive it with `saveCopies(bookId, {})`.
- **Every purge test seeds a second record that must survive.** A test with one member or one book in the database cannot fail — the same trap the loan-history tests hit, where every loan belonged to the single seeded book.

**Spec:** `docs/superpowers/specs/2026-09-23-permanent-removal-funnel-design.md`

---

## File Structure

**Core — new behaviour**

| File | Responsibility |
|---|---|
| `libraries/Core/include/VLMS/Core/CirculationRepository.h` / `src/CirculationRepository.cpp` | `purgeLoan` |
| `libraries/Core/include/VLMS/Core/CatalogRepository.h` / `src/CatalogRepository.cpp` | `purgeCopy`, `purgeBook`, `bookHasOpenLoans`, `listFreeLocalNumbers` (facade) |
| `libraries/Core/src/BookCopyStore.h` / `.cpp` | `purgeCopy`, `freeLocalNumbers` implementations; copy `loanCount` column and its sort |
| `libraries/Core/src/MemberRepository.cpp` | `purgeMember` tightened to require archived; member `loanCount` column |
| `libraries/Core/src/MemberSql.cpp` | `MemberSort::kAllLoans` order expression |
| `libraries/Core/include/VLMS/Core/MemberTypes.h` | `MemberRecord::loanCount`, `MemberSort::kAllLoans` |
| `libraries/Core/include/VLMS/Core/CatalogTypes.h` | `BookCopyRecord::loanCount`, `CopySort::kLoans` |
| `libraries/Core/src/Strings.cpp` | every new key, three tables |

**UI**

| File | Responsibility |
|---|---|
| `applications/vlms/src/ui/archive/ArchivePage.h` / `.cpp` | `Permanently remove` button, its gate, the two new Loans columns |
| `applications/vlms/src/ui/members/MembersPage.cpp` | delete archives, no checkbox |
| `applications/vlms/src/ui/catalog/CatalogPage.cpp` | open-loan pre-check before the confirmation |
| `applications/vlms/src/ui/catalog/FreeLocalNumberDelegate.h` / `.cpp` | **new** — editable combo of free numbers on a new copy row |
| `applications/vlms/src/ui/catalog/BookCopiesTable.cpp` | installs the delegate, fills its role on `addRow` |

**Tests**

| File | Responsibility |
|---|---|
| `libraries/Core/test/src/test_archive_purge.cpp` | **new** — all four purges and the funnel walked end to end |
| `libraries/Core/test/src/test_free_local_numbers.cpp` | **new** — gaps, per stock, empty, cap |
| `applications/vlms/test/src/ModalTest.h` | **new** — modal helpers extracted from `test_member_removal.cpp` |
| `applications/vlms/test/src/test_archive_page.cpp` | button gate per type, new columns |
| `applications/vlms/test/src/test_member_removal.cpp` | rewritten for the checkbox-free flow |
| `applications/vlms/test/src/test_catalog_removal.cpp` | **new** — catalogue refuses before it confirms |
| `applications/vlms/test/src/test_free_number_picker.cpp` | **new** — the copies table's drop-down |

Task order matters: Tasks 1–4 are the Core gates (bottom of the funnel upward), Task 5 is the data the UI gate reads, Task 6 is independent, Tasks 7–10 are the UI, Task 11 closes out.

---

### Task 1: `purgeLoan` — the bottom of the funnel

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/CirculationRepository.h:29`
- Modify: `libraries/Core/src/CirculationRepository.cpp` (after `restoreLoan`)
- Modify: `libraries/Core/src/Strings.cpp` (three tables)
- Create: `libraries/Core/test/src/test_archive_purge.cpp`
- Modify: `libraries/Core/test/CMakeLists.txt` (`TST_SOURCES`)

**Interfaces:**
- Consumes: nothing.
- Produces: `VLMS::Status CirculationRepository::purgeLoan(std::int64_t loanId);` — `ok()` on success, `Validation("error.loan.notArchived")` when the loan is live, `NotFound("error.loan.notFound")` when there is no such loan.

- [ ] **Step 1: Add the three string entries**

In `libraries/Core/src/Strings.cpp`, add one line to each of the three tables, immediately after the existing `{"error.loan.notFound", ...}` entry in that same table:

Arabic table:
```cpp
        {"error.loan.notArchived", "هذه الإعارة ليست في الأرشيف."},
```
French table:
```cpp
        {"error.loan.notArchived", "Ce prêt n'est pas archivé."},
```
English table:
```cpp
        {"error.loan.notArchived", "This loan is not in the Archive."},
```

- [ ] **Step 2: Write the failing tests**

Create `libraries/Core/test/src/test_archive_purge.cpp`:

```cpp
#include "TestDatabase.h"
#include "TestSeed.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/MemberRepository.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace VLMS::Test;

/// One fixture for the whole funnel: a purge is always a question about the
/// rows beneath a record, so every test needs all three repositories.
class test_core_ArchivePurge : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_circulation.reset();
        m_catalog.reset();
        m_members.reset();
        m_db.reset();
    }

    /// A returned loan, already archived, so it is ready to be purged.
    std::int64_t seedArchivedLoan(int index)
    {
        const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(index));
        const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(index));
        const std::int64_t loanId =
            rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                          "2026-09-15", "2026-09-10");
        EXPECT_GT(loanId, 0);
        EXPECT_TRUE(m_circulation->archiveLoan(loanId));
        return loanId;
    }

    [[nodiscard]] bool loanExists(std::int64_t loanId) const
    {
        return m_db->scalar("SELECT COUNT(*) FROM loans WHERE id = " + std::to_string(loanId))
                   .toInt()
            > 0;
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MemberRepository> m_members;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<CirculationRepository> m_circulation;
};

TEST_F(test_core_ArchivePurge, PurgeLoanRemovesAnArchivedLoanAndLeavesTheOthers)
{
    const std::int64_t purged = seedArchivedLoan(1);
    const std::int64_t bystander = seedArchivedLoan(2);

    ASSERT_TRUE(m_circulation->purgeLoan(purged));

    EXPECT_FALSE(loanExists(purged));
    EXPECT_TRUE(loanExists(bystander));
}

TEST_F(test_core_ArchivePurge, PurgeLoanIsRefusedWhileTheLoanIsStillLive)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(3));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(3));
    const std::int64_t loanId = rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(),
                                              "2026-09-01", "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);

    const auto refused = m_circulation->purgeLoan(loanId);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.loan.notArchived");
    EXPECT_TRUE(loanExists(loanId));
}

TEST_F(test_core_ArchivePurge, PurgeLoanReportsAMissingLoanRatherThanSucceeding)
{
    const std::int64_t bystander = seedArchivedLoan(4);

    const auto missing = m_circulation->purgeLoan(bystander + 1000);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().key, "error.loan.notFound");
    EXPECT_TRUE(loanExists(bystander));
}
```

- [ ] **Step 3: Register the test file**

In `libraries/Core/test/CMakeLists.txt`, add to `TST_SOURCES` immediately after `src/test_archive_circulation.cpp`:

```cmake
    src/test_archive_purge.cpp
```

- [ ] **Step 4: Run the tests to verify they fail**

```bash
cmake --build build -j 2>&1 | tail -20
```
Expected: a compile error — `'purgeLoan' is not a member of 'CirculationRepository'`.

- [ ] **Step 5: Declare `purgeLoan`**

In `libraries/Core/include/VLMS/Core/CirculationRepository.h`, immediately after the `restoreLoan` declaration:

```cpp
    /// Destroys an archived loan. The bottom of the funnel: a loan has nothing
    /// beneath it, so being archived is the only condition.
    [[nodiscard]] VLMS::Status purgeLoan(std::int64_t loanId);
```

- [ ] **Step 6: Implement `purgeLoan`**

In `libraries/Core/src/CirculationRepository.cpp`, after `restoreLoan`'s closing brace:

```cpp
Status CirculationRepository::purgeLoan(const std::int64_t loanId)
{
    auto read = m_session.prepare("SELECT archived_at IS NOT NULL FROM loans WHERE id = :id");
    if (!read) {
        return RepoSql::sqlFailure(read.error().detail);
    }
    if (!read->bind(":id", loanId)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (!read->next()) {
        if (!read->ok()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return RepoSql::notFound("error.loan.notFound");
    }
    // Permanent removal belongs to the Archive alone: a live loan is deleted
    // from the Circulation page only in the sense of being put down first.
    if (read->integer(0) == 0) {
        return RepoSql::validation("error.loan.notArchived");
    }

    auto remove = m_session.prepare("DELETE FROM loans WHERE id = :id");
    if (!remove) {
        return RepoSql::sqlFailure(remove.error().detail);
    }
    if (!remove->bind(":id", loanId) || !remove->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (remove->changes() <= 0) {
        return RepoSql::notFound("error.loan.notFound");
    }
    return Status::ok();
}
```

- [ ] **Step 7: Build and run**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_core_ArchivePurge|^test_core_StringsParity' --output-on-failure
```
Expected: 3 purge tests plus the parity tests, all passing.

- [ ] **Step 8: Commit**

```bash
git add libraries/Core/include/VLMS/Core/CirculationRepository.h \
        libraries/Core/src/CirculationRepository.cpp \
        libraries/Core/src/Strings.cpp \
        libraries/Core/test/src/test_archive_purge.cpp \
        libraries/Core/test/CMakeLists.txt
git commit -m "Destroy an archived loan.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: `purgeCopy` — a copy goes when no loan names it

**Files:**
- Modify: `libraries/Core/src/BookCopyStore.h` (after `releaseArchivedNumber`)
- Modify: `libraries/Core/src/BookCopyStore.cpp`
- Modify: `libraries/Core/include/VLMS/Core/CatalogRepository.h:37` (after `restoreCopy`)
- Modify: `libraries/Core/src/CatalogRepository.cpp` (after `restoreCopy`)
- Modify: `libraries/Core/src/Strings.cpp` (three tables)
- Modify: `libraries/Core/test/src/test_archive_purge.cpp`

**Interfaces:**
- Consumes: the `test_core_ArchivePurge` fixture from Task 1.
- Produces: `VLMS::Status CatalogRepository::purgeCopy(std::int64_t copyId);` — `Validation("error.copy.notArchived")` when live, `Validation("error.copy.hasHistory")` when any loan row names it, `NotFound("error.copy.notFound")` when absent. `BookCopyStore::purgeCopy(std::int64_t)` does the work; the caller holds the transaction.

- [ ] **Step 1: Add the three string entries**

`error.copy.notArchived` already exists in all three tables. Add only `error.copy.hasHistory`, immediately after each table's existing `{"error.copy.notArchived", ...}` line:

Arabic:
```cpp
        {"error.copy.hasHistory", "هذه النسخة أُعيرت من قبل؛ احذف إعاراتها من الأرشيف أولًا."},
```
French:
```cpp
        {"error.copy.hasHistory",
         "Cet exemplaire a été emprunté ; supprimez d'abord ses prêts archivés."},
```
English:
```cpp
        {"error.copy.hasHistory",
         "This copy has been borrowed; remove its archived loans first."},
```

- [ ] **Step 2: Write the failing tests**

Append to `libraries/Core/test/src/test_archive_purge.cpp`:

```cpp
TEST_F(test_core_ArchivePurge, PurgeCopyRemovesAnArchivedCopyAndLeavesItsBookAndTheOtherCopy)
{
    BookSeed seed = uniqueBookSeed(10);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copies.size(), 2u);
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    ASSERT_TRUE(m_catalog->purgeCopy(copies.at(0)));

    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = "
                           + std::to_string(copies.at(0)))
                  .toInt(),
              0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = "
                           + std::to_string(copies.at(1)))
                  .toInt(),
              1);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bookId))
                  .toInt(),
              1);
}

TEST_F(test_core_ArchivePurge, PurgeCopyIsRefusedWhileALoanNamesIt)
{
    BookSeed seed = uniqueBookSeed(11);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(11));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copies.at(0), "2026-09-01", "2026-09-15", "2026-09-10"),
              0);
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    const auto refused = m_catalog->purgeCopy(copies.at(0));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.copy.hasHistory");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = "
                           + std::to_string(copies.at(0)))
                  .toInt(),
              1);

    // The copy nobody borrowed still goes: the gate is per copy, not per book.
    EXPECT_TRUE(m_catalog->purgeCopy(copies.at(1)));
}

TEST_F(test_core_ArchivePurge, PurgeCopyIsRefusedWhileTheCopyIsStillLive)
{
    BookSeed seed = uniqueBookSeed(12);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);

    const auto refused = m_catalog->purgeCopy(copies.at(0));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.copy.notArchived");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE book_id = "
                           + std::to_string(bookId))
                  .toInt(),
              2);
}

TEST_F(test_core_ArchivePurge, PurgeCopyCountsArchivedLoansToo)
{
    BookSeed seed = uniqueBookSeed(13);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(13));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copies.at(0), "2026-09-01", "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    // Archiving the loan does not unblock the copy -- the funnel has to be
    // walked, not stepped around.
    const auto refused = m_catalog->purgeCopy(copies.at(0));
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.copy.hasHistory");

    ASSERT_TRUE(m_circulation->purgeLoan(loanId));
    EXPECT_TRUE(m_catalog->purgeCopy(copies.at(0)));
}
```

- [ ] **Step 3: Run to verify it fails**

```bash
cmake --build build -j 2>&1 | tail -20
```
Expected: `'purgeCopy' is not a member of 'CatalogRepository'`.

- [ ] **Step 4: Implement in `BookCopyStore`**

Declare in `libraries/Core/src/BookCopyStore.h`, after `releaseArchivedNumber`:

```cpp
    /// Destroys an archived copy that no loan names. Its local number becomes
    /// free again by the deletion itself -- free means a number no row holds,
    /// so there is no pool to write to. The caller holds the transaction.
    [[nodiscard]] VLMS::Status purgeCopy(std::int64_t copyId);
```

Implement in `libraries/Core/src/BookCopyStore.cpp`, after `restoreCopy`'s closing brace:

```cpp
Status BookCopyStore::purgeCopy(const std::int64_t copyId)
{
    {
        auto read =
            m_session.prepare("SELECT archived_at IS NOT NULL FROM book_copies WHERE id = :id");
        if (!read) {
            return RepoSql::sqlFailure(read.error().detail);
        }
        if (!read->bind(":id", copyId)) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (!read->next()) {
            if (!read->ok()) {
                return RepoSql::sqlFailure(m_session.lastError());
            }
            return RepoSql::notFound("error.copy.notFound");
        }
        if (read->integer(0) == 0) {
            return RepoSql::validation("error.copy.notArchived");
        }
    }

    {
        // Every loan row, archived ones included: an archived loan still points
        // here, and loans.book_copy_id has no ON DELETE clause, so SQLite would
        // refuse anyway -- in English, from a driver the librarian never sees.
        auto loans =
            m_session.prepare("SELECT COUNT(*) FROM loans WHERE book_copy_id = :id");
        if (!loans) {
            return RepoSql::sqlFailure(loans.error().detail);
        }
        if (!loans->bind(":id", copyId) || !loans->next()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (loans->integer(0) > 0) {
            return RepoSql::validation("error.copy.hasHistory");
        }
    }

    auto remove = m_session.prepare("DELETE FROM book_copies WHERE id = :id");
    if (!remove) {
        return RepoSql::sqlFailure(remove.error().detail);
    }
    if (!remove->bind(":id", copyId) || !remove->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (remove->changes() <= 0) {
        return RepoSql::notFound("error.copy.notFound");
    }
    return Status::ok();
}
```

- [ ] **Step 5: Add the facade**

Declare in `libraries/Core/include/VLMS/Core/CatalogRepository.h`, after `restoreCopy`:

```cpp
    [[nodiscard]] VLMS::Status purgeCopy(std::int64_t copyId);
```

Implement in `libraries/Core/src/CatalogRepository.cpp`, after `restoreCopy`:

```cpp
Status CatalogRepository::purgeCopy(const std::int64_t copyId)
{
    return m_session.transaction([&] { return m_copies->purgeCopy(copyId); });
}
```

- [ ] **Step 6: Build and run**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_core_ArchivePurge|^test_core_StringsParity|^test_core_ArchiveCatalog' --output-on-failure
```
Expected: all passing.

- [ ] **Step 7: Commit**

```bash
git add libraries/Core/src/BookCopyStore.h libraries/Core/src/BookCopyStore.cpp \
        libraries/Core/include/VLMS/Core/CatalogRepository.h \
        libraries/Core/src/CatalogRepository.cpp libraries/Core/src/Strings.cpp \
        libraries/Core/test/src/test_archive_purge.cpp
git commit -m "Destroy an archived copy no loan names.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: `purgeBook` — a title goes when it has no copies left

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/CatalogRepository.h:36` (after `restoreBook`)
- Modify: `libraries/Core/src/CatalogRepository.cpp` (after `archiveBook`, and a new `purgeBook`)
- Modify: `libraries/Core/src/Strings.cpp` (three tables)
- Modify: `libraries/Core/test/src/test_archive_purge.cpp`

**Interfaces:**
- Consumes: `CatalogRepository::purgeCopy` (Task 2), the Task 1 fixture.
- Produces:
  - `VLMS::Status CatalogRepository::purgeBook(std::int64_t id);` — `Validation("error.book.notArchived")`, `Validation("error.book.hasCopies")`, `NotFound("error.book.notFound")`.
  - `VLMS::Result<bool> CatalogRepository::bookHasOpenLoans(std::int64_t id) const;` — true when any live copy of the book is out. Task 9 uses it.

- [ ] **Step 1: Add the string entries**

In each of the three tables, after that table's `{"error.book.hasActiveLoans", ...}` entry:

Arabic:
```cpp
        {"error.book.hasCopies", "أزل نسخ هذا الكتاب المؤرشفة قبل حذف العنوان نهائيًا."},
        {"error.book.notArchived", "هذا الكتاب ليس في الأرشيف."},
```
French:
```cpp
        {"error.book.hasCopies",
         "Supprimez les exemplaires archivés avant de supprimer définitivement l'ouvrage."},
        {"error.book.notArchived", "Cet ouvrage n'est pas archivé."},
```
English:
```cpp
        {"error.book.hasCopies",
         "Remove this title's archived copies before removing the title itself."},
        {"error.book.notArchived", "This book is not in the Archive."},
```

- [ ] **Step 2: Write the failing tests**

Append to `libraries/Core/test/src/test_archive_purge.cpp`:

```cpp
TEST_F(test_core_ArchivePurge, PurgeBookRemovesAnArchivedTitleThatHasNoCopiesLeft)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(20));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(21));
    const std::int64_t copyId = copyIdsOf(*m_db, bookId).front();
    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    ASSERT_TRUE(m_catalog->purgeCopy(copyId));

    ASSERT_TRUE(m_catalog->purgeBook(bookId));

    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bookId))
                  .toInt(),
              0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bystander))
                  .toInt(),
              1);
}

TEST_F(test_core_ArchivePurge, PurgeBookIsRefusedWhileAnyCopyRemains)
{
    BookSeed seed = uniqueBookSeed(22);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    ASSERT_TRUE(m_catalog->purgeCopy(copies.at(0)));

    const auto refused = m_catalog->purgeBook(bookId);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.book.hasCopies");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bookId))
                  .toInt(),
              1);

    ASSERT_TRUE(m_catalog->purgeCopy(copies.at(1)));
    EXPECT_TRUE(m_catalog->purgeBook(bookId));
}

TEST_F(test_core_ArchivePurge, PurgeBookIsRefusedWhileTheTitleIsStillLive)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(23));

    const auto refused = m_catalog->purgeBook(bookId);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.book.notArchived");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(bookId))
                  .toInt(),
              1);
}

TEST_F(test_core_ArchivePurge, PurgeBookTakesTheCoverFolderWithIt)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(24));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(25));
    const std::filesystem::path folder =
        std::filesystem::path(m_db->resourcesDirectory()) / "books" / std::to_string(bookId);
    const std::filesystem::path keptFolder =
        std::filesystem::path(m_db->resourcesDirectory()) / "books" / std::to_string(bystander);
    std::filesystem::create_directories(folder);
    std::filesystem::create_directories(keptFolder);
    std::ofstream(folder / "cover.jpg") << "jpeg";
    std::ofstream(keptFolder / "cover.jpg") << "jpeg";

    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    ASSERT_TRUE(m_catalog->purgeCopy(copyIdsOf(*m_db, bookId).front()));
    ASSERT_TRUE(m_catalog->purgeBook(bookId));

    EXPECT_FALSE(std::filesystem::exists(folder));
    EXPECT_TRUE(std::filesystem::exists(keptFolder / "cover.jpg"));
}

TEST_F(test_core_ArchivePurge, BookHasOpenLoansAnswersForTheCatalogueBeforeItConfirms)
{
    BookSeed seed = uniqueBookSeed(26);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const std::int64_t quiet = seedBook(*m_db, uniqueBookSeed(27));
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(26));

    EXPECT_FALSE(VLMS_UNWRAP(m_catalog->bookHasOpenLoans(bookId)));

    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                            "2026-09-15"),
              0);
    EXPECT_TRUE(VLMS_UNWRAP(m_catalog->bookHasOpenLoans(bookId)));
    EXPECT_FALSE(VLMS_UNWRAP(m_catalog->bookHasOpenLoans(quiet)));
}
```

Add these includes at the top of the file, after `#include <memory>`:

```cpp
#include <filesystem>
#include <fstream>
```

- [ ] **Step 3: Run to verify it fails**

```bash
cmake --build build -j 2>&1 | tail -20
```
Expected: `'purgeBook' is not a member of 'CatalogRepository'`.

- [ ] **Step 4: Declare both calls**

In `libraries/Core/include/VLMS/Core/CatalogRepository.h`, after `restoreBook`:

```cpp
    /// Destroys an archived title that has no copies at all. Takes its cover
    /// folder with it.
    [[nodiscard]] VLMS::Status purgeBook(std::int64_t id);
    /// True when any live copy of the book is out on an unreturned loan. The
    /// Catalogue asks before it confirms a delete; archiveBook asks again.
    [[nodiscard]] VLMS::Result<bool> bookHasOpenLoans(std::int64_t id) const;
```

- [ ] **Step 5: Implement `bookHasOpenLoans` and reuse it in `archiveBook`**

In `libraries/Core/src/CatalogRepository.cpp`, above `archiveBook`:

```cpp
Result<bool> CatalogRepository::bookHasOpenLoans(const std::int64_t id) const
{
    auto loanCheck = m_session.prepare(
        "SELECT COUNT(*) FROM loans l "
        "INNER JOIN book_copies bc ON bc.id = l.book_copy_id "
        "WHERE bc.book_id = :book_id AND bc.archived_at IS NULL AND l.returned_at IS NULL");
    if (!loanCheck) {
        return RepoSql::sqlResult<bool>(loanCheck.error().detail);
    }
    if (!loanCheck->bind(":book_id", id) || !loanCheck->next()) {
        return RepoSql::sqlResult<bool>(m_session.lastError());
    }
    return Result<bool>::ok(loanCheck->integer(0) > 0);
}
```

Then replace the whole inline loan check at the top of `archiveBook`'s transaction lambda
(`libraries/Core/src/CatalogRepository.cpp:478-490`, from `auto loanCheck = m_session.prepare(` down
to and including the `return RepoSql::validation("error.book.hasActiveLoans");` block) with:

```cpp
        const auto open = bookHasOpenLoans(id);
        if (!open) {
            return asStatus(open);
        }
        if (open.value()) {
            return RepoSql::validation("error.book.hasActiveLoans");
        }
```

- [ ] **Step 6: Implement `purgeBook`**

In `libraries/Core/src/CatalogRepository.cpp`, after `restoreBook`'s closing brace:

```cpp
Status CatalogRepository::purgeBook(const std::int64_t id)
{
    const auto removed = m_session.transaction([&]() -> Status {
        {
            auto read =
                m_session.prepare("SELECT archived_at IS NOT NULL FROM books WHERE id = :id");
            if (!read) {
                return RepoSql::sqlFailure(read.error().detail);
            }
            if (!read->bind(":id", id)) {
                return RepoSql::sqlFailure(m_session.lastError());
            }
            if (!read->next()) {
                if (!read->ok()) {
                    return RepoSql::sqlFailure(m_session.lastError());
                }
                return RepoSql::notFound("error.book.notFound");
            }
            if (read->integer(0) == 0) {
                return RepoSql::validation("error.book.notArchived");
            }
        }

        {
            // Every copy, whatever its archive flag. The Archive's Copies
            // column is scoped to archived rows; this check must not depend on
            // which list asked the question.
            auto copies = m_session.prepare("SELECT COUNT(*) FROM book_copies WHERE book_id = :id");
            if (!copies) {
                return RepoSql::sqlFailure(copies.error().detail);
            }
            if (!copies->bind(":id", id) || !copies->next()) {
                return RepoSql::sqlFailure(m_session.lastError());
            }
            if (copies->integer(0) > 0) {
                return RepoSql::validation("error.book.hasCopies");
            }
        }

        auto remove = m_session.prepare("DELETE FROM books WHERE id = :id");
        if (!remove) {
            return RepoSql::sqlFailure(remove.error().detail);
        }
        if (!remove->bind(":id", id) || !remove->exec()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (remove->changes() <= 0) {
            return RepoSql::notFound("error.book.notFound");
        }
        return Status::ok();
    });
    if (!removed) {
        return removed;
    }

    // Outside the transaction: a filesystem removal cannot be rolled back, so
    // it waits until the row is certainly gone.
    const std::filesystem::path bookDir =
        std::filesystem::path(m_resourcesDirectory) / "books" / std::to_string(id);
    if (std::filesystem::exists(bookDir)) {
        std::error_code error;
        std::filesystem::remove_all(bookDir, error);
    }
    return Status::ok();
}
```

If `libraries/Core/src/CatalogRepository.cpp` does not already include `<filesystem>`, add it.

- [ ] **Step 7: Build and run**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_core_Archive|^test_core_Catalog|^test_core_StringsParity' --output-on-failure
```
Expected: all passing, including the pre-existing `test_core_ArchiveCatalog` tests that cover `archiveBook`'s refusal — they guard the `bookHasOpenLoans` extraction.

- [ ] **Step 8: Commit**

```bash
git add libraries/Core/include/VLMS/Core/CatalogRepository.h \
        libraries/Core/src/CatalogRepository.cpp libraries/Core/src/Strings.cpp \
        libraries/Core/test/src/test_archive_purge.cpp
git commit -m "Destroy an archived title once its copies are gone.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: `purgeMember` refuses a member who is not archived

**Files:**
- Modify: `libraries/Core/src/MemberRepository.cpp:681` (`purgeMember`)
- Modify: `libraries/Core/src/Strings.cpp` (three tables)
- Modify: `libraries/Core/test/src/test_archive_purge.cpp`

**Interfaces:**
- Consumes: `CirculationRepository::purgeLoan` (Task 1), the Task 1 fixture.
- Produces: `purgeMember` keeps its signature. New refusal: `Validation("error.member.notArchived")`.

- [ ] **Step 1: Add the string entries**

In each of the three tables, after that table's `{"error.member.hasHistory", ...}` entry:

Arabic:
```cpp
        {"error.member.notArchived", "هذا العضو ليس في الأرشيف."},
```
French:
```cpp
        {"error.member.notArchived", "Cet adhérent n'est pas archivé."},
```
English:
```cpp
        {"error.member.notArchived", "This member is not in the Archive."},
```

- [ ] **Step 2: Write the failing tests**

Append to `libraries/Core/test/src/test_archive_purge.cpp`:

```cpp
TEST_F(test_core_ArchivePurge, PurgeMemberIsRefusedWhileTheMemberIsStillLive)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(30));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(31));

    const auto refused = m_members->purgeMember(memberId);
    ASSERT_FALSE(refused);
    EXPECT_EQ(refused.error().key, "error.member.notArchived");
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM members").toInt(), 2);
    EXPECT_GT(bystander, 0);
}

TEST_F(test_core_ArchivePurge, PurgeMemberRemovesAnArchivedMemberWhoNeverBorrowed)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(32));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(33));
    ASSERT_TRUE(m_members->archiveMember(memberId));

    ASSERT_TRUE(m_members->purgeMember(memberId));

    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM members WHERE id = " + std::to_string(memberId))
                  .toInt(),
              0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM members WHERE id = " + std::to_string(bystander))
                  .toInt(),
              1);
}

TEST_F(test_core_ArchivePurge, PurgeMemberTakesTheirPhotoFolderWithIt)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(36));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(37));
    const std::filesystem::path folder =
        std::filesystem::path(m_db->resourcesDirectory()) / "members" / std::to_string(memberId);
    const std::filesystem::path keptFolder =
        std::filesystem::path(m_db->resourcesDirectory()) / "members" / std::to_string(bystander);
    std::filesystem::create_directories(folder);
    std::filesystem::create_directories(keptFolder);
    std::ofstream(folder / "photo.jpg") << "jpeg";
    std::ofstream(keptFolder / "photo.jpg") << "jpeg";

    ASSERT_TRUE(m_members->archiveMember(memberId));
    ASSERT_TRUE(m_members->purgeMember(memberId));

    EXPECT_FALSE(std::filesystem::exists(folder));
    EXPECT_TRUE(std::filesystem::exists(keptFolder / "photo.jpg"));
}

TEST_F(test_core_ArchivePurge, TheFunnelIsWalkableFromALoanUpToTheMemberWhoBorrowedIt)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(34));
    const std::int64_t bystander = seedMember(*m_db, uniqueMemberSeed(35));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(34));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15",
                      "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_members->archiveMember(memberId));

    // Blocked at the top of the funnel while the loan is still there ...
    const auto blocked = m_members->purgeMember(memberId);
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().key, "error.member.hasHistory");

    // ... and archiving the loan is not enough; it has to go.
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));
    const auto stillBlocked = m_members->purgeMember(memberId);
    ASSERT_FALSE(stillBlocked);
    EXPECT_EQ(stillBlocked.error().key, "error.member.hasHistory");

    ASSERT_TRUE(m_circulation->purgeLoan(loanId));
    EXPECT_TRUE(m_members->purgeMember(memberId));
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM members WHERE id = " + std::to_string(bystander))
                  .toInt(),
              1);
}
```

- [ ] **Step 3: Run to verify the first test fails**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_core_ArchivePurge.PurgeMemberIsRefusedWhileTheMemberIsStillLive' --output-on-failure
```
Expected: FAIL — `purgeMember` currently succeeds on a live member, so `refused` is truthy.

- [ ] **Step 4: Add the archived check**

In `libraries/Core/src/MemberRepository.cpp`, at the top of `purgeMember`, **before** the existing
`const auto block = removalBlock(id);` line:

```cpp
    {
        auto read = m_session.prepare("SELECT archived_at IS NOT NULL FROM members WHERE id = :id");
        if (!read) {
            return RepoSql::sqlFailure(read.error().detail);
        }
        if (!read->bind(":id", id)) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (!read->next()) {
            if (!read->ok()) {
                return RepoSql::sqlFailure(m_session.lastError());
            }
            return RepoSql::notFound("error.member.notFound");
        }
        // Delete archives; only the Archive destroys. Asked first, so a live
        // member is refused for being live rather than for their loans.
        if (read->integer(0) == 0) {
            return RepoSql::validation("error.member.notArchived");
        }
    }
```

- [ ] **Step 5: Build and run**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_core_Archive|^test_core_Member|^test_core_StringsParity' --output-on-failure
```
Expected: all passing. If a pre-existing `test_core_MemberRepository` test purges a live member, archive it first in that test — the rule changed deliberately, so update the test rather than weaken the check.

- [ ] **Step 6: Commit**

```bash
git add libraries/Core/src/MemberRepository.cpp libraries/Core/src/Strings.cpp \
        libraries/Core/test/src/test_archive_purge.cpp
git commit -m "Refuse to destroy a member who is not archived.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: `loanCount` on members and copies

The Archive's button is disabled when the row's gate is shut, and a disabled button only works if
the row says why. This task supplies the number the two new columns show, and the sort keys they
need.

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/MemberTypes.h` (`MemberRecord`, `MemberSort`)
- Modify: `libraries/Core/src/MemberRepository.cpp:29-84` (`kMemberSelect`, `readMemberRow`)
- Modify: `libraries/Core/src/MemberSql.cpp:98-139` (`orderExpressions`)
- Modify: `libraries/Core/include/VLMS/Core/CatalogTypes.h` (`BookCopyRecord`, `CopySort`)
- Modify: `libraries/Core/src/BookCopyStore.cpp:133-151, 215-256` (`copyOrderClause`, `listCopyRows`)
- Modify: `libraries/Core/test/src/test_archive_purge.cpp`

**Interfaces:**
- Consumes: `CatalogRepository::purgeCopy` (Task 2), `CirculationRepository::purgeLoan` (Task 1).
- Produces:
  - `int MemberRecord::loanCount` — every loan row naming the member, archived included.
  - `int BookCopyRecord::loanCount` — every loan row naming the copy, archived included.
  - `MemberSort::kAllLoans` (`"allLoans"`), `CopySort::kLoans` (`"loans"`) — sort keys for Task 7's columns.

- [ ] **Step 1: Write the failing tests**

Append to `libraries/Core/test/src/test_archive_purge.cpp`:

```cpp
TEST_F(test_core_ArchivePurge, MemberLoanCountCountsArchivedLoansSoTheColumnMatchesTheGate)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(40));
    const std::int64_t quiet = seedMember(*m_db, uniqueMemberSeed(41));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(40));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15",
                      "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));

    const auto borrower = m_members->getMember(memberId);
    ASSERT_TRUE(borrower);
    EXPECT_EQ(borrower->loanCount, 1);
    EXPECT_EQ(borrower->activeLoanCount, 0);

    const auto never = m_members->getMember(quiet);
    ASSERT_TRUE(never);
    EXPECT_EQ(never->loanCount, 0);

    // The column and the gate must agree, or a 0 sits beside a button that refuses.
    ASSERT_TRUE(m_circulation->purgeLoan(loanId));
    EXPECT_EQ(m_members->getMember(memberId)->loanCount, 0);
}

TEST_F(test_core_ArchivePurge, CopyLoanCountCountsArchivedLoansSoTheColumnMatchesTheGate)
{
    BookSeed seed = uniqueBookSeed(42);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(42));
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copies.at(0), "2026-09-01", "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    CopyQuery query;
    query.archive = ArchiveScope::Archived;
    const auto rows = VLMS_UNWRAP(m_catalog->listCopyRows(query));
    ASSERT_EQ(rows.size(), 2u);

    int borrowed = 0;
    int untouched = 0;
    for (const BookCopyRecord& copy : rows) {
        if (copy.id == copies.at(0)) {
            borrowed = copy.loanCount;
        } else if (copy.id == copies.at(1)) {
            untouched = copy.loanCount;
        }
    }
    EXPECT_EQ(borrowed, 1);
    EXPECT_EQ(untouched, 0);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j 2>&1 | tail -20
```
Expected: `'struct MemberRecord' has no member named 'loanCount'`.

- [ ] **Step 3: Add the member field, sort key and column**

In `libraries/Core/include/VLMS/Core/MemberTypes.h`, in `MemberRecord`, immediately after
`activeLoanCount`:

```cpp
    /// Every loan row naming this member, archived ones included. The Archive's
    /// gate on permanent removal, so the column that shows it must count the
    /// same rows -- an archived loan still blocks.
    int loanCount = 0;
```

In the same header, in `namespace MemberSort`, after `kLoans`:

```cpp
inline constexpr auto kAllLoans = "allLoans";
```

In `libraries/Core/src/MemberRepository.cpp`, in `kMemberSelect`, insert between the
`active_loan_count` subquery and the `archived_at` line:

```sql
            (
                SELECT COUNT(*)
                FROM loans l
                WHERE l.member_id = m.id
            ) AS loan_count,
```

In `readMemberRow`, replace the final two assignments:

```cpp
    member.loanCount = query.integer(20);
    member.archivedAt = query.text(21);
```

In `libraries/Core/src/MemberSql.cpp`, in `orderExpressions`, after the `MemberSort::kLoans` branch:

```cpp
    if (column == MemberSort::kAllLoans) {
        return withDirection("(SELECT COUNT(*) FROM loans l WHERE l.member_id = m.id)", asc) + ", "
            + withDirection("m.id", asc);
    }
```

- [ ] **Step 4: Add the copy field, sort key and column**

In `libraries/Core/include/VLMS/Core/CatalogTypes.h`, in `BookCopyRecord`, after `archivedAt`:

```cpp
    /// Every loan row naming this copy, archived ones included. Filled by the
    /// Archive copy list only; the gate on permanent removal.
    int loanCount = 0;
```

In the same header, in `namespace CopySort`, after `kArchivedAt`:

```cpp
inline constexpr auto kLoans = "loans";
```

In `libraries/Core/src/BookCopyStore.cpp`, in `listCopyRows`, extend the SELECT list — appended
last so no existing column index moves:

```cpp
    std::string sql = R"SQL(
        SELECT bc.id, bc.book_id, COALESCE(bc.global_copy_id, ''), bc.source,
               COALESCE(bc.local_id, ''), COALESCE(bc.central_id, ''),
               COALESCE(bc.notes, ''), b.title, COALESCE(bc.archived_at, ''),
               (SELECT COUNT(*) FROM loans l WHERE l.book_copy_id = bc.id)
        FROM book_copies bc
        INNER JOIN books b ON b.id = bc.book_id
        WHERE 1 = 1
    )SQL";
```

and in the row loop, after `copy.archivedAt = q->text(8);`:

```cpp
        copy.loanCount = q->integer(9);
```

In `copyOrderClause`, after the `CopySort::kArchivedAt` branch:

```cpp
    if (column == CopySort::kLoans) {
        return " ORDER BY (SELECT COUNT(*) FROM loans l WHERE l.book_copy_id = bc.id)" + dir
            + ", bc.id" + dir;
    }
```

- [ ] **Step 5: Build and run**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_core_' --output-on-failure
```
Expected: the whole Core suite passes. `readMemberRow`'s index shift touches every member query, so
run all of Core rather than one suite.

- [ ] **Step 6: Commit**

```bash
git add libraries/Core/include/VLMS/Core/MemberTypes.h \
        libraries/Core/include/VLMS/Core/CatalogTypes.h \
        libraries/Core/src/MemberRepository.cpp libraries/Core/src/MemberSql.cpp \
        libraries/Core/src/BookCopyStore.cpp libraries/Core/test/src/test_archive_purge.cpp
git commit -m "Count every loan a member or copy has, archived ones too.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: `listFreeLocalNumbers`

Depends only on Task 2's `purgeCopy`, which one test uses to prove a removed number comes
back. Independent of Tasks 1, 3, 4 and 5.

**Files:**
- Modify: `libraries/Core/src/BookCopyStore.h` / `.cpp`
- Modify: `libraries/Core/include/VLMS/Core/CatalogRepository.h` / `src/CatalogRepository.cpp`
- Create: `libraries/Core/test/src/test_free_local_numbers.cpp`
- Modify: `libraries/Core/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `CatalogRepository::purgeCopy` (Task 2).
- Produces: `VLMS::Result<std::vector<std::string>> CatalogRepository::listFreeLocalNumbers(const std::string& source, int limit) const;` — the numbers in `1 .. MAX(local_id)` for that stock that no `book_copies` row holds, ascending, as decimal strings, at most `limit` of them. Task 10 calls it.

- [ ] **Step 1: Write the failing tests**

Create `libraries/Core/test/src/test_free_local_numbers.cpp`:

```cpp
#include "TestDatabase.h"
#include "TestSeed.h"

#include <VLMS/Core/CatalogRepository.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace VLMS::Test;

/// A number is free when no book_copies row holds it -- live or archived.
/// Computed, never stored: a permanently removed number rejoins the list by
/// itself, and the gaps the 2022 import left are in it from the first run.
class test_core_FreeLocalNumbers : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_catalog.reset();
        m_db.reset();
    }

    /// Seeds one arabic book whose copies carry exactly `numbers`.
    void seedArabicNumbers(int index, const std::vector<int>& numbers)
    {
        BookSeed seed = uniqueBookSeed(index);
        seed.language = "ar";
        seed.initialCopyCount = static_cast<int>(numbers.size());
        const std::int64_t bookId = seedBook(*m_db, seed);
        const auto copies = copyIdsOf(*m_db, bookId);
        ASSERT_EQ(copies.size(), numbers.size());
        for (std::size_t i = 0; i < numbers.size(); ++i) {
            ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(i), std::to_string(numbers.at(i))));
        }
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_catalog;
};

TEST_F(test_core_FreeLocalNumbers, TheGapsBetweenTheNumbersInUseAreFree)
{
    seedArabicNumbers(1, {1, 2, 5, 9});

    const auto free = VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100));
    EXPECT_EQ(free, (std::vector<std::string>{"3", "4", "6", "7", "8"}));
}

TEST_F(test_core_FreeLocalNumbers, AContiguousStockHasNoFreeNumbers)
{
    seedArabicNumbers(2, {1, 2, 3});

    EXPECT_TRUE(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100)).empty());
}

TEST_F(test_core_FreeLocalNumbers, AnArchivedCopyStillHoldsItsNumberButAPurgedOneGivesItBack)
{
    BookSeed seed = uniqueBookSeed(3);
    seed.language = "ar";
    seed.initialCopyCount = 3;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(0), "1"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(1), "2"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(2), "3"));
    ASSERT_TRUE(m_catalog->archiveBook(bookId));

    EXPECT_TRUE(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100)).empty());

    ASSERT_TRUE(m_catalog->purgeCopy(copies.at(1)));
    EXPECT_EQ(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100)),
              (std::vector<std::string>{"2"}));
}

TEST_F(test_core_FreeLocalNumbers, EachStockIsCountedOnItsOwn)
{
    seedArabicNumbers(4, {1, 4});

    BookSeed foreign = uniqueBookSeed(5);
    foreign.language = "fr";
    foreign.initialCopyCount = 2;
    const std::int64_t foreignBook = seedBook(*m_db, foreign);
    const auto foreignCopies = copyIdsOf(*m_db, foreignBook);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, foreignCopies.at(0), "1"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, foreignCopies.at(1), "2"));

    EXPECT_EQ(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100)),
              (std::vector<std::string>{"2", "3"}));
    EXPECT_TRUE(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("foreign", 100)).empty());
}

TEST_F(test_core_FreeLocalNumbers, TheCapIsHonouredBecauseTheDropDownIsAChoiceNotAHaystack)
{
    seedArabicNumbers(6, {1, 20});

    const auto free = VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 3));
    EXPECT_EQ(free, (std::vector<std::string>{"2", "3", "4"}));
}
```

- [ ] **Step 2: Register the test file**

In `libraries/Core/test/CMakeLists.txt`, add to `TST_SOURCES` after `src/test_archive_purge.cpp`:

```cmake
    src/test_free_local_numbers.cpp
```

- [ ] **Step 3: Run to verify it fails**

```bash
cmake --build build -j 2>&1 | tail -20
```
Expected: `'listFreeLocalNumbers' is not a member of 'CatalogRepository'`.

- [ ] **Step 4: Implement in `BookCopyStore`**

Declare in `libraries/Core/src/BookCopyStore.h`, after `suggestCopyIdentifiers`:

```cpp
    /// The numbers in 1 .. MAX(local_id) for `source` that no copy holds,
    /// ascending, at most `limit` of them.
    [[nodiscard]] VLMS::Result<std::vector<std::string>> freeLocalNumbers(
        const std::string& source,
        int limit) const;
```

Implement in `libraries/Core/src/BookCopyStore.cpp`, after `suggestCopyIdentifiers`:

```cpp
Result<std::vector<std::string>> BookCopyStore::freeLocalNumbers(const std::string& source,
                                                                 const int limit) const
{
    std::vector<std::string> free;
    if (limit <= 0) {
        return Result<std::vector<std::string>>::ok(std::move(free));
    }

    // One sorted read, walked in C++. A recursive CTE with a NOT EXISTS would
    // scan the copies table once per candidate -- CAST(local_id AS INTEGER) is
    // not indexed -- which is ~19,773 scans for the live arabic stock.
    auto query = m_session.prepare(
        "SELECT CAST(local_id AS INTEGER) FROM book_copies "
        "WHERE source = :source AND local_id IS NOT NULL AND trim(local_id) <> '' "
        "ORDER BY 1");
    if (!query) {
        return RepoSql::sqlResult<std::vector<std::string>>(query.error().detail);
    }
    if (!query->bind(":source", normalizedCopySource(source))) {
        return RepoSql::sqlResult<std::vector<std::string>>(m_session.lastError());
    }

    std::int64_t expected = 1;
    while (query->next()) {
        const std::int64_t held = query->int64(0);
        // Duplicates cannot occur -- UNIQUE (source, local_id) -- but a number
        // below `expected` would loop forever if one ever did.
        if (held < expected) {
            continue;
        }
        for (; expected < held && static_cast<int>(free.size()) < limit; ++expected) {
            free.push_back(std::to_string(expected));
        }
        if (static_cast<int>(free.size()) >= limit) {
            break;
        }
        expected = held + 1;
    }
    if (!query->ok()) {
        return RepoSql::sqlResult<std::vector<std::string>>(m_session.lastError());
    }
    return Result<std::vector<std::string>>::ok(std::move(free));
}
```

- [ ] **Step 5: Add the facade**

Declare in `libraries/Core/include/VLMS/Core/CatalogRepository.h`, after `suggestCopyIdentifiers`:

```cpp
    [[nodiscard]] VLMS::Result<std::vector<std::string>> listFreeLocalNumbers(
        const std::string& source,
        int limit) const;
```

Implement in `libraries/Core/src/CatalogRepository.cpp`, after `suggestCopyIdentifiers`:

```cpp
Result<std::vector<std::string>> CatalogRepository::listFreeLocalNumbers(const std::string& source,
                                                                         const int limit) const
{
    return m_copies->freeLocalNumbers(source, limit);
}
```

- [ ] **Step 6: Build and run**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_core_FreeLocalNumbers' --output-on-failure
```
Expected: 5 tests passing.

- [ ] **Step 7: Commit**

```bash
git add libraries/Core/src/BookCopyStore.h libraries/Core/src/BookCopyStore.cpp \
        libraries/Core/include/VLMS/Core/CatalogRepository.h \
        libraries/Core/src/CatalogRepository.cpp \
        libraries/Core/test/src/test_free_local_numbers.cpp libraries/Core/test/CMakeLists.txt
git commit -m "Find the local numbers nobody holds.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: The Archive's `Permanently remove` button

**Files:**
- Modify: `applications/vlms/src/ui/archive/ArchivePage.h`
- Modify: `applications/vlms/src/ui/archive/ArchivePage.cpp`
- Modify: `libraries/Core/src/Strings.cpp` (three tables)
- Modify: `applications/vlms/test/src/test_archive_page.cpp`

**Interfaces:**
- Consumes: `purgeLoan` (Task 1), `purgeCopy` (Task 2), `purgeBook` (Task 3), `purgeMember` (Task 4), `MemberRecord::loanCount` / `BookCopyRecord::loanCount` / `MemberSort::kAllLoans` / `CopySort::kLoans` (Task 5).
- Produces: nothing later tasks depend on.

- [ ] **Step 1: Add the string entries**

In each of the three tables in `libraries/Core/src/Strings.cpp`, add `archive.col.loans`
immediately after that table's `{"archive.col.notes", ...}` line, and the five purge keys
immediately after that table's `{"archive.restoreConfirm", ...}` line.

Arabic:
```cpp
        {"archive.col.loans", "الإعارات"},
```
```cpp
        {"archive.purge", "حذف نهائي"},
        {"archive.purgeConfirmMember",
         "حذف هذا العضو نهائيًا؟ لا يمكن التراجع عن هذه العملية."},
        {"archive.purgeConfirmBook",
         "حذف هذا الكتاب نهائيًا؟ لا يمكن التراجع عن هذه العملية."},
        {"archive.purgeConfirmCopy",
         "حذف هذه النسخة نهائيًا؟ سيعود رقمها المحلي متاحًا. لا يمكن التراجع عن هذه العملية."},
        {"archive.purgeConfirmLoan",
         "حذف هذه الإعارة نهائيًا؟ لا يمكن التراجع عن هذه العملية."},
```

French:
```cpp
        {"archive.col.loans", "Prêts"},
```
```cpp
        {"archive.purge", "Suppression définitive"},
        {"archive.purgeConfirmMember",
         "Supprimer définitivement cet adhérent ? Cette opération est irréversible."},
        {"archive.purgeConfirmBook",
         "Supprimer définitivement cet ouvrage ? Cette opération est irréversible."},
        {"archive.purgeConfirmCopy",
         "Supprimer définitivement cet exemplaire ? Son numéro local redevient disponible. "
         "Cette opération est irréversible."},
        {"archive.purgeConfirmLoan",
         "Supprimer définitivement ce prêt ? Cette opération est irréversible."},
```

English:
```cpp
        {"archive.col.loans", "Loans"},
```
```cpp
        {"archive.purge", "Permanently remove"},
        {"archive.purgeConfirmMember",
         "Permanently remove this member? This cannot be undone."},
        {"archive.purgeConfirmBook",
         "Permanently remove this title? This cannot be undone."},
        {"archive.purgeConfirmCopy",
         "Permanently remove this copy? Its local number becomes free again. "
         "This cannot be undone."},
        {"archive.purgeConfirmLoan",
         "Permanently remove this loan? This cannot be undone."},
```

- [ ] **Step 2: Write the failing tests**

First, **update the two existing column assertions** in
`applications/vlms/test/src/test_archive_page.cpp` — Members gains a column, Books does not:

at line 167-168, replace with:
```cpp
    EXPECT_EQ(m_table->columnCount(), 6);
    EXPECT_EQ(m_table->horizontalHeaderItem(4)->text(), QStringLiteral("Loans"));
    EXPECT_EQ(m_table->horizontalHeaderItem(5)->text(), QStringLiteral("Archived"));
```

Then append these tests to the same file:

```cpp
TEST_F(test_ui_ArchivePage, PermanentRemovalIsOfferedOnlyWhenTheRowsGateIsOpen)
{
    // Members: the seeded archived member never borrowed, so nothing holds them.
    m_table->selectRow(rowOf(m_memberId));
    EXPECT_TRUE(m_purge->isEnabled());

    // Books: the title still has its two archived copies.
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("books")));
    m_table->selectRow(rowOf(m_bookId));
    EXPECT_FALSE(m_purge->isEnabled());

    // Copies: neither seeded copy was ever borrowed.
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    m_table->selectRow(rowOf(m_numberedCopyId));
    EXPECT_TRUE(m_purge->isEnabled());

    // Loans: nothing sits beneath a loan.
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("loans")));
    m_table->selectRow(rowOf(m_loanId));
    EXPECT_TRUE(m_purge->isEnabled());
}

TEST_F(test_ui_ArchivePage, TheLoansColumnSaysWhyAMemberCannotGo)
{
    const std::int64_t borrower = seedMember(*m_db, uniqueMemberSeed(9));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(9));
    const std::int64_t loanId = rawInsertLoan(*m_db, borrower, copyIdsOf(*m_db, bookId).front(),
                                              "2026-09-01", "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));
    ASSERT_TRUE(m_members->archiveMember(borrower));
    m_page->refresh();

    const int row = rowOf(borrower);
    ASSERT_GE(row, 0);
    // The column counts the archived loan, because the archived loan blocks.
    EXPECT_EQ(m_table->item(row, 4)->text(), QStringLiteral("1"));
    m_table->selectRow(row);
    EXPECT_FALSE(m_purge->isEnabled());

    // And the one who never borrowed still reads 0 and still goes.
    m_table->selectRow(rowOf(m_memberId));
    EXPECT_EQ(m_table->item(rowOf(m_memberId), 4)->text(), QStringLiteral("0"));
    EXPECT_TRUE(m_purge->isEnabled());
}

TEST_F(test_ui_ArchivePage, PermanentlyRemovingACopyTakesItOffTheListAndLeavesTheOther)
{
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    ASSERT_EQ(m_table->rowCount(), 2);
    m_table->selectRow(rowOf(m_numberedCopyId));

    answerNextBoxYes();
    m_purge->click();
    QApplication::processEvents();

    EXPECT_EQ(m_table->rowCount(), 1);
    EXPECT_EQ(rowOf(m_numberedCopyId), -1);
    EXPECT_GE(rowOf(m_numberlessCopyId), 0);
}
```

Add `m_purge` to the fixture. After the `m_reuse = buttonWithText(...)` line in `SetUp`:

```cpp
        m_purge = buttonWithText(m_page.get(), QStringLiteral("Permanently remove"));
        ASSERT_NE(m_purge, nullptr);
```

and beside `QPushButton* m_reuse = nullptr;` in the member list:

```cpp
    QPushButton* m_purge = nullptr;
```

- [ ] **Step 3: Run to verify it fails**

```bash
cmake --build build -j 2>&1 | tail -20
ctest --test-dir build -R '^test_ui_ArchivePage' --output-on-failure 2>&1 | tail -20
```
Expected: `SetUp` fails on `ASSERT_NE(m_purge, nullptr)` — there is no such button yet.

- [ ] **Step 4: Add the two columns**

In `applications/vlms/src/ui/archive/ArchivePage.cpp`, in `columnsFor`, insert a Loans entry
before `archivedAt` for Members and for Copies:

```cpp
    case ArchivePage::Type::Members:
        return {{"archive.col.number", MemberSort::kNumber},
                {"archive.col.name", MemberSort::kName},
                {"archive.col.city", MemberSort::kCity},
                {"archive.col.status", MemberSort::kStatus},
                {"archive.col.loans", MemberSort::kAllLoans},
                {"archive.col.archivedAt", MemberSort::kArchivedAt}};
```
```cpp
    case ArchivePage::Type::Copies:
        return {{"archive.col.localId", CopySort::kLocalId},
                {"archive.col.source", CopySort::kSource},
                {"archive.col.title", CopySort::kTitle},
                {"archive.col.loans", CopySort::kLoans},
                {"archive.col.archivedAt", CopySort::kArchivedAt}};
```

In `fillRows`, in the Members branch, replace the last `setItem` with:

```cpp
            m_table->setItem(row, 4, new QTableWidgetItem(QString::number(member.loanCount)));
            m_table->setItem(row, 5, new QTableWidgetItem(qs(member.archivedAt)));
```

and in the Copies branch:

```cpp
            m_table->setItem(row, 3, new QTableWidgetItem(QString::number(copy.loanCount)));
            m_table->setItem(row, 4, new QTableWidgetItem(qs(copy.archivedAt)));
```

- [ ] **Step 5: Add the button, the gate and the slot**

In `applications/vlms/src/ui/archive/ArchivePage.h`, add to the private slots after
`reuseSelected`:

```cpp
    void purgeSelected();
```

to the private helpers after `selectedCopy`:

```cpp
    /// True when nothing holds the selected row any more: no loan names a
    /// member or a copy, no copy belongs to a title. Read from the row already
    /// on screen, so the Loans (or Copies) column always agrees with the button.
    [[nodiscard]] bool selectedMayBePurged() const;
```

and beside `m_reuseButton`:

```cpp
    QPushButton* m_purgeButton = nullptr;
```

In `ArchivePage.cpp`'s `buildUi`, after the `frame->addButton(m_reuseButton);` line:

```cpp
    m_purgeButton = VLMS::makeSecondaryButton({});
    connect(m_purgeButton, &QPushButton::clicked, this, &ArchivePage::purgeSelected);
    frame->addButton(m_purgeButton);
```

In `retranslateUi`, after `m_reuseButton->setText(T("archive.reuse"));`:

```cpp
    m_purgeButton->setText(T("archive.purge"));
```

In `onSelectionChanged`, after the `m_reuseButton->setEnabled(...)` line:

```cpp
    m_purgeButton->setEnabled(selectedMayBePurged());
```

Then add both new functions, after `selectedCopy`:

```cpp
bool ArchivePage::selectedMayBePurged() const
{
    const qint64 id = selectedId();
    if (id <= 0) {
        return false;
    }
    switch (m_type) {
    case Type::Members:
        for (const MemberRecord& member : m_memberRows) {
            if (member.id == id) {
                return member.loanCount == 0;
            }
        }
        return false;
    case Type::Books:
        for (const BookRecord& book : m_bookRows) {
            if (book.id == id) {
                return book.totalCopies == 0;
            }
        }
        return false;
    case Type::Copies: {
        const BookCopyRecord* copy = selectedCopy();
        return copy != nullptr && copy->loanCount == 0;
    }
    case Type::Loans:
        return true;
    }
    return false;
}

void ArchivePage::purgeSelected()
{
    const qint64 id = selectedId();
    if (id <= 0) {
        return;
    }
    const char* confirmKey = "archive.purgeConfirmMember";
    switch (m_type) {
    case Type::Members:
        confirmKey = "archive.purgeConfirmMember";
        break;
    case Type::Books:
        confirmKey = "archive.purgeConfirmBook";
        break;
    case Type::Copies:
        confirmKey = "archive.purgeConfirmCopy";
        break;
    case Type::Loans:
        confirmKey = "archive.purgeConfirmLoan";
        break;
    }
    if (!VLMS::askYesNo(this, T("archive.purge"), T(confirmKey))) {
        return;
    }

    const auto removed = [&]() -> VLMS::Status {
        switch (m_type) {
        case Type::Members:
            return m_members.purgeMember(id);
        case Type::Books:
            return m_catalog.purgeBook(id);
        case Type::Copies:
            return m_catalog.purgeCopy(id);
        case Type::Loans:
            return m_circulation.purgeLoan(id);
        }
        return VLMS::Status::ok();
    }();
    if (!removed) {
        // The button is disabled when the gate is shut, so this is a race or a
        // rule the row could not see -- either way it is the repository's word.
        VLMS::showRepoError(this, removed.error());
        return;
    }

    // No recordRestored(): nothing went back to a live page, and the live lists
    // never showed this row.
    refreshRows();
}
```

- [ ] **Step 6: Build and run**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_ui_ArchivePage|^test_ui_ArchiveCatalog|^test_ui_ArchiveCirculation|^test_core_StringsParity' --output-on-failure
```
Expected: all passing.

- [ ] **Step 7: Commit**

```bash
git add applications/vlms/src/ui/archive/ArchivePage.h \
        applications/vlms/src/ui/archive/ArchivePage.cpp \
        libraries/Core/src/Strings.cpp \
        applications/vlms/test/src/test_archive_page.cpp
git commit -m "Remove a record permanently, from the Archive only.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: The Members page loses its checkbox

**Files:**
- Modify: `applications/vlms/src/ui/members/MembersPage.cpp:739-813` (`deleteMember`)
- Modify: `applications/vlms/test/src/test_member_removal.cpp`
- Modify: `libraries/Core/src/Strings.cpp` — **removal** of two keys from all three tables

**Interfaces:**
- Consumes: Task 4's tightened `purgeMember` (which is what makes the checkbox unreachable anyway).
- Produces: nothing later tasks depend on.

- [ ] **Step 1: Rewrite the two checkbox tests**

In `applications/vlms/test/src/test_member_removal.cpp`:

Replace `TEST_F(test_ui_MemberRemoval, ConfirmationCarriesTheArchiveCheckBox)` with:

```cpp
TEST_F(test_ui_MemberRemoval, ConfirmationIsAPlainYesOrNo)
{
    const QDate today = QDate::currentDate();
    seedMemberWithLoan(today.addDays(-20).toString(Qt::ISODate));

    const QString screenshot = qEnvironmentVariable("VLMS_TEST_SHOT_DIR").isEmpty()
        ? QString()
        : QDir(qEnvironmentVariable("VLMS_TEST_SHOT_DIR"))
              .filePath(QStringLiteral("confirm-delete.png"));

    const ModalOutcome outcome = runAndAnswerModal(
        [this]() { clickDelete(m_page.get()); }, qs(Strings::t("common.no")), std::nullopt,
        screenshot);

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Strings::t("members.deleteConfirm")));
    // Delete archives. Destroying a record is the Archive's business now, so
    // there is nothing here to opt out of.
    EXPECT_FALSE(outcome.hasCheckBox);
    EXPECT_EQ(m_db->count("members"), 1);
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId))
                    .isNull());
}
```

Replace `TEST_F(test_ui_MemberRemoval, ClearingTheCheckBoxOnBorrowingHistoryIsRefused)` with:

```cpp
TEST_F(test_ui_MemberRemoval, AMemberWithBorrowingHistoryIsArchivedWithoutASecondQuestion)
{
    const QDate today = QDate::currentDate();
    seedMemberWithLoan(today.addDays(-20).toString(Qt::ISODate));

    // One box, not two: history no longer changes what Delete can do, because
    // Delete no longer offers to destroy anything.
    const QList<ModalOutcome> outcomes = runAndAnswerModals(
        [this]() { clickDelete(m_page.get()); },
        {ModalAnswer{qs(Strings::t("common.yes")), std::nullopt, {}},
         ModalAnswer{qs(Strings::t("common.ok")), std::nullopt, {}}});

    ASSERT_TRUE(outcomes.at(0).appeared);
    EXPECT_FALSE(outcomes.at(1).appeared);
    EXPECT_EQ(m_db->count("members"), 1);
    EXPECT_EQ(m_db->count("loans"), 1);
    EXPECT_FALSE(
        m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId))
            .isNull());
}
```

In `TEST_F(test_ui_MemberRemoval, ArchivingRemovesTheMemberFromTheList)`, change the
`runAndAnswerModal` call's third argument from `true` to `std::nullopt` — there is no checkbox to
tick:

```cpp
    const ModalOutcome outcome = runAndAnswerModal(
        [this]() { clickDelete(m_page.get()); }, qs(Strings::t("common.yes")), std::nullopt);
```

- [ ] **Step 2: Run to verify they fail**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_ui_MemberRemoval' --output-on-failure 2>&1 | tail -30
```
Expected: `ConfirmationIsAPlainYesOrNo` fails on `EXPECT_FALSE(outcome.hasCheckBox)`, and
`AMemberWithBorrowingHistoryIsArchivedWithoutASecondQuestion` fails because a second box appears.

- [ ] **Step 3: Simplify `deleteMember`**

In `applications/vlms/src/ui/members/MembersPage.cpp`, replace everything from the
`// Checked by default: archiving keeps the record ...` comment down to the end of the function
with:

```cpp
    if (!VLMS::askYesNo(this, title, T("members.deleteConfirm"))) {
        return;
    }

    // Delete archives, always. Destroying the record is the Archive's, behind
    // its own button and its own gate -- so a member with borrowing history is
    // no longer a special case here.
    if (const auto archived = m_repository.archiveMember(memberId); !archived) {
        VLMS::showRepoError(this, archived.error());
        return;
    }

    refreshAllFilters();
    refreshMembers();
}
```

The open-loans pre-check above it is unchanged.

- [ ] **Step 4: Remove the two dead strings**

`members.moveToArchive` and `members.deleteBlockedByHistory` now have no caller. Confirm and remove:

```bash
grep -rn "members.moveToArchive\|members.deleteBlockedByHistory" --include="*.cpp" --include="*.h" applications/ libraries/ | grep -v Strings.cpp
```
Expected: no output. Then delete both entries from all three tables in
`libraries/Core/src/Strings.cpp` (six lines in total; some are wrapped over two lines).

- [ ] **Step 5: Build and run**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_ui_Member|^test_core_StringsParity' --output-on-failure
```
Expected: all passing.

- [ ] **Step 6: Commit**

```bash
git add applications/vlms/src/ui/members/MembersPage.cpp libraries/Core/src/Strings.cpp \
        applications/vlms/test/src/test_member_removal.cpp
git commit -m "Delete a member by archiving, with no checkbox.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 9: The Catalogue refuses before it confirms

Today `deleteBook` confirms first and only then discovers `error.book.hasActiveLoans` coming back
from `archiveBook`. Being asked "delete this book?" only to be told afterwards that it was never
possible is the long way round to the same no — the comment in `MembersPage::deleteMember` already
says so, and the three pages should agree.

This task also lifts the modal helpers out of `test_member_removal.cpp`'s anonymous namespace, so
the new catalogue test does not copy ninety lines of `QTimer` polling.

**Files:**
- Create: `applications/vlms/test/src/ModalTest.h`
- Modify: `applications/vlms/test/src/test_member_removal.cpp:35-140` (use the header)
- Create: `applications/vlms/test/src/test_catalog_removal.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt`
- Modify: `applications/vlms/src/ui/catalog/CatalogPage.cpp:770-795` (`deleteBook`)

**Interfaces:**
- Consumes: `CatalogRepository::bookHasOpenLoans` (Task 3).
- Produces: `ModalTest.h` — `struct ModalOutcome`, `struct ModalAnswer`, `runAndAnswerModals`, `runAndAnswerModal`, `clickButtonWithText(QWidget*, const QString&)`. Every function `inline`.

- [ ] **Step 1: Extract the modal helpers**

Create `applications/vlms/test/src/ModalTest.h` holding, verbatim, the `ModalOutcome`,
`ModalAnswer`, `runAndAnswerModals` and `runAndAnswerModal` definitions currently in the anonymous
namespace of `applications/vlms/test/src/test_member_removal.cpp` (lines 37–125). Wrap them in
`#pragma once`, mark both functions `inline`, and add the includes they need:

```cpp
#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
```

Then add the page-agnostic click helper that replaces `clickDelete`:

```cpp
/// Clicks the first button on `root` whose label is exactly `text`. Fails the
/// test if there is none -- a renamed button should not read as a silent pass.
inline void clickButtonWithText(QWidget* root, const QString& text)
{
    for (QPushButton* button : root->findChildren<QPushButton*>()) {
        if (button->text() == text) {
            button->click();
            return;
        }
    }
    FAIL() << "no button labelled \"" << text.toStdString() << "\"";
}
```

In `test_member_removal.cpp`: delete lines 37–125 (the four moved definitions), add
`#include "ModalTest.h"` beside the existing `#include "UiTest.h"`, and replace the body of
`clickDelete` with a call through:

```cpp
void clickDelete(MembersPage* page)
{
    clickButtonWithText(page, qs(Strings::t("members.delete")));
}
```

- [ ] **Step 2: Verify the extraction changed nothing**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_ui_MemberRemoval' --output-on-failure
```
Expected: the same tests pass as before the move. Commit this separately so the refactor is legible:

```bash
git add applications/vlms/test/src/ModalTest.h \
        applications/vlms/test/src/test_member_removal.cpp
git commit -m "Share the modal test helpers.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

- [ ] **Step 3: Write the failing test**

Create `applications/vlms/test/src/test_catalog_removal.cpp`:

```cpp
#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"

#include "ui/catalog/CatalogPage.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>

#include <QTableWidget>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

using VLMS::Locale;
using VLMS::Strings;
using namespace VLMS::Test;

class test_ui_CatalogRemoval : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }
    static void TearDownTestSuite() { Locale::setCode(Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_page.reset();
        m_circulation.reset();
        m_catalog.reset();
        m_db.reset();
    }

    void openPageAndSelect(std::int64_t bookId)
    {
        m_page = std::make_unique<CatalogPage>(*m_catalog, *m_circulation);
        m_table = m_page->findChild<QTableWidget*>();
        ASSERT_NE(m_table, nullptr);
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (m_table->item(row, 0)->data(Qt::UserRole).toLongLong() == bookId) {
                m_table->selectRow(row);
                return;
            }
        }
        FAIL() << "book " << bookId << " is not on the catalogue page";
    }

    void clickDelete() { clickButtonWithText(m_page.get(), qs(Strings::t("catalog.delete"))); }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::unique_ptr<CatalogPage> m_page;
    QTableWidget* m_table = nullptr;
};

TEST_F(test_ui_CatalogRemoval, ABookWithACopyOutIsRefusedBeforeTheConfirmationIsAsked)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(1));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(2));
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(1));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                            "2026-09-15"),
              0);
    openPageAndSelect(bookId);

    // One box, and it is the refusal -- not a confirmation followed by a no.
    const QList<ModalOutcome> outcomes =
        runAndAnswerModals([this]() { clickDelete(); },
                           {ModalAnswer{qs(Strings::t("common.ok")), std::nullopt, {}},
                            ModalAnswer{qs(Strings::t("common.yes")), std::nullopt, {}}});

    ASSERT_TRUE(outcomes.at(0).appeared);
    EXPECT_EQ(outcomes.at(0).text, qs(Strings::t("error.book.hasActiveLoans")));
    EXPECT_FALSE(outcomes.at(1).appeared);
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId))
                    .isNull());
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bystander))
                    .isNull());
}

TEST_F(test_ui_CatalogRemoval, ABookWhoseCopiesAreAllInArchivesTheTitleOnYes)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(3));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(4));
    openPageAndSelect(bookId);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(); }, qs(Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Strings::t("catalog.deleteConfirm")));
    EXPECT_FALSE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId))
                     .isNull());
    // Archived, never destroyed -- and the neighbour is untouched.
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books").toInt(), 2);
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bystander))
                    .isNull());
}
```

- [ ] **Step 4: Register the test file**

In `applications/vlms/test/CMakeLists.txt`, add to `SRC` after `src/test_catalog_loans.cpp`:

```cmake
        src/test_catalog_removal.cpp
```

- [ ] **Step 5: Run to verify the first test fails**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_ui_CatalogRemoval' --output-on-failure 2>&1 | tail -30
```
Expected: `ABookWithACopyOutIsRefusedBeforeTheConfirmationIsAsked` fails — the first box today is the
confirmation, not the refusal.

- [ ] **Step 6: Add the pre-check**

In `applications/vlms/src/ui/catalog/CatalogPage.cpp`, in `deleteBook`, between the
`if (bookId <= 0) { ... }` block and the `askYesNo` call:

```cpp
    // Asked before the confirmation, as the Members page asks: being offered
    // "delete this book?" only to be told afterwards that it was never possible
    // is the long way round to the same no.
    const auto open = m_repository.bookHasOpenLoans(bookId);
    if (!open) {
        VLMS::showRepoError(this, open.error());
        return;
    }
    if (open.value()) {
        VLMS::showInformation(this, T("catalog.deleteBook"),
                                    T("error.book.hasActiveLoans"));
        return;
    }
```

No new string: the refusal reuses the sentence `archiveBook` already returns, so the two cannot
drift apart.

- [ ] **Step 7: Build and run**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_ui_Catalog|^test_core_Catalog' --output-on-failure
```
Expected: all passing.

- [ ] **Step 8: Commit**

```bash
git add applications/vlms/src/ui/catalog/CatalogPage.cpp \
        applications/vlms/test/src/test_catalog_removal.cpp \
        applications/vlms/test/CMakeLists.txt
git commit -m "Refuse a book that is out before asking to delete it.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 10: The free-number drop-down on a new copy row

The librarian keeps control of the number. The cell **opens on the next incremented number**, as it
does today; the free numbers are in the list behind it, and taking one is a deliberate pick. Nothing
changes unless the list is opened — which is what "keep the control by the librarian" has to mean
for a stock with 5,976 gaps whose provenance nobody can vouch for.

**Files:**
- Create: `applications/vlms/src/ui/catalog/FreeLocalNumberDelegate.h` / `.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (the `vlms_ui` source list)
- Modify: `applications/vlms/src/ui/catalog/BookCopiesTable.cpp`
- Create: `applications/vlms/test/src/test_free_number_picker.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `CatalogRepository::listFreeLocalNumbers` (Task 6).
- Produces: `FreeLocalNumberDelegate`, with `static constexpr int kFreeNumbersRole = Qt::UserRole + 4;` — a `QStringList` on the local-number item. `Qt::UserRole + 1..3` are already taken in this table by `kCopyIdRole`, `kCopyOnLoanRole` and `kCopyReservedRole`.

- [ ] **Step 1: Write the failing test**

Create `applications/vlms/test/src/test_free_number_picker.cpp`:

```cpp
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"

#include "ui/catalog/BookCopiesTable.h"
#include "ui/catalog/FreeLocalNumberDelegate.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/Locale.h>

#include <QAbstractItemModel>
#include <QComboBox>
#include <QStringList>
#include <QStyleOptionViewItem>
#include <QTableWidget>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using VLMS::Locale;
using namespace VLMS::Test;

class test_ui_FreeNumberPicker : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }
    static void TearDownTestSuite() { Locale::setCode(Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_table.reset();
        m_catalog.reset();
        m_db.reset();
    }

    /// Leaves the arabic stock holding 1 and 4, so 2 and 3 are free and the
    /// next incremented number is 5.
    void seedArabicGap()
    {
        BookSeed seed = uniqueBookSeed(1);
        seed.language = "ar";
        seed.initialCopyCount = 2;
        const std::int64_t bookId = seedBook(*m_db, seed);
        const auto copies = copyIdsOf(*m_db, bookId);
        ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(0), "1"));
        ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(1), "4"));
    }

    QTableWidget* gridOf(BookCopiesTable* widget) const
    {
        return widget->findChild<QTableWidget*>(QStringLiteral("copiesTable"));
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<BookCopiesTable> m_table;
};

TEST_F(test_ui_FreeNumberPicker, ANewRowOpensOnTheNextNumberAndOffersTheGapsBehindIt)
{
    seedArabicGap();
    m_table = std::make_unique<BookCopiesTable>(*m_catalog);
    m_table->addRow(QStringLiteral("ar"));

    QTableWidget* grid = gridOf(m_table.get());
    ASSERT_NE(grid, nullptr);
    ASSERT_EQ(grid->rowCount(), 1);
    QTableWidgetItem* cell = grid->item(0, 0);
    ASSERT_NE(cell, nullptr);

    // What a save would use if nobody opened the list: the incremented number.
    EXPECT_EQ(cell->text(), QStringLiteral("5"));

    // The gaps are there to be picked, never picked for the librarian.
    const QStringList offered = cell->data(FreeLocalNumberDelegate::kFreeNumbersRole).toStringList();
    EXPECT_EQ(offered, (QStringList{QStringLiteral("5"), QStringLiteral("2"), QStringLiteral("3")}));
}

TEST_F(test_ui_FreeNumberPicker, TheEditorIsAComboOfThoseNumbersAndStillTakesATypedOne)
{
    seedArabicGap();
    m_table = std::make_unique<BookCopiesTable>(*m_catalog);
    m_table->addRow(QStringLiteral("ar"));

    QTableWidget* grid = gridOf(m_table.get());
    QTableWidgetItem* cell = grid->item(0, 0);
    auto* delegate = qobject_cast<FreeLocalNumberDelegate*>(grid->itemDelegateForColumn(0));
    ASSERT_NE(delegate, nullptr);

    QWidget* editor = delegate->createEditor(grid->viewport(), QStyleOptionViewItem{},
                                             grid->model()->index(0, 0));
    ASSERT_NE(editor, nullptr);
    auto* combo = qobject_cast<QComboBox*>(editor);
    ASSERT_NE(combo, nullptr);
    EXPECT_TRUE(combo->isEditable());
    EXPECT_EQ(combo->count(), 3);
    EXPECT_EQ(combo->itemText(0), QStringLiteral("5"));
    EXPECT_EQ(combo->itemText(1), QStringLiteral("2"));

    combo->setCurrentText(QStringLiteral("99"));
    delegate->setModelData(combo, grid->model(), grid->model()->index(0, 0));
    EXPECT_EQ(cell->text(), QStringLiteral("99"));
    delete editor;
}

TEST_F(test_ui_FreeNumberPicker, BookCreationKeepsIncrementingAndNeverTakesAGap)
{
    seedArabicGap();

    // Several numbers minted at once with nobody looking at them: precisely
    // where a gap must not be taken silently.
    BookSeed fresh = uniqueBookSeed(9);
    fresh.language = "ar";
    fresh.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, fresh);

    const auto copies = VLMS_UNWRAP(m_catalog->listCopies(bookId));
    ASSERT_EQ(copies.size(), 2u);
    EXPECT_EQ(copies.at(0).localId, "5");
    EXPECT_EQ(copies.at(1).localId, "6");
    // 2 and 3 are still free -- book creation did not help itself to them.
    EXPECT_EQ(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100)),
              (std::vector<std::string>{"2", "3"}));
}

TEST_F(test_ui_FreeNumberPicker, ARowLoadedFromTheDatabaseGetsNoDropDown)
{
    BookSeed seed = uniqueBookSeed(2);
    seed.language = "ar";
    seed.initialCopyCount = 1;
    const std::int64_t bookId = seedBook(*m_db, seed);
    m_table = std::make_unique<BookCopiesTable>(*m_catalog);
    m_table->loadCopies(bookId);

    QTableWidget* grid = gridOf(m_table.get());
    ASSERT_EQ(grid->rowCount(), 1);
    // An existing copy's number is not a fresh choice, so the list stays shut.
    EXPECT_TRUE(grid->item(0, 0)->data(FreeLocalNumberDelegate::kFreeNumbersRole)
                    .toStringList()
                    .isEmpty());

    auto* delegate = qobject_cast<FreeLocalNumberDelegate*>(grid->itemDelegateForColumn(0));
    ASSERT_NE(delegate, nullptr);
    QWidget* editor = delegate->createEditor(grid->viewport(), QStyleOptionViewItem{},
                                             grid->model()->index(0, 0));
    EXPECT_EQ(qobject_cast<QComboBox*>(editor), nullptr);
    delete editor;
}
```

Add `src/test_free_number_picker.cpp` to `SRC` in `applications/vlms/test/CMakeLists.txt`,
after `src/test_catalog_removal.cpp`.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j 2>&1 | tail -20
```
Expected: `ui/catalog/FreeLocalNumberDelegate.h: No such file or directory`.

- [ ] **Step 3: Write the delegate**

Create `applications/vlms/src/ui/catalog/FreeLocalNumberDelegate.h`:

```cpp
#pragma once

#include <QStyledItemDelegate>

/// Editor for a **new** copy row's local number. The cell opens on the next
/// incremented number; the stock's free numbers sit behind it in an editable
/// combo, so reusing a gap is a deliberate pick and never a default. Rows
/// loaded from the database carry no numbers in the role and fall back to the
/// plain line edit -- changing an existing copy's number is a different act.
class FreeLocalNumberDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    /// QStringList: the number the cell opens on, then the free ones. Roles
    /// +1..+3 are already taken by the copies table.
    static constexpr int kFreeNumbersRole = Qt::UserRole + 4;

    explicit FreeLocalNumberDelegate(QObject* parent = nullptr);

    [[nodiscard]] QWidget* createEditor(QWidget* parent,
                                        const QStyleOptionViewItem& option,
                                        const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor,
                      QAbstractItemModel* model,
                      const QModelIndex& index) const override;
};
```

Create `applications/vlms/src/ui/catalog/FreeLocalNumberDelegate.cpp`:

```cpp
#include "ui/catalog/FreeLocalNumberDelegate.h"

#include <QComboBox>
#include <QStringList>

FreeLocalNumberDelegate::FreeLocalNumberDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QWidget* FreeLocalNumberDelegate::createEditor(QWidget* parent,
                                               const QStyleOptionViewItem& option,
                                               const QModelIndex& index) const
{
    const QStringList numbers = index.data(kFreeNumbersRole).toStringList();
    if (numbers.isEmpty()) {
        return QStyledItemDelegate::createEditor(parent, option, index);
    }

    auto* combo = new QComboBox(parent);
    // Editable: a number typed by hand still works, and is validated by the
    // save exactly as it was before this drop-down existed.
    combo->setEditable(true);
    combo->addItems(numbers);
    combo->setCurrentIndex(0);
    return combo;
}

void FreeLocalNumberDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (combo == nullptr) {
        QStyledItemDelegate::setEditorData(editor, index);
        return;
    }
    combo->setCurrentText(index.data(Qt::EditRole).toString());
}

void FreeLocalNumberDelegate::setModelData(QWidget* editor,
                                           QAbstractItemModel* model,
                                           const QModelIndex& index) const
{
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (combo == nullptr) {
        QStyledItemDelegate::setModelData(editor, model, index);
        return;
    }
    model->setData(index, combo->currentText().trimmed(), Qt::EditRole);
}
```

Add both files to the `vlms_ui` source list in `applications/vlms/CMakeLists.txt`, beside
the existing `src/ui/catalog/LocalNumberDelegate.cpp` / `.h` entries.

- [ ] **Step 4: Fill the role and install the delegate**

In `applications/vlms/src/ui/catalog/BookCopiesTable.cpp`, add the include beside the others:

```cpp
#include "ui/catalog/FreeLocalNumberDelegate.h"
```

In the constructor, after the `VLMS::enableWidgetTableSort(...)` line:

```cpp
    m_table->setItemDelegateForColumn(kCopyLocalId, new FreeLocalNumberDelegate(m_table));
```

In `addRow`, after the three `copy.*` assignments and before `appendCopyRow(copy);`:

```cpp
    // The incremented number first -- it is what the cell opens on and what a
    // save uses if nobody touches the list. The gaps follow, capped: a
    // drop-down with 5,976 entries is not a choice, it is a haystack.
    QStringList offered{qs(localId)};
    if (const auto free = m_repository.listFreeLocalNumbers(source, kFreeNumberLimit)) {
        for (const std::string& number : free.value()) {
            offered.append(qs(number));
        }
    }
```

and immediately after `appendCopyRow(copy);`:

```cpp
    if (QTableWidgetItem* numberCell = m_table->item(m_table->rowCount() - 1, kCopyLocalId)) {
        numberCell->setData(FreeLocalNumberDelegate::kFreeNumbersRole, offered);
    }
```

Add the cap beside the other file-local constants (after `kCopyReservedRole`):

```cpp
constexpr int kFreeNumberLimit = 100;
```

`addReservedRow` and `loadCopies` set no role, so their cells keep the plain editor — which is what
the third test pins.

- [ ] **Step 5: Build and run**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build -R '^test_ui_FreeNumberPicker|^test_ui_BookEditor|^test_ui_ArchivePage' --output-on-failure
```
Expected: all passing.

- [ ] **Step 6: Commit**

```bash
git add applications/vlms/src/ui/catalog/FreeLocalNumberDelegate.h \
        applications/vlms/src/ui/catalog/FreeLocalNumberDelegate.cpp \
        applications/vlms/src/ui/catalog/BookCopiesTable.cpp \
        applications/vlms/CMakeLists.txt \
        applications/vlms/test/src/test_free_number_picker.cpp \
        applications/vlms/test/CMakeLists.txt
git commit -m "Offer the free local numbers without choosing one.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 11: Whole suite, the real application, and the session log

Green tests are not the finish line in this repo: the last three pieces of work each hit a trap
invisible to every property a test can read (a lone widget taking a whole row, a stylesheet type
selector matching a subclass, a combo popup repainting its items). The funnel has to be walked in
the real application before it is called done.

**Files:**
- Modify: `CLAUDE.md` (Session log)

**Interfaces:**
- Consumes: everything.
- Produces: nothing.

- [ ] **Step 1: Run the whole suite**

```bash
cmake --build build -j 2>&1 | tail -5
ctest --test-dir build --output-on-failure 2>&1 | tail -30
```
Expected: every test passing. Do not proceed on a failure — fix it.

- [ ] **Step 2: Build a sandbox application, never the live database**

`database/vlms.db` holds real library and member data and must never be opened by a test run,
uploaded, or pasted into a chat. Configure a second build with dev paths **off**: that drops the
compiled-in `VLMS_PROJECT_ROOT`, so `Application` falls back to `applicationDirPath()` and
reads `<bindir>/database/vlms.db` instead of the repo's.

```bash
cmake -S . -B build-sandbox -DCMAKE_BUILD_TYPE=Release \
      -DVLMS_DEV_PATHS=OFF -DVLMS_BUILD_TESTS=OFF
cmake --build build-sandbox -j 2>&1 | tail -5
mkdir -p build-sandbox/bin/database build-sandbox/bin/resources
cp database/vlms.db build-sandbox/bin/database/
cp -r resources/books build-sandbox/bin/resources/
```

Leave `build-sandbox/bin/resources/members` empty — it holds member photos and ID scans. Then
scrub the member PII in the **copy** (names, phone, address, email, notes) with one `UPDATE`, so a
screenshot can be shown without exposing real records. `/build-*/` is already gitignored. Set
`VLMS_SCHEMA_PATH` to the repo's `database/schema.sql`, since turning dev paths off drops that
define too.

Check for the user's own instance before doing anything: `pgrep -x vlms`. A dev-build instance
is bound to the **live** database and synthetic clicks are global, so one landing on its window
would edit real data. Ask before killing it — it is their session, not a stale process.

- [ ] **Step 3: Launch it and capture**

The app is a native Wayland client by default, so X tools cannot see it; and it **quits within
seconds unless it is the launching shell's foreground process**. Put the capture sequence in a
background subshell and run the app in the foreground:

```bash
export SHOT_DIR=/tmp/vlms-funnel && mkdir -p "$SHOT_DIR"
( sleep 4
  WID=$(xwininfo -root -tree | grep '"vlms" "VLMS"' | grep -v ' 1x1' \
        | head -1 | awk '{print $1}')
  xwd -id "$WID" | magick xwd:- "$SHOT_DIR/archive.png"
  pkill -x vlms ) &
QT_QPA_PLATFORM=xcb DISPLAY=:1 ./build-sandbox/bin/vlms
```

`xwd -root` fails with BadMatch here (the root spans 3200x1080 across two monitors, partly
unbacked), which is why the capture is by client window id. A modal dialog is its own X window: re-run
`xwininfo -root -tree` after opening one and take the `("vlms" "VLMS")` entry that is not
the main window.

- [ ] **Step 4: Walk the funnel by hand**

Confirm each of these in the running application, in Arabic and in English:

1. Members → Delete on a member with an unreturned loan: the refusal with *Go to loans*, no checkbox.
2. Members → Delete on any other member: a plain yes/no, then the row leaves the list.
3. Archive → Members: the new Loans column; `Permanently remove` disabled on a row reading 1, enabled on a row reading 0.
4. Archive → Loans: `Permanently remove` a returned archived loan, then return to Members and watch that member's Loans column fall to 0 and the button come alive.
5. Archive → Books: the button disabled while Copies reads 2; remove both copies under Archive → Copies, then the title goes.
6. Catalogue → Delete on a book with a copy out: the refusal comes first, with no confirmation behind it.
7. Book editor → Add Copy: the number cell opens on the incremented number; the drop-down lists the gaps; a typed number still saves.

Screenshot at least the Archive page with the new button and column, and the copies drop-down open.

- [ ] **Step 5: Add the session log entry**

At the top of the Session log in `CLAUDE.md` (newest first), a two-or-three-line entry covering what
the next session would otherwise re-derive: that delete archives everywhere and permanent removal
lives only in the Archive behind four `purge*` calls that each refuse an unarchived record; that a
book needs zero copies rather than zero loans, because the copies go one at a time; that the Loans
column counts archived loans too, since an archived loan still blocks; and that a free local number
is computed (no row holds it) rather than stored, so the 8,175 import gaps are offered from day one
but never chosen automatically. Name the spec path.

- [ ] **Step 6: Commit**

```bash
git add CLAUDE.md
git commit -m "Note the permanent removal funnel in the session log.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```
