# Row ticks and bulk actions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tick boxes on the list tables, and the destructive buttons acting on every ticked row at once, while a click with nothing ticked still acts on the highlighted row.

**Architecture:** `TableRowChecks` paints a check inside column 0 and a tri-state box in the header of section 0, without moving any column index. Read-only `can…` twins on the three repositories return the same `Status` the matching action already returns, and each action calls its twin first so the rule lives once. `runBulkAction` sorts the ticked rows into passes and blocked, asks, then runs the existing single-row action once per pass.

**Tech Stack:** Qt 6 Widgets, C++17, SQLite via `VLMS::SqliteSession`, GoogleTest.

## Global Constraints

- **No schema change.** No migration, no new table.
- **Repositories are Qt-free.** Core uses `std::string`, `std::vector`, `std::int64_t`, and returns `VLMS::Result<T>` / `Status` — never a raw driver string. Errors carry a key that `Strings::t()` translates.
- **Every new user-visible string is three entries**, added to the Arabic, French and English tables in `libraries/Core/src/Strings.cpp`. `test_core_StringsParity` fails if a key is missing from any table.
- **British English in prose and comments** (catalogue, not catalog) — never in Qt API names or existing identifiers, which stay as they are (`CatalogRepository`, `CatalogPage`).
- **Judge Core tests by `ctest`, never by running the binary.** `./build/bin/test_vlms_core` fails tests on its own: the schema path, data directory and TZ come from per-test `ENVIRONMENT` in `cmake/TestUtils.cmake`.
- Build with `cmake --build build -j`. Run one suite with `ctest --test-dir build -R '<regex>' --output-on-failure`.
- `createBook` rejects `initialCopyCount < 1` with `error.book.minCopies`. To seed a copy-less book, seed one copy and archive it with `saveCopies(bookId, {})` — for a purge of the title, archive the book and `purgeCopy` each copy instead, because an archived copy still counts.
- **Every gate test seeds a second record that must survive.** A check that ignores its id must fail.
- The checkbox sits **inside the first cell**, not in a new column. No `item(row, N)` index moves.
- **Nothing ticked:** the button acts on the highlighted row, exactly as today, including the Members "Go to loans" warning. Ticks do not move the highlighted row. Edit and Loans ignore ticks.
- **Select all** ticks the rows currently on screen, not the query beyond the page. A press on the header box does not sort; a press on the header text does.
- Ticks last until the row-refresh method runs (`clear()`).
- Bulk does not cascade. Each row is the existing single-row action in its own transaction (`SqliteSession::transaction` does not nest). `can…` methods do not open a transaction.
- Restore has no `can…`. Bulk Restore loops `restore*` and puts `notFound` / `notArchived` failures in the summary. The Archive emits `recordRestored()` once per batch, not once per row.
- Reuse stays single-row.
- Metrics and the loan-history dialogs get no ticks.
- Do not touch `database/vlms.db` or `resources/books/**` or `resources/members/**`.

**Spec:** `docs/superpowers/specs/2026-09-23-row-ticks-bulk-actions-design.md`

---

## File Structure

| File | Responsibility |
|---|---|
| `libraries/Core/include/VLMS/Core/CatalogRepository.h` / `src/CatalogRepository.cpp` | `canArchiveBook`, `canPurgeBook`, `canPurgeCopy`; actions call them |
| `libraries/Core/src/BookCopyStore.h` / `.cpp` | `canPurgeCopy` implementation, called by `purgeCopy` |
| `libraries/Core/include/VLMS/Core/MemberRepository.h` / `src/MemberRepository.cpp` | `canArchiveMember`, `canPurgeMember` |
| `libraries/Core/include/VLMS/Core/CirculationRepository.h` / `src/CirculationRepository.cpp` | `canArchiveLoan`, `canPurgeLoan` |
| `libraries/Core/src/Strings.cpp` | bulk prompt, noun, and verb keys, three tables |
| `libraries/Core/test/src/test_can_remove.cpp` | `ok`, each refusal key, and the paired action |
| `applications/vlms/src/ui/TableRowChecks.h` / `.cpp` | row boxes, header box, `checkedIds` / `checkedRows` / `clear` |
| `applications/vlms/src/ui/BulkAction.h` / `.cpp` | `runBulkAction` |
| `applications/vlms/src/ui/catalog/CatalogPage.*` | ticks + bulk Delete |
| `applications/vlms/src/ui/members/MembersPage.*` | ticks + bulk Delete |
| `applications/vlms/src/ui/circulation/CirculationPage.*` | ticks + bulk Delete |
| `applications/vlms/src/ui/archive/ArchivePage.*` | ticks + bulk Restore and Purge |
| `applications/vlms/src/ui/catalog/BookCopiesTable.*` | ticks + bulk Remove |
| `applications/vlms/src/ui/catalog/FreeLocalNumberDelegate.*` | paint the check indicator |
| `applications/vlms/CMakeLists.txt` | the four new UI sources |
| `applications/vlms/test/src/test_table_row_checks.cpp` | header, selection, RTL, `clear` |
| `applications/vlms/test/src/test_bulk_action.cpp` | passes, No, summary |
| `applications/vlms/test/src/test_bulk_pages.cpp` | two ticked rows, none ticked, a blocked row stays |
| `applications/vlms/test/src/test_bulk_copies.cpp` | remove, blockers, double-click, painted indicator |
| `applications/vlms/test/CMakeLists.txt` | the four new UI tests |
| `libraries/Core/test/CMakeLists.txt` | `test_can_remove.cpp` |

Task 1 (Core gates), Task 2 (strings) and Task 3 (`TableRowChecks`) share no files. Task 4 (`runBulkAction`) needs Task 2's keys. Tasks 5–9 need Tasks 1, 3 and 4, and each page is its own files. The shared `build/` directory means `cmake --build` runs one at a time even when the edits do not overlap.

---

### Task 1: `can…` gates

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/CatalogRepository.h`
- Modify: `libraries/Core/src/CatalogRepository.cpp`
- Modify: `libraries/Core/src/BookCopyStore.h`
- Modify: `libraries/Core/src/BookCopyStore.cpp`
- Modify: `libraries/Core/include/VLMS/Core/MemberRepository.h`
- Modify: `libraries/Core/src/MemberRepository.cpp`
- Modify: `libraries/Core/include/VLMS/Core/CirculationRepository.h`
- Modify: `libraries/Core/src/CirculationRepository.cpp`
- Create: `libraries/Core/test/src/test_can_remove.cpp`
- Modify: `libraries/Core/test/CMakeLists.txt` (`TST_SOURCES`)

**Interfaces:**
- Consumes: `bookHasOpenLoans`, `removalBlock`, the existing `archive*` / `purge*` bodies.
- Produces (all `[[nodiscard]] VLMS::Status`, const, no transaction of their own):
  - `CatalogRepository::canArchiveBook(std::int64_t id)` — `error.book.hasActiveLoans`, else `error.book.notFound` when the title is missing or already archived, else `ok()`.
  - `CatalogRepository::canPurgeBook(std::int64_t id)` — `error.book.notFound`, `error.book.notArchived`, `error.book.hasCopies`.
  - `CatalogRepository::canPurgeCopy(std::int64_t copyId)` — delegates to `BookCopyStore::canPurgeCopy`. Keys: `error.copy.notFound`, `error.copy.notArchived`, `error.copy.hasHistory`.
  - `MemberRepository::canArchiveMember(std::int64_t id)` — `error.member.archiveHasLoans`, else `error.member.notFound`.
  - `MemberRepository::canPurgeMember(std::int64_t id)` — `error.member.notFound`, `error.member.notArchived`, `error.member.deleteHasLoans`, `error.member.hasHistory`. Not-archived is decided before the loan block, matching `purgeMember`.
  - `CirculationRepository::canArchiveLoan(std::int64_t loanId)` — `error.loan.notFound`, `error.loan.archiveOpen` (open is reported even when the row is also archived).
  - `CirculationRepository::canPurgeLoan(std::int64_t loanId)` — `error.loan.notFound`, `error.loan.notArchived`.
- Each action calls its `can…` first and does not repeat that rule. `archiveBook` / `purgeBook` / `purgeCopy` stay inside the transaction they already open. `archiveMember`, `purgeMember` (SQL only; the photo folder stays outside), `archiveLoan` and `purgeLoan` gain a transaction around the check plus the write. `bookHasOpenLoans` and `removalBlock` stay.

- [ ] **Step 1: Write the failing tests** in `test_can_remove.cpp`. One fixture with all three repositories. For every `can…`: `ok` on a row that would pass; each refusal key; a missing id returns `notFound`. For each refusal and the missing id, the action on the same row returns the same key. Every test seeds a second record and asserts it is unchanged. An archived member who still has a book out is seeded with SQL (`UPDATE members SET archived_at = …`) because `archiveMember` itself refuses that row.

- [ ] **Step 2: Run** `cmake --build build -j --target test_vlms_core && ctest --test-dir build -R 'test_core_CanRemove' --output-on-failure` and confirm the tests fail to compile because the methods are missing.

- [ ] **Step 3: Implement** the declarations and the bodies. `purgeCopy`'s reads move into `BookCopyStore::canPurgeCopy`; `purgeCopy` calls it, then deletes.

- [ ] **Step 4: Run** the same `ctest` filter. Expected: PASS. Then `ctest --test-dir build -R 'test_core_Archive|test_core_CanRemove' --output-on-failure` so the existing archive tests still pass.

- [ ] **Step 5: Commit** only the Task 1 files.

---

### Task 2: Bulk strings

**Files:**
- Modify: `libraries/Core/src/Strings.cpp` (three tables, immediately after `archive.purgeConfirmLoan`)

**Interfaces:**
- Produces these keys (placeholders must match across the three tables):

| Key | English |
|---|---|
| `bulk.confirm` | `{verb} {count} {noun}?` |
| `bulk.blockedIntro` | `{blocked} of {count} {noun} can't be {passive}:` |
| `bulk.andMore` | `…and {count} more` |
| `bulk.confirmRest` | `{verb} the other {count}?` |
| `bulk.summary` | `{done} {passive}, {failed} failed: {reason}` |
| `bulk.noun.books` | `books` |
| `bulk.noun.members` | `members` |
| `bulk.noun.loans` | `loans` |
| `bulk.noun.copies` | `copies` |
| `bulk.verb.archive` | `Archive` |
| `bulk.passive.archive` | `archived` |
| `bulk.verb.restore` | `Restore` |
| `bulk.passive.restore` | `restored` |
| `bulk.verb.purge` | `Permanently remove` |
| `bulk.passive.purge` | `removed` |
| `bulk.verb.remove` | `Remove` |
| `bulk.passive.remove` | `removed` |

Arabic (same keys, same placeholders): confirm `هل تريد {verb} {count} {noun}؟`; intro `تعذّر {passive} {blocked} من {count} {noun}:`; andMore `…و{count} أخرى`; confirmRest `{verb} الـ {count} الأخرى؟`; summary `تم {passive} {done}، وفشل {failed}: {reason}`. Nouns: `كتب` `أعضاء` `إعارات` `نسخ`. Verbs: `أرشفة` / `أرشفتها`, `استرجاع` / `استرجاعها`, `حذف` / `حذفها`, `إزالة` / `إزالتها`.

French: confirm `{verb} {count} {noun} ?`; intro `{blocked} {noun} sur {count} ne peuvent pas être {passive} :`; andMore `…et {count} de plus`; confirmRest `{verb} les {count} autres ?`; summary `{done} {passive}, {failed} échec : {reason}`. Nouns: `livres` `adhérents` `prêts` `exemplaires`. Verbs: `Archiver` / `archivés`, `Restaurer` / `restaurés`, `Supprimer définitivement` / `supprimés`, `Retirer` / `retirés`.

- [ ] **Step 1:** Add the English block only. Run `ctest --test-dir build -R test_core_StringsParity --output-on-failure`. Expected: FAIL, keys missing from Arabic and French.
- [ ] **Step 2:** Add the Arabic and French blocks with the same placeholders.
- [ ] **Step 3:** Re-run the parity test. Expected: PASS.
- [ ] **Step 4: Commit** `Strings.cpp` only.

---

### Task 3: `TableRowChecks`

**Files:**
- Create: `applications/vlms/src/ui/TableRowChecks.h`
- Create: `applications/vlms/src/ui/TableRowChecks.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (both the `.cpp` and the `.h` lists)
- Create: `applications/vlms/test/src/test_table_row_checks.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt`

**Interfaces:**
- Consumes: a `QTableWidget` whose column-0 item carries the row. Install **before** `TableHeaderSort` / `enableWidgetTableSort`, because those connect to the header that exists at construction, and this class replaces it.
- Produces:

```cpp
class TableRowChecks : public QObject {
    Q_OBJECT
public:
    explicit TableRowChecks(QTableWidget* table, QObject* parent = nullptr);
    [[nodiscard]] QList<qint64> checkedIds() const; // Qt::UserRole on column 0
    [[nodiscard]] QList<int> checkedRows() const;
    void clear(); // ItemIsUserCheckable + Unchecked on every column-0 item
signals:
    void checkedCountChanged(int count);
};
```

`CheckHeaderView` (private to the `.cpp`, installed as the horizontal header) paints a tri-state indicator at the leading edge of section 0 (`Qt::RightToLeft` → the right edge of that section). A press that starts and ends inside the indicator toggles every on-screen row (none or some → all checked; all → none) and does **not** emit `sectionClicked`. Any other press goes to `QHeaderView`. A viewport event filter eats press/release/double-click that land in column 0's check indicator (`QStyle::SE_ItemViewItemCheckIndicator`) and toggles that row, so the highlighted row does not move. Clicks elsewhere are left to the table.

- [ ] **Step 1: Failing tests** (`test_ui_TableRowChecks`):
  - Three rows with ids 10, 20, 30. Select row 0. Click the indicator of row 1: row 1 is checked, `currentRow()` stays 0, `checkedIds()` is `{20}`.
  - Click the header indicator: all three checked, header state `Checked`, `sectionClicked` / `sortChanged` did not fire.
  - Uncheck one row: header is `PartiallyChecked`. Click the header indicator again: all checked. Click it again: none checked.
  - Click the header **text** (past the indicator): `sortChanged` fires.
  - `clear()` unchecks every row.
  - Under `Qt::RightToLeft`, the header indicator's centre x is greater than the centre of section 0.
- [ ] **Step 2:** Build `test_vlms_ui` and run `ctest --test-dir build -R test_ui_TableRowChecks --output-on-failure`. Expected: FAIL (missing type).
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** Re-run that filter plus `test_ui_TableHeaderSort`. Expected: PASS.
- [ ] **Step 5: Commit.**

---

### Task 4: `runBulkAction`

**Files:**
- Create: `applications/vlms/src/ui/BulkAction.h`
- Create: `applications/vlms/src/ui/BulkAction.cpp`
- Modify: `applications/vlms/CMakeLists.txt`
- Create: `applications/vlms/test/src/test_bulk_action.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2 keys, `askYesNo` / `showInformation` from `UiHelpers.h`, `Strings::t` via `QtBridge.h`.
- Produces:

```cpp
struct BulkRow { qint64 id = 0; int row = -1; QString label; };
struct BulkActionTexts { QString title; QString verb; QString passive; QString noun; };

int runBulkAction(QWidget* parent,
                  const BulkActionTexts& texts,
                  const QList<BulkRow>& rows,
                  const std::function<VLMS::Status(const BulkRow&)>& check,
                  const std::function<VLMS::Status(const BulkRow&)>& act);
```

Returns how many rows `act` returned `ok()` for. The caller refreshes only when that is `> 0` (No, and an all-blocked OK, leave the ticks).

Flow:
1. `check` each row. `ok()` → passes, in input order. Anything else → blocked, reason `qs(Strings::t(error.key))`.
2. Ask, using `fill(key, replacements)` in this file (the templates have more than two placeholders, so this is not `Strings::t2`):
   - No blocked rows: `askYesNo` with `bulk.confirm`.
   - Some blocked: intro, then one `label — reason` line per blocked row, at most 10, then `bulk.andMore` when more remain, then `bulk.confirmRest`. `askYesNo`.
   - All blocked: the same list without `confirmRest`. `showInformation` (OK only). `act` is not called.
3. On Yes, loop `act` over passes. `QProgressDialog` with `setMinimumDuration(500)`, range `0 .. passes.size()`, label the row's `label`. Cancel stops between rows; rows already done stay done.
4. After the loop, `showInformation` with `bulk.summary` only when some `act` failed or the dialog was cancelled. `{reason}` is the first failure's translated key, or empty when the only news is a cancel with no failure.

- [ ] **Step 1: Failing tests** (`test_ui_BulkAction`), `Locale::setCode("en")`, modals answered through `ModalTest.h`:
  - Two rows, both checks ok, answer No: `act` not called, return 0, the box text is `Archive 2 books?`.
  - Answer Yes: both ids acted, return 2, no second box.
  - Three rows, check fails the second with `error.book.hasActiveLoans`: the box contains `1 of 3 books can't be archived:`, the label, the translated reason, and `Archive the other 2?`. Yes acts on the two passes only.
  - All three fail: one OK box, `act` not called, return 0.
  - Twelve blocked, one pass: the box contains ten label lines and `…and 2 more`.
  - Yes, and `act` fails the second row: return 1, summary `1 archived, 1 failed:` plus the translated key.
- [ ] **Step 2:** Run `ctest --test-dir build -R test_ui_BulkAction --output-on-failure`. Expected: FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** Re-run. Expected: PASS.
- [ ] **Step 5: Commit.**

---

### Task 5: Catalogue Delete

**Files:**
- Modify: `applications/vlms/src/ui/catalog/CatalogPage.h` / `.cpp`
- Modify: `applications/vlms/test/src/test_catalog_removal.cpp` (add cases; do not weaken the two that exist)

**Interfaces:**
- Consumes: `TableRowChecks` (construct `m_checks` immediately after `m_booksTable` exists and **before** `new TableHeaderSort`), `runBulkAction`, `canArchiveBook`, `archiveBook`.
- `refreshBooks` calls `m_checks->clear()` after the rows are filled.
- `deleteBook`: when `checkedIds()` is empty, the body stays as it is (`bookHasOpenLoans`, then `catalog.deleteConfirm`, then `archiveBook`). When it is not, build `BulkRow` from column 0 (id = `Qt::UserRole`, label = the title text) and call `runBulkAction` with `catalog.deleteBook`, `bulk.verb.archive`, `bulk.passive.archive`, `bulk.noun.books`. `check` is `canArchiveBook`, `act` is `archiveBook`. If the return is `> 0`, `refreshLanguages()`, `refreshCategories()`, `refreshBooks()`.

- [ ] **Step 1: Failing tests** in `test_ui_CatalogRemoval`:
  - Two books ticked, a third only highlighted: Yes archives the two ticked ones and leaves the third live.
  - Nothing ticked, one row highlighted: Yes archives only that row (the existing confirm text `catalog.deleteConfirm`).
  - One ticked book has a copy out, the other does not: the box names the blocked title, Yes archives the other, the blocked one stays live.
- [ ] **Step 2:** Run `ctest --test-dir build -R test_ui_CatalogRemoval --output-on-failure`. Expected: the new tests FAIL, the old ones PASS.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** Re-run `test_ui_CatalogRemoval` and `test_ui_CatalogSort`. Expected: PASS.
- [ ] **Step 5: Commit.**

---

### Task 6: Members Delete

**Files:**
- Modify: `applications/vlms/src/ui/members/MembersPage.h` / `.cpp`
- Modify: `applications/vlms/test/src/test_member_removal.cpp`

**Interfaces:**
- Same attachment rule as Task 5, on `m_membersTable`, `clear()` at the end of `refreshMembers`.
- `deleteMember`: empty ticks keep today's body, including `showWarningWithAction` and `memberLoansRequested`. Non-empty ticks: `BulkRow` id from column 0 `Qt::UserRole`, label from column 1 (the full name). Texts: `members.deleteMember`, archive verb/passive, `bulk.noun.members`. `check` = `canArchiveMember`, `act` = `archiveMember`. On `> 0`: `refreshAllFilters()` then `refreshMembers()`.

- [ ] **Step 1: Failing tests:**
  - Two ticked members are both archived; a third live member stays.
  - Nothing ticked: only the highlighted member is archived, and the confirm text is still `members.deleteConfirm`.
  - A ticked member with a book out stays; the other ticked member is archived. The single-row "Go to loans" box is not the one shown (the bulk intro is).
- [ ] **Step 2–5:** `ctest --test-dir build -R 'test_ui_MemberRemoval|test_ui_MemberFilters' --output-on-failure`, implement, re-run, commit.

---

### Task 7: Circulation Delete

**Files:**
- Modify: `applications/vlms/src/ui/circulation/CirculationPage.h` / `.cpp`
- Create the circulation cases inside `applications/vlms/test/src/test_bulk_pages.cpp`

**Interfaces:**
- Attach before `TableHeaderSort` on `m_loansTable`. `clear()` at the end of `refreshLoans`.
- `archiveLoan`: empty ticks keep today's body (`circulation.archiveLoan`, then `archiveLoan`). Non-empty: label is column 0 text + ` — ` + column 2 text (member — title). Texts: `circulation.delete`, archive verb/passive, `bulk.noun.loans`. `check` = `canArchiveLoan`, `act` = `archiveLoan`. On `> 0`: `refreshLoans()`.

- [ ] **Step 1: Failing tests** (seed two returned loans and one still out, plus a bystander returned loan):
  - Tick the two returned loans, highlight the bystander: both ticked loans are archived, the bystander stays live.
  - Nothing ticked: only the highlighted returned loan is archived.
  - Tick one open loan and one returned loan: the open one stays, the returned one is archived after Yes.
- [ ] **Step 2–5:** `ctest --test-dir build -R 'test_ui_BulkPages|test_ui_CirculationFilters' --output-on-failure`, implement, re-run, commit.

---

### Task 8: Archive Restore and Purge

**Files:**
- Modify: `applications/vlms/src/ui/archive/ArchivePage.h` / `.cpp`
- Modify: `applications/vlms/test/src/test_archive_page.cpp`

**Interfaces:**
- Attach before `TableHeaderSort`. `refreshRows` calls `m_checks->clear()` after a successful fill (every type). The type switch already calls `refreshRows`, so the ticks die with the columns.
- Labels: Members column 1, Books column 0, Copies `localId — title` (column 0 and column 2), Loans `member — title` (column 0 and the book title after ` / ` in column 1, or column 0 + column 1).
- `restoreSelected`: empty ticks keep today's confirm (`archive.restoreConfirm`) and emit `recordRestored()` once. Non-empty: `check` always returns `Status::ok()`. `act` is the existing `restore*` switch. Texts: `archive.restore`, restore verb/passive, noun for the current type (`members` / `books` / `copies` / `loans`). On `> 0`: `refreshRows()` once, then `emit recordRestored()` once.
- `purgeSelected`: empty ticks keep today's per-type confirm. Non-empty: `check` is `canPurge*`, `act` is `purge*`. Texts: `archive.purge`, purge verb/passive, the same noun. On `> 0`: `refreshRows()` once and do **not** emit `recordRestored()`.

- [ ] **Step 1: Failing tests** (archived members, and archived loans that are the bottom of the funnel so purge can succeed):
  - Two ticked archived members, Restore + Yes: both live again, `recordRestored` fired once.
  - Nothing ticked: only the highlighted member is restored.
  - Two ticked archived loans, Purge + Yes: both rows are gone. A third archived loan stays.
  - A ticked archived book that still has a copy is refused; a ticked archived copy with no loans is purged. The book stays.
- [ ] **Step 2–5:** `ctest --test-dir build -R test_ui_ArchivePage --output-on-failure`, implement, re-run, commit.

---

### Task 9: Copies tab

**Files:**
- Modify: `applications/vlms/src/ui/catalog/BookCopiesTable.h` / `.cpp`
- Modify: `applications/vlms/src/ui/catalog/FreeLocalNumberDelegate.h` / `.cpp`
- Create: `applications/vlms/test/src/test_bulk_copies.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt`

**Interfaces:**
- `TableRowChecks` is constructed **before** `enableWidgetTableSort`. `loadCopies` calls `clear()` after the rows exist. New rows from `addRow` are checkable because `appendCopyRow` ends in `clear()` — `clear()` only unchecks, which is correct for a freshly appended row as well as a reload. Do not call `clear()` from `removeSelectedRow` (the row is already gone).
- `removeSelectedRow`: when `checkedRows()` is empty, the body stays (on-loan warning, reserved row silently stays). When it is not, build rows **highest index first**. `check` reads `kCopyOnLoanRole` → `Status::fail(Validation, "book.copy.cannotRemoveOnLoan")` and `kCopyReservedRole` → `Status::fail(Validation, "book.copy.numberReserved")` (add that key in all three string tables: Arabic `هذا الرقم محجوز إلى أن يُحفظ الكتاب.`, French `Ce numéro reste réservé jusqu'à l'enregistrement.`, English `This number stays reserved until the book is saved.`). Otherwise `ok()`. `act` calls `m_table->removeRow(row.row)`. Texts: `book.tab.copies`, remove verb/passive, `bulk.noun.copies`. The in-memory table needs no repository refresh; `runBulkAction` returning `> 0` is enough because the rows are already gone. A reserved row that was the only selection and was not ticked still takes today's silent return.
- `FreeLocalNumberDelegate::paint` draws the check indicator (`PE_IndicatorCheckBox` in `SE_ItemViewItemCheckIndicator`) and then the base text, so the number stays editable. A double-click on the number (outside the indicator) still opens the editor. The viewport filter from Task 3 already eats a click on the indicator.

- [ ] **Step 1: Failing tests** in `test_ui_BulkCopies`, using `BookCopiesTable` against a `TestDatabase`:
  - Load a book with three copies. Tick two. Remove + Yes: those two rows are gone, the third remains, `copyInputs().size()` is 1.
  - Tick a copy whose loan is still out and a free copy: the on-loan row stays, the free one goes, and the box quotes `book.copy.cannotRemoveOnLoan`.
  - `addReservedRow`, tick it and a normal row: the reserved row stays.
  - Nothing ticked: Remove deletes only the current row.
  - Double-click the number, not the indicator: an editor opens (`findChild<QLineEdit*>` or `QComboBox`).
  - Render the cell (`show()`, `viewport()->render`) and read pixels: a checked row's indicator band differs from the same cell with the check flag cleared. The sample is the indicator rect from `SE_ItemViewItemCheckIndicator`, not the digits.
- [ ] **Step 2–5:** `ctest --test-dir build -R 'test_ui_BulkCopies|test_ui_FreeNumberPicker' --output-on-failure`, implement, re-run, commit. The new string is part of this task; add it to one table first so `test_core_StringsParity` fails, then to the other two.

---

## Self-review

Spec coverage: row boxes, header tri-state, no new column, ticks vs highlight, Edit/Loans untouched, select-all on screen, clear on refresh, `can…` keys and paired actions, missing → `notFound`, actions call `can…`, `bookHasOpenLoans` / `removalBlock` kept, `runBulkAction` ask/run/summary/cancel/progress, per-page check/act/label, filter refresh, `recordRestored` once, copies delegate paint and blockers, strings in three tables, second-record tests. Reuse, Metrics, and loan-history dialogs are explicitly out. No schema change.
