# Catalog Local Number Column Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended)
> or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax
> for tracking.

**Goal:** Replace the catalog table's ISBN and Language columns with a Local Number column
whose cells are dropdowns over each book's copy numbers.

**Architecture:** The copy numbers ride back on the existing `listBooks` query as one extra
`GROUP_CONCAT` aggregate, landing in a new `BookRecord::localIds` field. The table cell is
painted by a `QStyledItemDelegate` that looks like a combo box; a real `QComboBox` is built
only when a cell is clicked and destroyed when its popup closes, so the per-row cost stays at
one `QTableWidgetItem`.

**Tech Stack:** C++17, Qt 6 Widgets (Core/Gui/Widgets only), SQLite via `SqliteSession`,
GoogleTest, CMake + Unix Makefiles.

**Spec:** `docs/superpowers/specs/2026-09-21-catalog-local-number-column-design.md`

## Global Constraints

- **Core is Qt-free.** Anything under `libraries/Core/` uses `std::string`, `std::vector`,
  `std::int64_t`. No `QString`, no Qt headers.
- **No new Qt modules.** The app links Core/Gui/Widgets only. Do not pull in QtSvg, QtSql,
  or anything else.
- **Repositories return `Result<T>` / `Status`**, with message keys via `Strings::t`, never a
  raw SQL error string.
- **Every user-visible string is a key in `libraries/Core/src/Strings.cpp`**, present in all
  three locale tables (ar, fr, en). Counts and numeric suffixes built inline with
  `QStringLiteral("%1 (%2)")` are the established exception.
- **Branch:** work lands on `Beta`. Do not merge to `main`.
- **Build directory is `build/`** (Debug, Unix Makefiles), already configured.
- **UI tests need** `QT_QPA_PLATFORM=offscreen`.
- **The pager opens on ALL**, so the catalog paints 18,468 rows on load and repaints them on
  every search keystroke. No solution may add a widget per row.
- **`local_id` is unique per source, not globally.** Never treat it as a global key.

## Commands you will use

```bash
# Build one target
cmake --build build --target test_vlms_core -j"$(nproc)"
cmake --build build --target test_vlms_ui -j"$(nproc)"
cmake --build build --target vlms -j"$(nproc)"

# Run one suite
./build/bin/test_vlms_core --gtest_filter='test_core_CatalogRepository.*'
QT_QPA_PLATFORM=offscreen ./build/bin/test_vlms_ui --gtest_filter='test_ui_CatalogSort.*'
```

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `libraries/Core/include/VLMS/Core/CatalogTypes.h` | modify | `BookRecord::localIds` (Task 1); `BookSort::kLocalNumber` (Task 2); drop `kIsbn`, `kLanguage` (Task 5) |
| `libraries/Core/src/CatalogRepository.cpp` | modify | the `GROUP_CONCAT` aggregate and its parse into `localIds` |
| `libraries/Core/src/BookSql.cpp` | modify | the `localNumber` ORDER BY branch (Task 2); drop two dead branches (Task 5) |
| `libraries/Core/src/Strings.cpp` | modify | add `catalog.col.localNumber` ×3 (Task 3); drop `catalog.col.isbn`, `catalog.col.language` ×3 (Task 5) |
| `libraries/Core/test/support/TestSeed.h/.cpp` | modify | `rawSetCopyLocalId` seam |
| `libraries/Core/test/src/test_catalog_repository.cpp` | modify | `localIds` coverage |
| `applications/vlms/src/ui/catalog/LocalNumberDelegate.h/.cpp` | **create** | paint a combo-looking cell; pop a real combo on click |
| `applications/vlms/src/ui/catalog/CatalogPage.cpp` | modify | six columns, new sort keys, delegate install, cell fill |
| `applications/vlms/CMakeLists.txt` | modify | register the delegate sources |
| `applications/vlms/test/src/test_local_number_delegate.cpp` | **create** | delegate behaviour, standalone |
| `applications/vlms/test/src/test_catalog_local_number.cpp` | **create** | the column end to end |
| `applications/vlms/test/CMakeLists.txt` | modify | register both new test files |

## Task dependency graph

```
Task 1 (Core: localIds + seed seam) ──┬──> Task 2 (Core: sort key) ──┐
Task 3 (Strings)  ────────────────────┤                              ├──> Task 5 (CatalogPage) ──> Task 6 (run the app)
Task 4 (LocalNumberDelegate) ─────────┘                              │
                                                                     ┘
```

Tasks 1, 3 and 4 touch disjoint files and may run in parallel. Task 2 edits
`CatalogTypes.h`, which Task 1 also edits, so it waits for Task 1. Task 5 waits for all of
1–4. Task 6 waits for Task 5.

**Tasks 1–4 only add.** Every removal — the two sort keys, the two string keys, the two
columns — happens in Task 5, in one commit. That is deliberate: it keeps `build/` green at
every commit, so a task running in parallel never fails on a neighbour's half-finished work.

---

### Task 1: Core — `BookRecord::localIds`

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/CatalogTypes.h`
- Modify: `libraries/Core/src/CatalogRepository.cpp` (`kBookSelect` ~line 66, `readBookRow` ~line 114)
- Modify: `libraries/Core/test/support/TestSeed.h`, `libraries/Core/test/support/TestSeed.cpp`
- Test: `libraries/Core/test/src/test_catalog_repository.cpp`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces:
  - `BookRecord::localIds` — `std::vector<std::string>`, ascending numerically, empty when
    the book has no copies. Task 5 reads this.
  - `VLMS::Test::rawSetCopyLocalId(const TestDatabase& db, std::int64_t copyId, const std::string& value) -> bool`.
    Task 5's tests use this.

- [ ] **Step 1: Add the field to `BookRecord`**

In `libraries/Core/include/VLMS/Core/CatalogTypes.h`, inside `struct BookRecord`, directly
after `int availableCopies = 0;`:

```cpp
    /// Local accession numbers of this book's copies, ascending numerically.
    /// Empty when the book has no copies in the queried scope.
    std::vector<std::string> localIds;
```

- [ ] **Step 2: Add the test seam to `TestSeed.h`**

In `libraries/Core/test/support/TestSeed.h`, directly after the
`bool rawSetPublicationDate(...)` declaration:

```cpp
bool rawSetCopyLocalId(const TestDatabase& db, std::int64_t copyId, const std::string& value);
```

- [ ] **Step 3: Implement the seam in `TestSeed.cpp`**

In `libraries/Core/test/support/TestSeed.cpp`, directly after the `rawSetPublicationDate`
definition:

```cpp
bool rawSetCopyLocalId(const TestDatabase& db, std::int64_t copyId, const std::string& value)
{
    return db.execBound("UPDATE book_copies SET local_id = :value WHERE id = :id",
                        {{"value", value}, {"id", copyId}});
}
```

- [ ] **Step 4: Write the failing tests**

Append to `libraries/Core/test/src/test_catalog_repository.cpp`:

```cpp
TEST_F(test_core_CatalogRepository, ListBooksReturnsCopyLocalIdsAscendingNumerically)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.initialCopyCount = 3;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const auto copyIds = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copyIds.size(), 3U);
    // Deliberately out of order, and chosen so a lexical sort would give 100, 9, 10.
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIds[0], "100"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIds[1], "9"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIds[2], "10"));

    BookQuery query;
    const auto books = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(books.size(), 1U);
    EXPECT_EQ(books.front().localIds, (std::vector<std::string>{"9", "10", "100"}));
}

TEST_F(test_core_CatalogRepository, ListBooksReturnsNoLocalIdsForABookWithoutCopies)
{
    // createBook rejects initialCopyCount < 1 (rule C11b, "error.book.minCopies"),
    // so a copy-less book cannot be seeded directly. Seed one copy and archive it
    // through saveCopies({}) — the path Catalog Delete uses when the librarian
    // removes the last copy row — which leaves the book live with no live copies.
    BookSeed seed = uniqueBookSeed(2);
    seed.initialCopyCount = 1;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    ASSERT_TRUE(m_repository->saveCopies(bookId, {}));

    BookQuery query;
    const auto books = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(books.size(), 1U);
    EXPECT_TRUE(books.front().localIds.empty());
}
```

- [ ] **Step 5: Run the tests to verify they fail**

```bash
cmake --build build --target test_vlms_core -j"$(nproc)" \
  && ./build/bin/test_vlms_core --gtest_filter='test_core_CatalogRepository.ListBooks*LocalIds*'
```

Expected: both FAIL — `localIds` comes back empty because nothing fills it yet. (The
no-copies test may pass by accident; the ascending one must fail.)

- [ ] **Step 6: Add the aggregate to `kBookSelect`**

In `libraries/Core/src/CatalogRepository.cpp`, in the `kBookSelect` raw string, change the
last line from:

```
            COALESCE(b.archived_at, '') AS archived_at
```

to:

```
            COALESCE(b.archived_at, '') AS archived_at,
            COALESCE(GROUP_CONCAT(DISTINCT bc.local_id), '') AS local_ids
```

It goes last so every existing column index in `readBookRow` is untouched. `DISTINCT` guards
against the `active_loan` join multiplying a copy row.

- [ ] **Step 7: Parse and sort it in `readBookRow`**

In the same file, in the anonymous namespace **above** `readBookRow`, add:

```cpp
/// SQLite will not order a GROUP_CONCAT, so the numbers are sorted here.
/// They are accession counters: compare numerically, falling back to text so a
/// non-numeric legacy value still lands somewhere stable.
std::vector<std::string> splitLocalIds(const std::string& joined)
{
    std::vector<std::string> ids;
    std::size_t start = 0;
    while (start <= joined.size()) {
        const std::size_t comma = joined.find(',', start);
        const std::size_t end = comma == std::string::npos ? joined.size() : comma;
        std::string piece = joined.substr(start, end - start);
        if (!piece.empty()) {
            ids.push_back(std::move(piece));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    std::sort(ids.begin(), ids.end(), [](const std::string& a, const std::string& b) {
        const long long na = std::strtoll(a.c_str(), nullptr, 10);
        const long long nb = std::strtoll(b.c_str(), nullptr, 10);
        if (na != nb) {
            return na < nb;
        }
        return a < b;
    });
    return ids;
}
```

Then in `readBookRow`, directly before `return book;`:

```cpp
    book.localIds = splitLocalIds(query.text(19));
```

- [ ] **Step 8: Add the includes**

At the top of `libraries/Core/src/CatalogRepository.cpp`, ensure these are present among the
existing includes (add only the ones missing):

```cpp
#include <algorithm>
#include <cstdlib>
```

- [ ] **Step 9: Run the tests to verify they pass**

```bash
cmake --build build --target test_vlms_core -j"$(nproc)" \
  && ./build/bin/test_vlms_core --gtest_filter='test_core_CatalogRepository.*'
```

Expected: PASS, including both new tests and every pre-existing one.

- [ ] **Step 10: Commit**

```bash
git add libraries/Core/include/VLMS/Core/CatalogTypes.h \
        libraries/Core/src/CatalogRepository.cpp \
        libraries/Core/test/support/TestSeed.h \
        libraries/Core/test/support/TestSeed.cpp \
        libraries/Core/test/src/test_catalog_repository.cpp
git commit -m "Carry each book's copy local numbers on BookRecord.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: Core — sort by local number

**Depends on:** Task 1 (both edit `CatalogTypes.h`).

This task only **adds**. `BookSort::kIsbn` and `kLanguage` stay until Task 5 removes them
together with their last caller, so the tree compiles at every commit and Tasks 3 and 4 can
keep building against it in parallel.

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/CatalogTypes.h` (`namespace BookSort`)
- Modify: `libraries/Core/src/BookSql.cpp` (`orderExpressions`, ~lines 81–120)
- Test: `libraries/Core/test/src/test_catalog_repository.cpp`

**Interfaces:**
- Consumes: `BookRecord::localIds` from Task 1 (for assertions only).
- Produces: `BookSort::kLocalNumber` — the string `"localNumber"`. Task 5 passes it as a
  column key.

- [ ] **Step 1: Write the failing tests**

Append to `libraries/Core/test/src/test_catalog_repository.cpp`:

```cpp
TEST_F(test_core_CatalogRepository, SortingByLocalNumberOrdersByTheLowestNumber)
{
    BookSeed high = uniqueBookSeed(10);
    high.title = "High";
    high.initialCopyCount = 1;
    const std::int64_t highId = seedBook(*m_db, high);
    ASSERT_GT(highId, 0);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIdsOf(*m_db, highId).front(), "500"));

    BookSeed low = uniqueBookSeed(11);
    low.title = "Low";
    low.initialCopyCount = 2;
    const std::int64_t lowId = seedBook(*m_db, low);
    ASSERT_GT(lowId, 0);
    const auto lowCopies = copyIdsOf(*m_db, lowId);
    ASSERT_EQ(lowCopies.size(), 2U);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, lowCopies[0], "900"));
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, lowCopies[1], "42"));

    BookQuery query;
    query.sortColumn = BookSort::kLocalNumber;
    query.sortAscending = true;
    const auto ascending = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(ascending.size(), 2U);
    // "Low" wins on MIN(42), not on its 900.
    EXPECT_EQ(ascending.front().title, "Low");

    query.sortAscending = false;
    const auto descending = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(descending.size(), 2U);
    EXPECT_EQ(descending.front().title, "High");
}

TEST_F(test_core_CatalogRepository, BooksWithoutCopiesSortLastByLocalNumberInBothDirections)
{
    BookSeed withCopy = uniqueBookSeed(12);
    withCopy.title = "Has a copy";
    withCopy.initialCopyCount = 1;
    const std::int64_t withId = seedBook(*m_db, withCopy);
    ASSERT_GT(withId, 0);
    ASSERT_TRUE(rawSetCopyLocalId(*m_db, copyIdsOf(*m_db, withId).front(), "7"));

    // createBook rejects initialCopyCount < 1 (rule C11b, "error.book.minCopies"),
    // so a copy-less book cannot be seeded directly. Seed one copy and archive it
    // through saveCopies({}) — the path Catalog Delete uses when the librarian
    // removes the last copy row — which leaves the book live with no live copies.
    BookSeed bare = uniqueBookSeed(13);
    bare.title = "No copies";
    bare.initialCopyCount = 1;
    const std::int64_t bareId = seedBook(*m_db, bare);
    ASSERT_GT(bareId, 0);
    ASSERT_TRUE(m_repository->saveCopies(bareId, {}));

    BookQuery query;
    query.sortColumn = BookSort::kLocalNumber;

    query.sortAscending = true;
    const auto ascending = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(ascending.size(), 2U);
    EXPECT_EQ(ascending.back().title, "No copies");

    query.sortAscending = false;
    const auto descending = VLMS_UNWRAP(m_repository->listBooks(query));
    ASSERT_EQ(descending.size(), 2U);
    EXPECT_EQ(descending.back().title, "No copies");
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build --target test_vlms_core -j"$(nproc)" 2>&1 | tail -20
```

Expected: FAIL at compile time — `'kLocalNumber' is not a member of 'BookSort'`.

- [ ] **Step 3: Add the sort key**

In `libraries/Core/include/VLMS/Core/CatalogTypes.h`, inside `namespace BookSort`, add one
line directly after `kCategory`:

```cpp
inline constexpr auto kLocalNumber = "localNumber";
```

Leave `kIsbn` and `kLanguage` alone — Task 5 removes them together with their last caller.

- [ ] **Step 4: Add the ORDER BY branch in `BookSql.cpp`**

In `libraries/Core/src/BookSql.cpp`, in `orderExpressions`, directly after the
`BookSort::kCategory` branch:

```cpp
    if (column == BookSort::kLocalNumber) {
        // Copy-less books are pinned last whichever way the column is sorted; the
        // lowest number is what the cell shows, so it is what the column sorts on.
        return std::string("CASE WHEN COUNT(bc.id) = 0 THEN 1 ELSE 0 END ASC, ")
            + withDirection("MIN(CAST(bc.local_id AS INTEGER))", asc) + ", "
            + withDirection("b.id", asc);
    }
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build --target test_vlms_core -j"$(nproc)" \
  && ./build/bin/test_vlms_core --gtest_filter='test_core_CatalogRepository.*'
```

Expected: PASS, all of them.

- [ ] **Step 6: Confirm the tree still builds whole**

```bash
cmake --build build -j"$(nproc)" 2>&1 | tail -5
grep -rn "BookSort::kIsbn\|BookSort::kLanguage" --include=*.cpp --include=*.h applications/ libraries/
```

Expected: the build succeeds, and the only matches are
`applications/vlms/src/ui/catalog/CatalogPage.cpp` (the column keys) and
`libraries/Core/src/BookSql.cpp` (the two branches) — the pair Task 5 removes together.
Anything else is a surprise: stop and report it.

- [ ] **Step 7: Commit**

```bash
git add libraries/Core/include/VLMS/Core/CatalogTypes.h \
        libraries/Core/src/BookSql.cpp \
        libraries/Core/test/src/test_catalog_repository.cpp
git commit -m "Sort the catalog by lowest local number.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

**Note:** nothing is removed here, so the tree builds and every existing test still passes at
this commit. `kIsbn` and `kLanguage` simply become unreachable for a while.

---

### Task 3: Strings — the column label

**Depends on:** nothing. Runs in parallel with Tasks 1 and 4.

This task only **adds** the new key. `catalog.col.isbn` and `catalog.col.language` stay until
Task 5 removes them together with their last caller — `T()` on a key that no longer exists
falls back to the raw key, so deleting them early would quietly put `catalog.col.isbn` in a
column header.

**Files:**
- Modify: `libraries/Core/src/Strings.cpp` (three locale tables: ar ~line 289, fr ~line 766, en ~line 1255)
- Test: `libraries/Core/test/src/test_strings_parity.cpp` (run only; no edit expected)

**Interfaces:**
- Consumes: nothing.
- Produces: the key `catalog.col.localNumber`. Task 5 calls `T("catalog.col.localNumber")`.

- [ ] **Step 1: Edit the Arabic table**

In `libraries/Core/src/Strings.cpp`, in the Arabic table, directly after
`{"catalog.col.category", "التصنيف"},` (around line 291):

```cpp
        {"catalog.col.localNumber", "الرقم المحلي"},
```

The wording matches the existing `archive.col.localId` and `book.copy.localId`.

- [ ] **Step 2: Edit the French table**

In the French table, directly after `{"catalog.col.category", "Catégorie"},` (around line 768):

```cpp
        {"catalog.col.localNumber", "N° local"},
```

- [ ] **Step 3: Edit the English table**

In the English table, directly after `{"catalog.col.category", "Category"},` (around line 1257):

```cpp
        {"catalog.col.localNumber", "Local no."},
```

- [ ] **Step 4: Confirm the key landed in all three tables**

```bash
grep -c "catalog.col.localNumber" libraries/Core/src/Strings.cpp
```

Expected: `3`. Two means you missed a table and the parity test in the next step will say so.

- [ ] **Step 5: Run the parity test to verify it passes**

```bash
cmake --build build --target test_vlms_core -j"$(nproc)" \
  && ./build/bin/test_vlms_core --gtest_filter='test_core_StringsParity.*'
```

Expected: PASS. A failure here means a key is present in one table and not another — fix the
table you missed, do not weaken the test.

- [ ] **Step 6: Commit**

```bash
git add libraries/Core/src/Strings.cpp
git commit -m "Label the catalog's local-number column.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: `LocalNumberDelegate`

**Depends on:** nothing. New files only; runs in parallel with Tasks 1 and 3.

**Files:**
- Create: `applications/vlms/src/ui/catalog/LocalNumberDelegate.h`
- Create: `applications/vlms/src/ui/catalog/LocalNumberDelegate.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (sources ~line 15, headers ~line 40)
- Create: `applications/vlms/test/src/test_local_number_delegate.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt` (`SRC` list)

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces:
  - `class LocalNumberDelegate : public QStyledItemDelegate`, constructor
    `explicit LocalNumberDelegate(QObject* parent = nullptr)`.
  - `static constexpr int LocalNumberDelegate::kNumbersRole = Qt::UserRole + 1;` — the item
    role holding a `QStringList` of that row's numbers. Task 5 writes this role.
  - `static QString LocalNumberDelegate::cellText(const QStringList& numbers);` — public and
    static so it can be tested without a paint device. Returns `—` for an empty list, the
    bare number for one, and `"%1 (+%2)"` for more.

- [ ] **Step 1: Write the header**

Create `applications/vlms/src/ui/catalog/LocalNumberDelegate.h`:

```cpp
#pragma once

#include <QPointer>
#include <QStringList>
#include <QStyledItemDelegate>

class QComboBox;

/// Paints a catalog cell that looks like a closed combo box over a book's copy
/// local numbers, and opens a real one only when the cell is clicked.
///
/// The catalog list opens on ALL — 18,468 rows — and is repopulated on every
/// search keystroke, so a QComboBox per row is out of the question. The numbers
/// live in the item's data under kNumbersRole; at most one combo widget exists
/// at a time, and picking a number does nothing but close the popup.
class LocalNumberDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    /// Item role holding the row's numbers as a QStringList.
    static constexpr int kNumbersRole = Qt::UserRole + 1;

    explicit LocalNumberDelegate(QObject* parent = nullptr);

    /// What the closed cell reads: an em dash for none, the number itself for
    /// one, and "<lowest> (+<rest>)" for more.
    [[nodiscard]] static QString cellText(const QStringList& numbers);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

protected:
    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

    /// Watches the open popup so the combo dies with it.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] static QStringList numbersOf(const QModelIndex& index);

    /// The one combo that may exist at a time. QPointer so a combo destroyed by
    /// its own deleteLater leaves nothing dangling here.
    QPointer<QComboBox> m_openCombo;
};
```

- [ ] **Step 2: Write the failing tests**

Create `applications/vlms/test/src/test_local_number_delegate.cpp`:

```cpp
#include "ui/catalog/LocalNumberDelegate.h"

#include <QComboBox>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTest>

#include <gtest/gtest.h>

#include <memory>

class test_ui_LocalNumberDelegate : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_table = std::make_unique<QTableWidget>(1, 1);
        m_delegate = std::make_unique<LocalNumberDelegate>();
        m_table->setItemDelegateForColumn(0, m_delegate.get());
        m_table->setItem(0, 0, new QTableWidgetItem);
    }

    void setNumbers(const QStringList& numbers)
    {
        m_table->item(0, 0)->setData(LocalNumberDelegate::kNumbersRole, numbers);
    }

    std::unique_ptr<QTableWidget> m_table;
    std::unique_ptr<LocalNumberDelegate> m_delegate;
};

TEST_F(test_ui_LocalNumberDelegate, NoNumbersReadAsADash)
{
    EXPECT_EQ(LocalNumberDelegate::cellText({}), QString::fromUtf8("—"));
}

TEST_F(test_ui_LocalNumberDelegate, OneNumberReadsAsItself)
{
    EXPECT_EQ(LocalNumberDelegate::cellText({QStringLiteral("1042")}),
              QStringLiteral("1042"));
}

TEST_F(test_ui_LocalNumberDelegate, SeveralNumbersReadAsTheFirstAndTheRestAsACount)
{
    const QStringList numbers{QStringLiteral("1042"), QStringLiteral("1043"),
                              QStringLiteral("1044")};
    EXPECT_EQ(LocalNumberDelegate::cellText(numbers), QStringLiteral("1042 (+2)"));
}

TEST_F(test_ui_LocalNumberDelegate, ClickingACellWithSeveralNumbersOpensOneCombo)
{
    setNumbers({QStringLiteral("10"), QStringLiteral("11")});
    EXPECT_EQ(m_table->viewport()->findChildren<QComboBox*>().size(), 0);

    const QRect cell = m_table->visualItemRect(m_table->item(0, 0));
    QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, cell.center());

    const auto combos = m_table->viewport()->findChildren<QComboBox*>();
    ASSERT_EQ(combos.size(), 1);
    EXPECT_EQ(combos.front()->count(), 2);
    EXPECT_EQ(combos.front()->itemText(0), QStringLiteral("10"));
    EXPECT_EQ(combos.front()->itemText(1), QStringLiteral("11"));
}

TEST_F(test_ui_LocalNumberDelegate, ClickingACellWithOneNumberOpensNothing)
{
    setNumbers({QStringLiteral("10")});
    const QRect cell = m_table->visualItemRect(m_table->item(0, 0));
    QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, cell.center());
    EXPECT_EQ(m_table->viewport()->findChildren<QComboBox*>().size(), 0);
}

TEST_F(test_ui_LocalNumberDelegate, ClickingACellWithNoNumbersOpensNothing)
{
    setNumbers({});
    const QRect cell = m_table->visualItemRect(m_table->item(0, 0));
    QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, cell.center());
    EXPECT_EQ(m_table->viewport()->findChildren<QComboBox*>().size(), 0);
}

TEST_F(test_ui_LocalNumberDelegate, PickingANumberChangesNothingInTheModel)
{
    setNumbers({QStringLiteral("10"), QStringLiteral("11")});
    const QRect cell = m_table->visualItemRect(m_table->item(0, 0));
    QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, cell.center());

    auto* combo = m_table->viewport()->findChild<QComboBox*>();
    ASSERT_NE(combo, nullptr);
    combo->setCurrentIndex(1);

    // Browse only: the row's data is exactly what was put there.
    EXPECT_EQ(m_table->item(0, 0)->data(LocalNumberDelegate::kNumbersRole).toStringList(),
              (QStringList{QStringLiteral("10"), QStringLiteral("11")}));
    EXPECT_TRUE(m_table->item(0, 0)->text().isEmpty());
}
```

- [ ] **Step 3: Register both new files with CMake**

In `applications/vlms/CMakeLists.txt`, add to the sources list after
`src/ui/catalog/CategoryManagerDialog.cpp`:

```cmake
    src/ui/catalog/LocalNumberDelegate.cpp
```

and to the headers list after `src/ui/catalog/CategoryManagerDialog.h`:

```cmake
    src/ui/catalog/LocalNumberDelegate.h
```

In `applications/vlms/test/CMakeLists.txt`, add to the `SRC` list after
`src/test_archive_circulation_ui.cpp`:

```cmake
        src/test_local_number_delegate.cpp
```

- [ ] **Step 4: Run the tests to verify they fail**

```bash
cmake --build build --target test_vlms_ui -j"$(nproc)" 2>&1 | tail -20
```

Expected: FAIL at link time — `undefined reference to LocalNumberDelegate::LocalNumberDelegate`
and friends, because only the header exists.

- [ ] **Step 5: Write the implementation**

Create `applications/vlms/src/ui/catalog/LocalNumberDelegate.cpp`:

```cpp
#include "ui/catalog/LocalNumberDelegate.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionComboBox>
#include <QStyleOptionViewItem>
#include <QWidget>

namespace {

/// Shown when a book has no copies at all. 1,749 of the live catalog's rows.
const QString kNoNumbers = QString::fromUtf8("—");

}  // namespace

LocalNumberDelegate::LocalNumberDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QStringList LocalNumberDelegate::numbersOf(const QModelIndex& index)
{
    return index.data(kNumbersRole).toStringList();
}

QString LocalNumberDelegate::cellText(const QStringList& numbers)
{
    if (numbers.isEmpty()) {
        return kNoNumbers;
    }
    if (numbers.size() == 1) {
        return numbers.first();
    }
    // Same untranslated "%1 (%2)" shape the filter-list counts already use.
    return QStringLiteral("%1 (+%2)").arg(numbers.first()).arg(numbers.size() - 1);
}

void LocalNumberDelegate::paint(QPainter* painter,
                                const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    const QStringList numbers = numbersOf(index);

    QStyleOptionViewItem background = option;
    initStyleOption(&background, index);
    background.text.clear();
    QStyle* style = background.widget != nullptr ? background.widget->style()
                                                 : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &background, painter, background.widget);

    if (numbers.size() < 2) {
        // One number or none: plain text, no frame, no arrow — there is nothing
        // to drop down to.
        painter->save();
        painter->setPen(option.palette.color(option.state.testFlag(QStyle::State_Selected)
                                                 ? QPalette::HighlightedText
                                                 : QPalette::Text));
        painter->drawText(option.rect.adjusted(4, 0, -4, 0),
                          Qt::AlignVCenter | Qt::AlignLeading,
                          cellText(numbers));
        painter->restore();
        return;
    }

    QStyleOptionComboBox combo;
    combo.rect = option.rect.adjusted(2, 2, -2, -2);
    combo.state = QStyle::State_Enabled | QStyle::State_Active;
    combo.currentText = cellText(numbers);
    combo.editable = false;
    combo.frame = true;
    combo.palette = option.palette;
    combo.direction = option.direction;
    combo.fontMetrics = option.fontMetrics;

    style->drawComplexControl(QStyle::CC_ComboBox, &combo, painter, background.widget);
    style->drawControl(QStyle::CE_ComboBoxLabel, &combo, painter, background.widget);
}

QSize LocalNumberDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    size.setWidth(option.fontMetrics.horizontalAdvance(cellText(numbersOf(index))) + 36);
    return size;
}

bool LocalNumberDelegate::editorEvent(QEvent* event,
                                      QAbstractItemModel* model,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index)
{
    if (event->type() != QEvent::MouseButtonRelease) {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    const QStringList numbers = numbersOf(index);
    if (numbers.size() < 2) {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != Qt::LeftButton || !option.rect.contains(mouse->position().toPoint())) {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    auto* view = qobject_cast<QAbstractItemView*>(const_cast<QWidget*>(option.widget));
    QWidget* host = view != nullptr ? view->viewport() : const_cast<QWidget*>(option.widget);
    if (host == nullptr) {
        return true;
    }

    // One combo at a time, alive only while its popup is. Nothing it does is
    // written back: the column is a viewer over numbers the book editor owns.
    if (!m_openCombo.isNull()) {
        m_openCombo->deleteLater();
    }
    auto* combo = new QComboBox(host);
    combo->addItems(numbers);
    combo->setGeometry(option.rect);
    combo->show();
    m_openCombo = combo;
    // The popup is a separate window; when it hides, the combo has done its job.
    combo->view()->window()->installEventFilter(this);
    combo->showPopup();
    return true;
}

bool LocalNumberDelegate::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Hide && !m_openCombo.isNull()) {
        m_openCombo->deleteLater();
        m_openCombo.clear();
    }
    return QStyledItemDelegate::eventFilter(watched, event);
}
```

- [ ] **Step 6: Check the includes**

`LocalNumberDelegate.cpp` uses `QApplication::style()` and `QPointer`. Its include block
should read:

```cpp
#include "ui/catalog/LocalNumberDelegate.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionComboBox>
#include <QStyleOptionViewItem>
#include <QWidget>
```

- [ ] **Step 7: Run the tests to verify they pass**

```bash
cmake --build build --target test_vlms_ui -j"$(nproc)" \
  && QT_QPA_PLATFORM=offscreen ./build/bin/test_vlms_ui \
       --gtest_filter='test_ui_LocalNumberDelegate.*'
```

Expected: PASS, all seven.

The combo is deleted with `deleteLater`, so it is still findable immediately after the click —
the tests never spin an event loop in between. Do not change that to a plain `delete`: the
popup is still delivering events when the hide arrives.

If `ClickingACellWithSeveralNumbersOpensOneCombo` fails because the popup closes immediately
under `offscreen`, the combo is still a child of the viewport and findable — check that
`combo->show()` runs before `showPopup()`. Do not weaken the assertion to "zero or one".

- [ ] **Step 8: Commit**

```bash
git add applications/vlms/src/ui/catalog/LocalNumberDelegate.h \
        applications/vlms/src/ui/catalog/LocalNumberDelegate.cpp \
        applications/vlms/CMakeLists.txt \
        applications/vlms/test/src/test_local_number_delegate.cpp \
        applications/vlms/test/CMakeLists.txt
git commit -m "Add a delegate that drops down a book's local numbers.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Wire the column into `CatalogPage`

**Depends on:** Tasks 1, 2, 3 and 4. This is the task that makes `test_vlms_ui` compile
again.

This is the only task that **removes** anything. Tasks 1–4 were additive so the tree stayed
green while they ran in parallel; here the ISBN and Language columns and everything that
existed only to serve them go in one commit, so no commit ever leaves a dangling reference.

**Files:**
- Modify: `applications/vlms/src/ui/catalog/CatalogPage.cpp`
  (constants ~line 37, `buildUi` ~lines 96–155, `retranslateUi` ~line 211, `refreshBooks` ~line 430)
- Modify: `libraries/Core/include/VLMS/Core/CatalogTypes.h` (drop `BookSort::kIsbn`, `kLanguage`)
- Modify: `libraries/Core/src/BookSql.cpp` (drop the two matching `orderExpressions` branches)
- Modify: `libraries/Core/src/Strings.cpp` (drop `catalog.col.isbn`, `catalog.col.language` ×3)
- Create: `applications/vlms/test/src/test_catalog_local_number.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt` (`SRC` list)
- Modify: `applications/vlms/test/src/test_catalog_sort.cpp` (only if it asserts a moved column index)

**Interfaces:**
- Consumes: `BookRecord::localIds` (Task 1), `BookSort::kLocalNumber` (Task 2),
  `catalog.col.localNumber` (Task 3), `LocalNumberDelegate` with `kNumbersRole` (Task 4),
  `rawSetCopyLocalId` (Task 1).
- Produces: the finished six-column table. `BookSort::kIsbn`, `BookSort::kLanguage`,
  `catalog.col.isbn` and `catalog.col.language` no longer exist. Nothing depends on this but
  Task 6.

- [ ] **Step 1: Write the failing tests**

Create `applications/vlms/test/src/test_catalog_local_number.cpp`:

```cpp
#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/catalog/CatalogPage.h"
#include "ui/catalog/LocalNumberDelegate.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/Locale.h>

#include <QHeaderView>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <gtest/gtest.h>

#include <memory>

using VLMS::Locale;
using namespace VLMS::Test;

namespace {
constexpr int kLocalNumberColumn = 3;
}  // namespace

class test_ui_CatalogLocalNumber : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_page.reset();
        m_catalog.reset();
        m_db.reset();
    }

    QTableWidget* openPage()
    {
        m_page = std::make_unique<CatalogPage>(*m_catalog);
        auto* table = m_page->findChild<QTableWidget*>();
        return table;
    }

    /// Seeds one book whose copies carry exactly `numbers`, in that order.
    ///
    /// An empty `numbers` means a book with no live copies. createBook rejects
    /// initialCopyCount < 1 (rule C11b, "error.book.minCopies"), so that case is
    /// reached by seeding one copy and archiving it through saveCopies({}) — the
    /// path Catalog Delete uses when the librarian removes the last copy row.
    std::int64_t seedWithNumbers(int index,
                                 const std::string& title,
                                 const std::vector<std::string>& numbers)
    {
        BookSeed seed = uniqueBookSeed(index);
        seed.title = title;
        seed.initialCopyCount = numbers.empty() ? 1 : static_cast<int>(numbers.size());
        const std::int64_t bookId = seedBook(*m_db, seed);
        if (bookId <= 0) {
            return 0;
        }
        if (numbers.empty()) {
            return m_catalog->saveCopies(bookId, {}) ? bookId : 0;
        }
        const auto copyIds = copyIdsOf(*m_db, bookId);
        if (copyIds.size() != numbers.size()) {
            return 0;
        }
        for (std::size_t i = 0; i < numbers.size(); ++i) {
            if (!rawSetCopyLocalId(*m_db, copyIds[i], numbers[i])) {
                return 0;
            }
        }
        return bookId;
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<CatalogPage> m_page;
};

TEST_F(test_ui_CatalogLocalNumber, ColumnsStartWithTitleAuthorCategoryLocalNumber)
{
    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->columnCount(), 6);
    EXPECT_EQ(table->horizontalHeaderItem(0)->text(), QStringLiteral("Title"));
    EXPECT_EQ(table->horizontalHeaderItem(1)->text(), QStringLiteral("Author"));
    EXPECT_EQ(table->horizontalHeaderItem(2)->text(), QStringLiteral("Category"));
    EXPECT_EQ(table->horizontalHeaderItem(3)->text(), QStringLiteral("Local no."));
    EXPECT_EQ(table->horizontalHeaderItem(4)->text(), QStringLiteral("Copies"));
    EXPECT_EQ(table->horizontalHeaderItem(5)->text(), QStringLiteral("Available"));

    for (int column = 0; column < table->columnCount(); ++column) {
        const QString header = table->horizontalHeaderItem(column)->text();
        EXPECT_NE(header, QStringLiteral("ISBN"));
        EXPECT_NE(header, QStringLiteral("Language"));
    }
}

TEST_F(test_ui_CatalogLocalNumber, ABookWithNoCopiesShowsNoNumbers)
{
    ASSERT_GT(seedWithNumbers(1, "Bare", {}), 0);
    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 1);

    const QStringList numbers =
        table->item(0, kLocalNumberColumn)->data(LocalNumberDelegate::kNumbersRole).toStringList();
    EXPECT_TRUE(numbers.isEmpty());
    EXPECT_EQ(LocalNumberDelegate::cellText(numbers), QString::fromUtf8("—"));
}

TEST_F(test_ui_CatalogLocalNumber, ASingleCopyShowsItsNumberAlone)
{
    ASSERT_GT(seedWithNumbers(2, "Alone", {"1042"}), 0);
    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 1);

    const QStringList numbers =
        table->item(0, kLocalNumberColumn)->data(LocalNumberDelegate::kNumbersRole).toStringList();
    EXPECT_EQ(numbers, (QStringList{QStringLiteral("1042")}));
    EXPECT_EQ(LocalNumberDelegate::cellText(numbers), QStringLiteral("1042"));
}

TEST_F(test_ui_CatalogLocalNumber, SeveralCopiesShowTheLowestNumberAndTheCount)
{
    // Out of order, and chosen so a lexical sort would read 1040, 104, 99.
    ASSERT_GT(seedWithNumbers(3, "Many", {"1040", "99", "104"}), 0);
    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 1);

    const QStringList numbers =
        table->item(0, kLocalNumberColumn)->data(LocalNumberDelegate::kNumbersRole).toStringList();
    EXPECT_EQ(numbers, (QStringList{QStringLiteral("99"), QStringLiteral("104"),
                                    QStringLiteral("1040")}));
    EXPECT_EQ(LocalNumberDelegate::cellText(numbers), QStringLiteral("99 (+2)"));
}

TEST_F(test_ui_CatalogLocalNumber, ClickingTheLocalNumberHeaderSortsByTheLowestNumber)
{
    ASSERT_GT(seedWithNumbers(4, "High", {"500"}), 0);
    ASSERT_GT(seedWithNumbers(5, "Low", {"900", "42"}), 0);

    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 2);

    emit table->horizontalHeader()->sectionClicked(kLocalNumberColumn);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("Low"));

    emit table->horizontalHeader()->sectionClicked(kLocalNumberColumn);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("High"));
}

TEST_F(test_ui_CatalogLocalNumber, BooksWithoutCopiesSortLastInBothDirections)
{
    ASSERT_GT(seedWithNumbers(6, "Has a copy", {"7"}), 0);
    ASSERT_GT(seedWithNumbers(7, "No copies", {}), 0);

    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 2);

    emit table->horizontalHeader()->sectionClicked(kLocalNumberColumn);
    EXPECT_EQ(table->item(1, 0)->text(), QStringLiteral("No copies"));

    emit table->horizontalHeader()->sectionClicked(kLocalNumberColumn);
    EXPECT_EQ(table->item(1, 0)->text(), QStringLiteral("No copies"));
}
```

Register it in `applications/vlms/test/CMakeLists.txt`, in the `SRC` list after
`src/test_local_number_delegate.cpp`:

```cmake
        src/test_catalog_local_number.cpp
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build --target test_vlms_ui -j"$(nproc)" \
  && QT_QPA_PLATFORM=offscreen ./build/bin/test_vlms_ui \
       --gtest_filter='test_ui_CatalogLocalNumber.*'
```

Expected: the build succeeds and every one of the six FAILs —
`ColumnsStartWithTitleAuthorCategoryLocalNumber` on `columnCount() == 7`, the rest on a null
item at column 3, because the table is still the old seven-column layout.

- [ ] **Step 3: Include the delegate and drop the dead constant**

At the top of `applications/vlms/src/ui/catalog/CatalogPage.cpp`, add next to the other
catalog includes:

```cpp
#include "ui/catalog/LocalNumberDelegate.h"
```

In the anonymous namespace, delete this line:

```cpp
constexpr int kIsbnColumnWidth = 150;
```

and add next to the remaining width constants:

```cpp
constexpr int kLocalNumberColumnWidth = 110;
constexpr int kLocalNumberColumn = 3;
```

- [ ] **Step 4: Narrow the table to six columns**

In `buildUi`, replace:

```cpp
    config.tableColumnCount = 7;
    config.tableColumnWidths = {
        kTitleColumnWidth, kAuthorColumnWidth, 130, kIsbnColumnWidth, 90, 70, 70};
```

with:

```cpp
    config.tableColumnCount = 6;
    config.tableColumnWidths = {
        kTitleColumnWidth, kAuthorColumnWidth, 130, kLocalNumberColumnWidth, 70, 70};
```

- [ ] **Step 5: Install the delegate and fix the sort keys**

Still in `buildUi`, directly after `m_booksTable = frame->table();`:

```cpp
    m_booksTable->setItemDelegateForColumn(kLocalNumberColumn,
                                           new LocalNumberDelegate(m_booksTable));
```

Then replace the `m_sort->setColumnKeys({...})` call with:

```cpp
    m_sort->setColumnKeys({
        QString::fromLatin1(BookSort::kTitle),
        QString::fromLatin1(BookSort::kAuthor),
        QString::fromLatin1(BookSort::kCategory),
        QString::fromLatin1(BookSort::kLocalNumber),
        QString::fromLatin1(BookSort::kCopies),
        QString::fromLatin1(BookSort::kAvailable),
    });
```

- [ ] **Step 6: Relabel the headers**

In `retranslateUi`, replace the `setHorizontalHeaderLabels` call with:

```cpp
    m_booksTable->setHorizontalHeaderLabels({
        T("catalog.col.title"),
        T("catalog.col.author"),
        T("catalog.col.category"),
        T("catalog.col.localNumber"),
        T("catalog.col.copies"),
        T("catalog.col.available"),
    });
```

- [ ] **Step 7: Fill the new cell**

In `refreshBooks`, replace the block from `m_booksTable->setItem(row, 1, ...)` through
`m_booksTable->setItem(row, 6, ...)` with:

```cpp
        m_booksTable->setItem(row, 1, new QTableWidgetItem(qs(book.authorName)));
        m_booksTable->setItem(row, 2, new QTableWidgetItem(qs(book.categoryLabel)));

        // The numbers only, no text: LocalNumberDelegate paints the cell and
        // opens the popup. One item per row, same cost as any other column.
        auto* localItem = new QTableWidgetItem;
        QStringList localNumbers;
        localNumbers.reserve(static_cast<int>(book.localIds.size()));
        for (const std::string& localId : book.localIds) {
            localNumbers.append(qs(localId));
        }
        localItem->setData(LocalNumberDelegate::kNumbersRole, localNumbers);
        m_booksTable->setItem(row, kLocalNumberColumn, localItem);

        m_booksTable->setItem(row, 4, new QTableWidgetItem(QString::number(book.totalCopies)));
        m_booksTable->setItem(row, 5, new QTableWidgetItem(QString::number(book.availableCopies)));
```

Add `#include <QStringList>` to the includes at the top if it is not already there.

- [ ] **Step 8: Run the new tests to verify they pass**

```bash
cmake --build build --target test_vlms_ui -j"$(nproc)" \
  && QT_QPA_PLATFORM=offscreen ./build/bin/test_vlms_ui \
       --gtest_filter='test_ui_CatalogLocalNumber.*'
```

Expected: PASS, all six.

- [ ] **Step 9: Remove the two dead sort keys**

`CatalogPage.cpp` no longer names them, so they are now unreachable. In
`libraries/Core/include/VLMS/Core/CatalogTypes.h`, delete these two lines from
`namespace BookSort`:

```cpp
inline constexpr auto kIsbn = "isbn";
inline constexpr auto kLanguage = "language";
```

In `libraries/Core/src/BookSql.cpp`, delete the two matching branches from
`orderExpressions`:

```cpp
    if (column == BookSort::kIsbn) {
        return withDirection("COALESCE(b.isbn, '') COLLATE NOCASE", asc) + ", "
            + withDirection("b.id", asc);
    }
    if (column == BookSort::kLanguage) {
        return withDirection("b.language COLLATE NOCASE", asc) + ", " + withDirection("b.id", asc);
    }
```

Confirm nothing is left:

```bash
grep -rn "BookSort::kIsbn\|BookSort::kLanguage" --include=*.cpp --include=*.h applications/ libraries/
```

Expected: no output.

- [ ] **Step 10: Remove the two dead string keys**

In `libraries/Core/src/Strings.cpp`, delete these lines from **all three** locale tables:

```cpp
        {"catalog.col.isbn", ...},
        {"catalog.col.language", ...},
```

Confirm they went together:

```bash
grep -c "catalog.col.isbn\|catalog.col.language" libraries/Core/src/Strings.cpp
```

Expected: `0`. `book.field.isbn` and `book.field.language` are a different pair and must stay
— the details panel uses them.

- [ ] **Step 11: Run every UI and Core test**

```bash
cmake --build build --target test_vlms_ui test_vlms_core -j"$(nproc)" \
  && ./build/bin/test_vlms_core \
  && QT_QPA_PLATFORM=offscreen ./build/bin/test_vlms_ui
```

Expected: PASS everywhere. If `test_ui_CatalogSort` or `test_ui_ListPageFrame` fails on a
column index or count, update the *test's* expectation to the new six-column layout — the
layout change is the point. If any other suite fails, stop and report it.

- [ ] **Step 12: Commit**

```bash
git add applications/vlms/src/ui/catalog/CatalogPage.cpp \
        libraries/Core/include/VLMS/Core/CatalogTypes.h \
        libraries/Core/src/BookSql.cpp \
        libraries/Core/src/Strings.cpp \
        applications/vlms/test/src/test_catalog_local_number.cpp \
        applications/vlms/test/CMakeLists.txt \
        applications/vlms/test/src/test_catalog_sort.cpp
git commit -m "Lead the catalog with the local number, drop ISBN and language.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Run the real application

**Depends on:** Task 5.

Green tests do not close this task. The catalog page has to be seen.

**Files:**
- Modify: `CLAUDE.md` (Session log — newest entry at the top of the list)

**Interfaces:**
- Consumes: the finished page from Task 5.
- Produces: nothing in code.

- [ ] **Step 1: Build the application**

```bash
cmake --build build --target vlms -j"$(nproc)"
```

Expected: `Built target vlms`, no warnings introduced by this work.

- [ ] **Step 2: Launch it and screenshot the catalog page**

Follow the project's screenshot recipe: run under **xcb, not Wayland**, capture by window id,
and keep the process in the **foreground** — it dies otherwise.

```bash
QT_QPA_PLATFORM=xcb ./build/bin/vlms
```

Capture the catalog page showing:
1. the six columns in the new order,
2. a row with one copy (bare number),
3. a row with several copies (`N (+n)`),
4. a row with no copies (the dash),
5. one dropdown open over a multi-copy row.

- [ ] **Step 3: Check the column under Arabic**

Switch the language to Arabic with the flag buttons in the header and confirm the Local
Number cell mirrors: the arrow moves to the leading edge and the header reads
`الرقم المحلي`. Screenshot that too.

- [ ] **Step 4: Check the list still arrives fast**

The catalogue's 18,468 rows were on screen about two seconds after launch before this change.
Confirm that still holds — if the page now takes noticeably longer, the delegate is doing
per-row work it should not and the task is not done.

- [ ] **Step 5: Add the session-log entry**

At the **top** of the Session log list in `CLAUDE.md`:

```markdown
- 2026-09-21 — Catalog table is Title | Author | Category | Local Number | Copies | Available; ISBN and language are gone from the list (still in the details panel, the language filter, and search), and `BookSort::kIsbn`/`kLanguage` with them. Copy numbers ride back on `listBooks` as one `GROUP_CONCAT` into `BookRecord::localIds`, sorted numerically in `readBookRow`. The cell is painted by `LocalNumberDelegate` — a real `QComboBox` is built only on click and dies with its popup, because the pager opens on ALL and 2,360 live books have more than one copy. Sorting uses `MIN(CAST(local_id AS INTEGER))` with copy-less books pinned last both ways. Spec: `docs/superpowers/specs/2026-09-21-catalog-local-number-column-design.md`.
```

- [ ] **Step 6: Commit**

```bash
git add CLAUDE.md
git commit -m "Note the catalog local-number column in the session log.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```
