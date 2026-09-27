# Catalogue Loans Button Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move `Categories…` off the Catalogue button pad into the book dialog's Category
row, and give the freed slot to a `Loans` button that opens the selected book's loan
history.

**Architecture:** `LoanQuery` gains a `bookId` that `LoanSql::filterClause` turns into
`AND bc.book_id = :book_id` — every loan query already joins `book_copies bc`, so no new
join. A new `BookLoansDialog` mirrors `MemberLoansDialog` with the member's name in place
of the title column. `CatalogPage` takes a second repository reference, exactly as
`MembersPage` does.

**Tech Stack:** C++17, Qt 6 Widgets, SQLite via `SqliteSession`, GoogleTest through
`ctest`.

## Global Constraints

- **Judge Core tests by `ctest`, never by running the binary.** The schema path, data
  directory and `TZ` come from per-test `ENVIRONMENT` in `cmake/TestUtils.cmake`. Running
  `./build/bin/test_vlms_core` directly fails 16 tests that pass under `ctest`.
- **Build directory is `build/`**, already configured. Build with
  `cmake --build build -j$(nproc)`.
- **British English in prose and comments** — "catalogue", "colour", "localise". Never in
  Qt API names or identifiers, which stay as Qt spells them.
- **Every user-visible string is a key in all three tables** in
  `libraries/Core/src/Strings.cpp` (Arabic, French, English, in that file order).
  `test_core_StringsParity` fails otherwise.
- **The app direction is RTL in Arabic.** Never position a widget by absolute side; let
  the layout mirror.
- Commit after every task, with the trailer
  `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

## File Structure

| File | Responsibility |
|---|---|
| `libraries/Core/include/VLMS/Core/LoanTypes.h` | `LoanQuery::bookId` field |
| `libraries/Core/src/LoanSql.cpp` | the `bc.book_id` clause and its bind |
| `libraries/Core/test/src/test_loan_book_filter.cpp` | new; the filter's Core test |
| `applications/vlms/src/ui/catalog/BookLoansDialog.{h,cpp}` | new; the history dialog |
| `applications/vlms/src/ui/catalog/CatalogPage.{h,cpp}` | `Loans` button in, `Categories…` out |
| `applications/vlms/src/ui/catalog/BookEditorDialog.{h,cpp}` | `Categories…` on the Category row |
| `applications/vlms/src/ui/MainWindow.cpp` | passes `app->circulation()` to `CatalogPage` |
| `libraries/Core/src/Strings.cpp` | `catalog.loans`, `catalog.loanHistoryTitle` |
| `applications/vlms/test/src/test_catalog_loans.cpp` | new; the UI test |

---

### Task 1: `LoanQuery::bookId` filters loans to one book

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/LoanTypes.h:33-42`
- Modify: `libraries/Core/src/LoanSql.cpp:69-71` and `:86-92`
- Create: `libraries/Core/test/src/test_loan_book_filter.cpp`
- Modify: `libraries/Core/test/CMakeLists.txt` (add the new source to `TST_SOURCES`)

**Interfaces:**
- Consumes: nothing.
- Produces: `LoanQuery::bookId` (`std::int64_t`, default `0`, `0` means "every book").
  Tasks 3 and 4 set it on a `LoanQuery` and pass it to
  `CirculationRepository::listLoans` / `countLoans`.

- [ ] **Step 1: Write the failing test**

Create `libraries/Core/test/src/test_loan_book_filter.cpp`:

```cpp
#include "TestDatabase.h"
#include "TestSeed.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/LoanTypes.h>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS::Test;

namespace {

class test_core_LoanBookFilter : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isOpen());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());

        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_GT(m_memberId, 0);

        BookSeed first = uniqueBookSeed(1);
        first.initialCopyCount = 2;
        m_firstBookId = seedBook(*m_db, first);
        ASSERT_GT(m_firstBookId, 0);

        BookSeed second = uniqueBookSeed(2);
        second.initialCopyCount = 1;
        m_secondBookId = seedBook(*m_db, second);
        ASSERT_GT(m_secondBookId, 0);

        const auto firstCopies = copyIdsOf(*m_db, m_firstBookId);
        ASSERT_EQ(firstCopies.size(), 2U);
        const auto secondCopies = copyIdsOf(*m_db, m_secondBookId);
        ASSERT_EQ(secondCopies.size(), 1U);

        // Two loans of the first book, on two different copies, one returned.
        ASSERT_GT(rawInsertLoan(*m_db, m_memberId, firstCopies.at(0),
                                "2025-01-10", "2025-01-24", "2025-01-20"), 0);
        ASSERT_GT(rawInsertLoan(*m_db, m_memberId, firstCopies.at(1),
                                "2025-02-10", "2025-02-24"), 0);
        // One loan of the second book, which must never show up.
        ASSERT_GT(rawInsertLoan(*m_db, m_memberId, secondCopies.at(0),
                                "2025-03-10", "2025-03-24"), 0);
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::int64_t m_memberId = 0;
    std::int64_t m_firstBookId = 0;
    std::int64_t m_secondBookId = 0;
};

TEST_F(test_core_LoanBookFilter, ListingByBookReturnsEveryCopysLoanAndNoOthers)
{
    LoanQuery query;
    query.bookId = m_firstBookId;
    query.archive = ArchiveScope::Any;

    const auto loans = m_circulation->listLoans(query);
    ASSERT_TRUE(loans.has_value());
    EXPECT_EQ(loans.value().size(), 2U);
    for (const LoanRecord& loan : loans.value()) {
        EXPECT_NE(loan.borrowedAt, "2025-03-10") << "a loan of another book leaked in";
    }
}

TEST_F(test_core_LoanBookFilter, CountingByBookCountsOnlyThatBook)
{
    LoanQuery query;
    query.bookId = m_secondBookId;
    query.archive = ArchiveScope::Any;

    const auto count = m_circulation->countLoans(query);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 1);
}

TEST_F(test_core_LoanBookFilter, ABookWithNoLoansCountsZero)
{
    BookSeed untouched = uniqueBookSeed(3);
    const std::int64_t bookId = seedBook(*m_db, untouched);
    ASSERT_GT(bookId, 0);

    LoanQuery query;
    query.bookId = bookId;
    query.archive = ArchiveScope::Any;

    const auto count = m_circulation->countLoans(query);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 0);
}

TEST_F(test_core_LoanBookFilter, AnArchivedCopysLoanIsStillPartOfTheHistory)
{
    // Archive the first book's second copy by submitting only the first: the
    // copy leaves the live list, but the loan it once carried still happened.
    CatalogRepository catalog(m_db->session());
    const auto before = catalog.listCopies(m_firstBookId);
    ASSERT_TRUE(before.has_value());
    ASSERT_EQ(before.value().size(), 2U);

    BookCopyInput survivor;
    survivor.id = before.value().at(0).id;
    survivor.source = before.value().at(0).source;
    survivor.localId = before.value().at(0).localId;
    survivor.globalCopyId = before.value().at(0).globalCopyId;
    const auto saved = catalog.saveCopies(m_firstBookId, {survivor});
    ASSERT_TRUE(saved) << saved.error().key;

    LoanQuery query;
    query.bookId = m_firstBookId;
    query.archive = ArchiveScope::Any;

    const auto count = m_circulation->countLoans(query);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 2) << "archiving a copy erased its loan from the history";
}

TEST_F(test_core_LoanBookFilter, LeavingTheBookUnsetChangesNothing)
{
    LoanQuery query;
    query.archive = ArchiveScope::Any;

    const auto count = m_circulation->countLoans(query);
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(count.value(), 3);
}

}  // namespace
```

Register it — in `libraries/Core/test/CMakeLists.txt`, add to `TST_SOURCES` after
`src/test_circulation_dates.cpp`:

```cmake
    src/test_loan_book_filter.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: a **compile** failure — `'struct LoanQuery' has no member named 'bookId'`. That
is the failure this step wants; the field does not exist yet.

- [ ] **Step 3: Add the field**

In `libraries/Core/include/VLMS/Core/LoanTypes.h`, inside `struct LoanQuery`, directly
after the `memberId` line:

```cpp
    std::int64_t memberId = 0;
    std::int64_t bookId = 0;
```

- [ ] **Step 4: Run again to see the real failure**

```bash
cmake --build build -j$(nproc) >/dev/null 2>&1 && ctest --test-dir build -R test_core_LoanBookFilter --output-on-failure 2>&1 | tail -20
```

Expected: it compiles, and `ListingByBookReturnsEveryCopysLoanAndNoOthers` FAILS with
`Which is: 3` against 2 — the field exists but no SQL reads it.

- [ ] **Step 5: Write the clause**

In `libraries/Core/src/LoanSql.cpp`, in `filterClause`, directly after the `memberId`
block:

```cpp
    if (query.memberId > 0) {
        sql += " AND l.member_id = :member_id ";
    }

    // Every loan query joins book_copies, so one book's history reaches all of
    // its copies — including copies since archived, which still lent the book.
    if (query.bookId > 0) {
        sql += " AND bc.book_id = :book_id ";
    }
```

and in `bindFilters`, directly after the `memberId` bind:

```cpp
    if (queryData.bookId > 0) {
        query.bind(":book_id", queryData.bookId);
    }
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build build -j$(nproc) >/dev/null 2>&1 && ctest --test-dir build -R test_core_LoanBookFilter --output-on-failure 2>&1 | tail -10
```

Expected: `100% tests passed`, 5 tests.

- [ ] **Step 7: Run the whole Core suite — the clause touches every loan query**

```bash
ctest --test-dir build -L core --output-on-failure 2>&1 | tail -15
```

Expected: `100% tests passed`. If `test_core_CirculationRepository` or an archive test
fails, the clause is leaking into a query that should not be filtered — fix before
committing.

- [ ] **Step 8: Commit**

```bash
git add libraries/Core/include/VLMS/Core/LoanTypes.h libraries/Core/src/LoanSql.cpp \
        libraries/Core/test/src/test_loan_book_filter.cpp libraries/Core/test/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Filter loans by the book their copy belongs to.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: The two new strings

**Files:**
- Modify: `libraries/Core/src/Strings.cpp` (three tables)

**Interfaces:**
- Consumes: nothing.
- Produces: keys `catalog.loans` and `catalog.loanHistoryTitle`, read by Tasks 3 and 4
  through `T("catalog.loans")` and `T("catalog.loanHistoryTitle")`.

- [ ] **Step 1: Add the Arabic entries**

In `libraries/Core/src/Strings.cpp`, find the Arabic `{"catalog.categories", "التصنيفات…"},`
line (around line 319) and add directly after it:

```cpp
        {"catalog.loans", "الإعارات"},
        {"catalog.loanHistoryTitle", "سجل الإعارات — {name}"},
```

- [ ] **Step 2: Add the French entries**

Find the French `{"catalog.categories", "Catégories…"},` line (around line 827) and add
directly after it:

```cpp
        {"catalog.loans", "Prêts"},
        {"catalog.loanHistoryTitle", "Historique des prêts — {name}"},
```

- [ ] **Step 3: Add the English entries**

Find the English `{"catalog.categories", "Categories…"},` line (around line 1347) and add
directly after it:

```cpp
        {"catalog.loans", "Loans"},
        {"catalog.loanHistoryTitle", "Loan history — {name}"},
```

- [ ] **Step 4: Run the parity test**

```bash
cmake --build build -j$(nproc) >/dev/null 2>&1 && ctest --test-dir build -R test_core_StringsParity --output-on-failure 2>&1 | tail -10
```

Expected: PASS. A failure here names the table that is missing a key — one of the three
edits went into the wrong place.

- [ ] **Step 5: Commit**

```bash
git add libraries/Core/src/Strings.cpp
git commit -m "$(cat <<'EOF'
Add the catalogue loan-history strings.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: `BookLoansDialog`

**Files:**
- Create: `applications/vlms/src/ui/catalog/BookLoansDialog.h`
- Create: `applications/vlms/src/ui/catalog/BookLoansDialog.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (source list, line ~21 and ~49)

**Interfaces:**
- Consumes: `LoanQuery::bookId` from Task 1; `catalog.loanHistoryTitle` from Task 2.
- Produces:
  ```cpp
  BookLoansDialog(CirculationRepository& repository,
                  qint64 bookId,
                  const QString& bookTitle,
                  QWidget* parent = nullptr);
  ```
  Task 4 constructs it and calls `exec()`.

This task has no test of its own — Task 5's UI test drives the dialog through the button
that opens it, which is how it is actually reached. Build-and-run is the gate here.

- [ ] **Step 1: Write the header**

Create `applications/vlms/src/ui/catalog/BookLoansDialog.h`:

```cpp
#pragma once

#include <VLMS/Core/CirculationRepository.h>

#include <QDialog>

class QTableWidget;

/// Every loan ever made of one book, across every copy it has ever had.
/// The member dialog's twin; they differ in their query, their title and one
/// column, which is less than a shared class with a mode flag would cost.
class BookLoansDialog final : public QDialog {
    Q_OBJECT

public:
    BookLoansDialog(CirculationRepository& repository,
                    qint64 bookId,
                    const QString& bookTitle,
                    QWidget* parent = nullptr);

private:
    void buildUi();
    void retranslateUi();
    void refresh();
    QString loanStatusLabel(const LoanRecord& loan) const;

    CirculationRepository& m_repository;
    qint64 m_bookId = 0;
    QString m_bookTitle;
    QTableWidget* m_table = nullptr;
};
```

- [ ] **Step 2: Write the implementation**

Create `applications/vlms/src/ui/catalog/BookLoansDialog.cpp`:

```cpp
#include "ui/catalog/BookLoansDialog.h"

#include <VLMS/Core/Strings.h>
#include "ui/TableHeaderSort.h"
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QTableWidget>
#include <QVariant>
#include <QVBoxLayout>

using VLMS::T;
using VLMS::qs;

BookLoansDialog::BookLoansDialog(CirculationRepository& repository,
                                 const qint64 bookId,
                                 const QString& bookTitle,
                                 QWidget* parent)
    : QDialog(parent),
      m_repository(repository),
      m_bookId(bookId),
      m_bookTitle(bookTitle) {
    resize(820, 420);
    buildUi();
    retranslateUi();
    refresh();
}

void BookLoansDialog::buildUi() {
    auto* layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    VLMS::enableWidgetTableSort(m_table);
    layout->addWidget(m_table);

    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    VLMS::localizeButtonBox(closeBox);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(closeBox);
}

void BookLoansDialog::retranslateUi() {
    QString title = T("catalog.loanHistoryTitle");
    title.replace(QStringLiteral("{name}"), m_bookTitle);
    setWindowTitle(title);

    VLMS::retranslateStandardButtons(findChild<QDialogButtonBox*>());

    // The title is the same on every row, so the member takes its column.
    m_table->setHorizontalHeaderLabels({
        T("circulation.col.member"),
        T("loan.field.copy"),
        T("circulation.col.borrowed"),
        T("circulation.col.due"),
        T("loan.field.returnedAt"),
        T("circulation.col.status"),
    });
}

QString BookLoansDialog::loanStatusLabel(const LoanRecord& loan) const {
    if (!loan.returnedAt.empty()) {
        return T("circulation.status.returned");
    }
    if (loan.isOverdue) {
        return T("circulation.status.overdue");
    }
    return T("circulation.status.open");
}

void BookLoansDialog::refresh() {
    LoanQuery query;
    query.bookId = m_bookId;
    query.archive = ArchiveScope::Any;  // history: an archived loan still happened
    query.limit = 1000;
    query.offset = 0;

    const auto loansResult = m_repository.listLoans(query);
    if (!loansResult) {
        VLMS::showRepoError(this, loansResult.error());
        return;
    }
    const auto& loans = loansResult.value();
    m_table->setRowCount(loans.size());

    for (int row = 0; row < loans.size(); ++row) {
        const LoanRecord& loan = loans.at(row);
        auto* memberItem = new QTableWidgetItem(qs(loan.memberName));
        memberItem->setData(Qt::UserRole, QVariant::fromValue(loan.id));
        m_table->setItem(row, 0, memberItem);
        m_table->setItem(row, 1, new QTableWidgetItem(qs(loan.copyCode)));
        m_table->setItem(row, 2, new QTableWidgetItem(qs(loan.borrowedAt)));
        m_table->setItem(row, 3, new QTableWidgetItem(qs(loan.dueAt)));
        m_table->setItem(
            row,
            4,
            new QTableWidgetItem(loan.returnedAt.empty()
                                     ? T("common.emDash")
                                     : qs(loan.returnedAt)));
        m_table->setItem(row, 5, new QTableWidgetItem(loanStatusLabel(loan)));
    }
}
```

Note `loans.size()` is compared against an `int row` exactly as `MemberLoansDialog` does;
`listLoans` returns a `std::vector`, and the existing dialog compiles this way under the
project's warning settings. If a `-Wsign-compare` warning appears, match whatever
`MemberLoansDialog.cpp` does rather than inventing a new idiom.

- [ ] **Step 3: Register the files**

In `applications/vlms/CMakeLists.txt`, add to the `.cpp` list after
`src/ui/catalog/CategoryManagerDialog.cpp`:

```cmake
    src/ui/catalog/BookLoansDialog.cpp
```

and to the header list after `src/ui/catalog/CategoryManagerDialog.h`:

```cmake
    src/ui/catalog/BookLoansDialog.h
```

- [ ] **Step 4: Build**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -15
```

Expected: builds clean. `Q_OBJECT` needs `AUTOMOC`, which the target already has; if the
link fails on `vtable for BookLoansDialog`, the header was not added to the CMake list.

- [ ] **Step 5: Commit**

```bash
git add applications/vlms/src/ui/catalog/BookLoansDialog.h \
        applications/vlms/src/ui/catalog/BookLoansDialog.cpp \
        applications/vlms/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add the book loan-history dialog.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: `CatalogPage` swaps `Categories…` for `Loans`

**Files:**
- Modify: `applications/vlms/src/ui/catalog/CatalogPage.h:27`, `:38-41`, `:56-66`, `:69`, `:75-78`
- Modify: `applications/vlms/src/ui/catalog/CatalogPage.cpp:129-141`, `:209-213`, `:601-616`, `:795-801`
- Modify: `applications/vlms/src/ui/MainWindow.cpp:134`
- Modify: `applications/vlms/test/src/test_catalog_sort.cpp`, `test_catalog_local_number.cpp`, `test_archive_catalog_ui.cpp` (constructor call sites)

**Interfaces:**
- Consumes: `BookLoansDialog` from Task 3; `catalog.loans` from Task 2;
  `LoanQuery::bookId` from Task 1.
- Produces: `CatalogPage(CatalogRepository&, CirculationRepository&, QWidget* = nullptr)`,
  and a `Loans` button whose text is `T("catalog.loans")` — Task 5's test finds it by that
  text.

- [ ] **Step 1: Widen the constructor and declare the members**

In `applications/vlms/src/ui/catalog/CatalogPage.h`:

Add the include beside the existing one:

```cpp
#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CatalogTypes.h>
#include <VLMS/Core/CirculationRepository.h>
```

Replace the constructor declaration:

```cpp
    CatalogPage(CatalogRepository& repository,
                CirculationRepository& circulation,
                QWidget* parent = nullptr);
```

(`explicit` goes, as it did on `MembersPage` — the constructor now takes two arguments.)

In the `private slots:` block, replace `void manageCategories();` with:

```cpp
    void showLoanHistory();
```

In the private helpers, after `void clearBookDetails();`:

```cpp
    void updateLoansButton();
```

Replace the `m_categoriesButton` member with:

```cpp
    QPushButton* m_loansButton = nullptr;
```

and add the repository beside the existing one:

```cpp
    CatalogRepository& m_repository;
    CirculationRepository& m_circulation;
```

- [ ] **Step 2: Rewire the page**

In `applications/vlms/src/ui/catalog/CatalogPage.cpp`:

Swap the `CategoryManagerDialog.h` include for the new dialog, keeping both — the category
manager is still opened, now from the book dialog, so this page no longer needs it:

```cpp
#include "ui/catalog/BookEditorDialog.h"
#include "ui/catalog/BookLoansDialog.h"
#include "ui/catalog/LocalNumberDelegate.h"
```

(delete the `CategoryManagerDialog.h` line.)

Widen the constructor definition:

```cpp
CatalogPage::CatalogPage(CatalogRepository& repository,
                         CirculationRepository& circulation,
                         QWidget* parent)
    : QWidget(parent),
      m_repository(repository),
      m_circulation(circulation) {
```

In `buildUi`, replace the button block:

```cpp
    m_loansButton = VLMS::makeSecondaryButton({});
    m_addButton = VLMS::makePrimaryButton({});
    m_editButton = VLMS::makeSecondaryButton({});
    m_deleteButton = VLMS::makeSecondaryButton({});
    m_loansButton->setEnabled(false);
    connect(m_loansButton, &QPushButton::clicked, this, &CatalogPage::showLoanHistory);
    connect(m_addButton, &QPushButton::clicked, this, &CatalogPage::addBook);
    connect(m_editButton, &QPushButton::clicked, this, &CatalogPage::editBook);
    connect(m_deleteButton, &QPushButton::clicked, this, &CatalogPage::deleteBook);
    frame->addButton(m_loansButton);
    frame->addButton(m_addButton);
    frame->addButton(m_editButton);
    frame->addButton(m_deleteButton);
```

In `retranslateUi`, replace the `m_categoriesButton` line:

```cpp
    m_loansButton->setText(T("catalog.loans"));
```

Replace `manageCategories()` (around line 795) with:

```cpp
void CatalogPage::updateLoansButton() {
    const qint64 bookId = selectedBookId();
    if (bookId <= 0) {
        m_loansButton->setEnabled(false);
        return;
    }

    LoanQuery query;
    query.bookId = bookId;
    query.archive = ArchiveScope::Any;
    query.limit = 1;
    query.offset = 0;
    const auto loanCount = m_circulation.countLoans(query);
    m_loansButton->setEnabled(loanCount && loanCount.value() > 0);
}

void CatalogPage::showLoanHistory() {
    const qint64 bookId = selectedBookId();
    if (bookId <= 0) {
        return;
    }

    const auto book = m_repository.getBook(bookId);
    if (!book.has_value()) {
        return;
    }

    BookLoansDialog dialog(m_circulation, bookId, qs(book->title), this);
    dialog.exec();
}
```

In `onSelectionChanged`, call it **before** the early return, so deselecting disables the
button:

```cpp
void CatalogPage::onSelectionChanged() {
    updateLoansButton();

    const qint64 bookId = selectedBookId();
    if (bookId <= 0) {
        return;
    }
```

and at the end of `refreshSelectedBookPreview`, after `updateCoverPreview(book.value());`,
add `updateLoansButton();` — and in its two early-return branches, after
`clearBookDetails();`, add `updateLoansButton();` as well, so a refresh that loses the
selection also loses the button.

- [ ] **Step 3: Update `MainWindow`**

In `applications/vlms/src/ui/MainWindow.cpp:134`:

```cpp
        m_catalogPage = new CatalogPage(app->catalog(), app->circulation(), m_stack);
```

- [ ] **Step 4: Update the three test fixtures**

Each already has a `TestDatabase`; add a `CirculationRepository` beside the existing
`CatalogRepository` and pass it.

In `applications/vlms/test/src/test_catalog_sort.cpp` and
`test_catalog_local_number.cpp`, add the include:

```cpp
#include <VLMS/Core/CirculationRepository.h>
```

add the member beside `std::unique_ptr<CatalogRepository> m_catalog;`:

```cpp
    std::unique_ptr<CirculationRepository> m_circulation;
```

construct it in `SetUp`, right after the catalogue repository is made:

```cpp
    m_circulation = std::make_unique<CirculationRepository>(m_db->session());
```

and change every `std::make_unique<CatalogPage>(*m_catalog)` to:

```cpp
    std::make_unique<CatalogPage>(*m_catalog, *m_circulation)
```

In `test_archive_catalog_ui.cpp:119`, `CatalogPage page(*m_catalog);` becomes
`CatalogPage page(*m_catalog, *m_circulation);` — add the same include, member and
construction to that fixture if it lacks them.

- [ ] **Step 5: Build and run the UI suite**

```bash
cmake --build build -j$(nproc) >/dev/null 2>&1 && ctest --test-dir build -L ui --output-on-failure 2>&1 | tail -20
```

Expected: `100% tests passed`. A `no matching constructor` error means a call site was
missed; the compiler names the file and line.

- [ ] **Step 6: Commit**

```bash
git add applications/vlms/src/ui/catalog/CatalogPage.h \
        applications/vlms/src/ui/catalog/CatalogPage.cpp \
        applications/vlms/src/ui/MainWindow.cpp \
        applications/vlms/test/src/test_catalog_sort.cpp \
        applications/vlms/test/src/test_catalog_local_number.cpp \
        applications/vlms/test/src/test_archive_catalog_ui.cpp
git commit -m "$(cat <<'EOF'
Open a book's loan history from the catalogue.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: The Catalogue loans UI test

**Files:**
- Create: `applications/vlms/test/src/test_catalog_loans.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt` (source list)

**Interfaces:**
- Consumes: everything from Tasks 1–4.
- Produces: nothing later tasks use.

Written after Task 4 rather than before it because the button cannot be found by text
until the string and the button both exist; Task 1 already covers the query itself
test-first.

- [ ] **Step 1: Write the test**

Create `applications/vlms/test/src/test_catalog_loans.cpp`:

```cpp
#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/catalog/BookLoansDialog.h"
#include "ui/catalog/CatalogPage.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>

#include <QPushButton>
#include <QTableWidget>

#include <gtest/gtest.h>

#include <memory>

using VLMS::Locale;
using VLMS::T;
using namespace VLMS::Test;

namespace {

QPushButton* buttonWithText(QWidget* root, const QString& text)
{
    for (QPushButton* button : root->findChildren<QPushButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

class test_ui_CatalogLoans : public ::testing::Test {
protected:
    void SetUp() override
    {
        Locale::apply("en");
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isOpen());
        m_catalog = std::make_unique<CatalogRepository>(m_db->session());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());
        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_GT(m_memberId, 0);
    }

    void TearDown() override
    {
        m_page.reset();
        Locale::apply("en");
    }

    /// Builds the page after the rows are seeded: it queries on construction.
    void openPage()
    {
        m_page = std::make_unique<CatalogPage>(*m_catalog, *m_circulation);
        m_table = m_page->findChild<QTableWidget*>();
        ASSERT_NE(m_table, nullptr);
    }

    void selectRowWithTitle(const QString& title)
    {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (m_table->item(row, 0) != nullptr && m_table->item(row, 0)->text() == title) {
                m_table->selectRow(row);
                return;
            }
        }
        FAIL() << "no row titled " << title.toStdString();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::unique_ptr<CatalogPage> m_page;
    QTableWidget* m_table = nullptr;
    std::int64_t m_memberId = 0;
};

TEST_F(test_ui_CatalogLoans, TheLoansButtonIsDisabledUntilABookWithLoansIsSelected)
{
    BookSeed borrowed = uniqueBookSeed(1);
    borrowed.title = "Borrowed Book";
    const std::int64_t borrowedId = seedBook(*m_db, borrowed);
    ASSERT_GT(borrowedId, 0);

    BookSeed untouched = uniqueBookSeed(2);
    untouched.title = "Untouched Book";
    ASSERT_GT(seedBook(*m_db, untouched), 0);

    const auto copies = copyIdsOf(*m_db, borrowedId);
    ASSERT_FALSE(copies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(0), "2025-01-10", "2025-01-24"), 0);

    openPage();
    auto* loans = buttonWithText(m_page.get(), T("catalog.loans"));
    ASSERT_NE(loans, nullptr) << "no button carries the catalog.loans label";

    selectRowWithTitle(QStringLiteral("Untouched Book"));
    EXPECT_FALSE(loans->isEnabled()) << "a book never borrowed offers an empty history";

    selectRowWithTitle(QStringLiteral("Borrowed Book"));
    EXPECT_TRUE(loans->isEnabled());
}

TEST_F(test_ui_CatalogLoans, TheHistoryListsEveryLoanOfEveryCopy)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Two Copies";
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copies.size(), 2U);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(0),
                            "2025-01-10", "2025-01-24", "2025-01-20"), 0);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(1), "2025-02-10", "2025-02-24"), 0);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(0), "2025-03-10", "2025-03-24"), 0);

    BookLoansDialog dialog(*m_circulation, bookId, QStringLiteral("Two Copies"));
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 3) << "the history covers both copies";
}

TEST_F(test_ui_CatalogLoans, TheHistoryNamesTheMemberNotTheTitle)
{
    MemberSeed borrower = uniqueMemberSeed(7);
    borrower.firstName = "Salim";
    borrower.lastName = "Trabelsi";
    const std::int64_t memberId = seedMember(*m_db, borrower);
    ASSERT_GT(memberId, 0);

    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Named Book";
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_FALSE(copies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copies.at(0), "2025-01-10", "2025-01-24"), 0);

    BookLoansDialog dialog(*m_circulation, bookId, QStringLiteral("Named Book"));
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 1);
    const QString first = table->item(0, 0)->text();
    EXPECT_NE(first, QStringLiteral("Named Book")) << "the title column should be gone";
    EXPECT_TRUE(first.contains(QStringLiteral("Trabelsi"))) << "first column: " << first.toStdString();
}

TEST_F(test_ui_CatalogLoans, TheWindowTitleCarriesTheBookTitle)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Titled Book";
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    BookLoansDialog dialog(*m_circulation, bookId, QStringLiteral("Titled Book"));
    EXPECT_TRUE(dialog.windowTitle().contains(QStringLiteral("Titled Book")));
    EXPECT_FALSE(dialog.windowTitle().contains(QStringLiteral("{name}")))
        << "the placeholder was not replaced";
}

}  // namespace
```

Register it — in `applications/vlms/test/CMakeLists.txt`, add after
`src/test_catalog_local_number.cpp`:

```cmake
        src/test_catalog_loans.cpp
```

- [ ] **Step 2: Run the tests**

```bash
cmake --build build -j$(nproc) >/dev/null 2>&1 && ctest --test-dir build -R test_ui_CatalogLoans --output-on-failure 2>&1 | tail -20
```

Expected: `100% tests passed`, 4 tests. If `TheLoansButtonIsDisabledUntil…` fails on the
enabled case, `updateLoansButton` is not reached from the selection handler — check that
it sits **before** the early return in `onSelectionChanged`.

- [ ] **Step 3: Sanity-check that the test can fail**

Temporarily change `query.bookId = m_bookId;` in `BookLoansDialog::refresh` to
`query.bookId = 0;`, rebuild, and re-run. `TheHistoryListsEveryLoanOfEveryCopy` must FAIL.
Restore the line and re-run to green. A test that cannot fail is not a test.

- [ ] **Step 4: Commit**

```bash
git add applications/vlms/test/src/test_catalog_loans.cpp \
        applications/vlms/test/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Guard the catalogue loan history.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: `Categories…` moves onto the book dialog's Category row

**Files:**
- Modify: `applications/vlms/src/ui/catalog/BookEditorDialog.h` (one slot, one member)
- Modify: `applications/vlms/src/ui/catalog/BookEditorDialog.cpp:199-201` (the row),
  `:373-386` (retranslate)

**Interfaces:**
- Consumes: the existing `catalog.categories` key and `CategoryManagerDialog`.
- Produces: nothing later tasks use.

- [ ] **Step 1: Declare the button and its slot**

In `applications/vlms/src/ui/catalog/BookEditorDialog.h`, beside
`void populateCategories();`:

```cpp
    void populateCategories();
    void manageCategories();
```

and beside `QComboBox* m_categoryCombo = nullptr;`:

```cpp
    QPushButton* m_categoriesButton = nullptr;
```

If `QPushButton` is not already forward-declared or included in that header, add
`class QPushButton;` with the other forward declarations.

- [ ] **Step 2: Build the row**

In `applications/vlms/src/ui/catalog/BookEditorDialog.cpp`, add the includes if
missing:

```cpp
#include "ui/catalog/CategoryManagerDialog.h"

#include <QHBoxLayout>
#include <QPushButton>
```

Replace the category row (around line 199):

```cpp
    // The category list is managed from the row that uses it: a librarian who
    // finds the category missing while cataloguing no longer has to abandon
    // the book to add it. The row is one widget, so the form's label column and
    // its RTL mirroring are untouched.
    m_categoryCombo = new QComboBox(bookTab);
    m_categoriesButton = VLMS::makeSecondaryButton({});
    connect(m_categoriesButton, &QPushButton::clicked, this, &BookEditorDialog::manageCategories);

    auto* categoryRow = new QWidget(bookTab);
    auto* categoryLayout = new QHBoxLayout(categoryRow);
    categoryLayout->setContentsMargins(0, 0, 0, 0);
    categoryLayout->setSpacing(8);
    categoryLayout->addWidget(m_categoryCombo, 1);
    categoryLayout->addWidget(m_categoriesButton, 0);
    m_form->addRow(makeFieldLabel(bookTab), categoryRow);
```

The row index stays 3, so `setFormLabel(m_form, 3, …)` in `retranslateUi` still finds the
label — it reads `LabelRole`, which is untouched.

- [ ] **Step 3: Label it and wire the dialog**

In `retranslateUi`, after the `setFormLabel(m_form, 4, …)` line:

```cpp
    if (m_categoriesButton != nullptr) {
        m_categoriesButton->setText(T("catalog.categories"));
    }
```

And add the slot, next to `populateCategories`:

```cpp
void BookEditorDialog::manageCategories() {
    CategoryManagerDialog dialog(m_repository, this);
    dialog.exec();
    // The list the combo shows may have grown or lost an entry; the current
    // pick is read and restored by populateCategories itself.
    populateCategories();
}
```

- [ ] **Step 4: Build and run the whole suite**

```bash
cmake --build build -j$(nproc) >/dev/null 2>&1 && ctest --test-dir build --output-on-failure 2>&1 | tail -20
```

Expected: `100% tests passed`. `test_ui_DialogTranslations` and `test_ui_BookEditorOcr`
both build this dialog and must still pass.

- [ ] **Step 5: Commit**

```bash
git add applications/vlms/src/ui/catalog/BookEditorDialog.h \
        applications/vlms/src/ui/catalog/BookEditorDialog.cpp
git commit -m "$(cat <<'EOF'
Manage categories from the book dialog's category row.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: Verify in the running app

**Files:** none — this task changes nothing. It is the gate the test suite cannot be.

The project's standing rule: a change is not finished on green tests alone. The screenshot
recipe is xcb rather than Wayland, captured by window id, and the capture dies unless it
runs in the foreground.

- [ ] **Step 1: Build the sandbox binary**

```bash
cmake -S . -B build -DVLMS_DEV_PATHS=OFF >/dev/null && cmake --build build -j$(nproc) 2>&1 | tail -5
```

- [ ] **Step 2: Drive the app and confirm, in this order**

1. Catalogue: the button pad reads `Loans | Add Book | Edit | Delete`, and `Categories…`
   is gone from it.
2. With nothing selected, `Loans` is greyed.
3. Select a book that has been borrowed — `Loans` lights up; click it; the window title
   names the book, the first column names members, and the dates read correctly.
4. Select a book never borrowed — `Loans` greys again.
5. `Add Book`: the Category row shows the drop-down then `Categories…`; click it, add a
   category, close, and confirm the new category is in the drop-down and the previous
   pick survived.
6. Switch to Arabic: the row mirrors, the button sits at the leading edge of the row, and
   both new strings are Arabic. Repeat in dark mode.

- [ ] **Step 3: Record what was seen**

Add a dated entry to the Session Log at the bottom of `CLAUDE.md`, newest first, a couple
of lines: what moved, the `bc.book_id` clause and why it reaches archived copies, and
anything that surprised you during step 2.

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md
git commit -m "$(cat <<'EOF'
Note the catalogue loans work in the session log.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```
