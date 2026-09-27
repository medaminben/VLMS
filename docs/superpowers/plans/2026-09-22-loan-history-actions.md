# Loan History Actions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn both loan-history dialogs into places where a librarian can lend, extend
and return, and stop the `Loans` buttons being disabled when there is no history.

**Architecture:** One new widget, `LoanHistoryActions`, owns the three buttons, their
selection-driven enablement, and the Extend/Return work — both dialogs embed it and differ only
in what `Loan` means. `LoanCheckoutDialog` takes an optional `LoanScope` fixing the book or
the member. Core's `listAvailableCopies` gains an optional `bookId`.

**Tech Stack:** C++17, Qt 6 Widgets, SQLite via `SqliteSession`, GoogleTest through `ctest`.

## Global Constraints

- **Judge Core tests by `ctest`, never by running the binary.** Core registers as a single
  ctest entry named `test_vlms_core`, so `ctest -R test_core_Something` finds nothing —
  use `ctest --test-dir build -L core`. UI tests are discovered individually and `-R` works.
- **Reconfigure after adding a source file:** `cmake -S . -B build` before
  `cmake --build build -j$(nproc)`, or the new file is silently not compiled.
- **British English in prose and comments**; never in Qt API names or identifiers.
- **Every user-visible string is a key in all three tables** in `libraries/Core/src/Strings.cpp`
  (Arabic, French, English, in that file order). `test_core_StringsParity` fails otherwise.
- **The app direction is RTL in Arabic.** Never position a widget by absolute side.
- **A scoping test must seed something the scope excludes.** Last round a history test
  seeded one book and passed with the filter deleted. Every scoping test here seeds a
  second book or member that would appear if the scope were dropped.
- Commit after every task, trailer `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

## File Structure

| File | Responsibility |
|---|---|
| `libraries/Core/include/VLMS/Core/CirculationRepository.h` | `listAvailableCopies` gains `bookId` |
| `libraries/Core/src/CirculationRepository.cpp` | the `bc.book_id` clause |
| `libraries/Core/test/src/test_loan_book_filter.cpp` | scoped-copies test |
| `libraries/Core/src/Strings.cpp` | two empty-state keys |
| `applications/vlms/src/ui/circulation/LoanCheckoutDialog.{h,cpp}` | `LoanScope` |
| `applications/vlms/src/ui/circulation/LoanHistoryActions.{h,cpp}` | new; the shared button row |
| `applications/vlms/src/ui/catalog/BookLoansDialog.{h,cpp}` | empty state + book-scoped Loan |
| `applications/vlms/src/ui/members/MemberLoansDialog.{h,cpp}` | empty state + member-scoped Loan |
| `applications/vlms/src/ui/catalog/CatalogPage.{h,cpp}` | drop `updateLoansButton` |
| `applications/vlms/src/ui/members/MembersPage.{h,cpp}` | drop `updateLoansButton` |
| `applications/vlms/test/src/test_catalog_loans.cpp` | extended |
| `applications/vlms/test/src/test_member_loans_actions.cpp` | new |

---

### Task 1: Available copies can be scoped to one book

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/CirculationRepository.h:32-33`
- Modify: `libraries/Core/src/CirculationRepository.cpp:506-546`
- Modify: `libraries/Core/test/src/test_loan_book_filter.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `listAvailableCopies(const std::string& search = {}, std::int64_t bookId = 0)`.
  Task 3 calls it with a book id; `CirculationPage` keeps calling it with one argument.

- [ ] **Step 1: Write the failing test**

Append to `libraries/Core/test/src/test_loan_book_filter.cpp`, inside the anonymous
namespace, before its closing brace:

```cpp
TEST_F(test_core_LoanBookFilter, AvailableCopiesCanBeScopedToOneBook)
{
    // The fixture leaves the first book with one free copy of two (the second
    // is still out) and the second book with none (its only copy is out).
    // A third book, untouched, gives the scope something to exclude.
    BookSeed spare = uniqueBookSeed(4);
    spare.title = "Spare Book";
    spare.initialCopyCount = 2;
    const std::int64_t spareId = seedBook(*m_db, spare);
    ASSERT_GT(spareId, 0);

    const auto everything = m_circulation->listAvailableCopies();
    ASSERT_TRUE(everything.has_value());
    EXPECT_GE(everything.value().size(), 3U) << "both books' free copies";

    const auto scoped = m_circulation->listAvailableCopies({}, spareId);
    ASSERT_TRUE(scoped.has_value());
    EXPECT_EQ(scoped.value().size(), 2U);
    for (const LoanCopyOption& copy : scoped.value()) {
        EXPECT_EQ(copy.bookTitle, "Spare Book") << "another book's copy leaked in";
    }
}

TEST_F(test_core_LoanBookFilter, ScopingLeavesOutACopyThatIsOnLoan)
{
    // The first book has two copies; the second one's loan was never returned.
    const auto scoped = m_circulation->listAvailableCopies({}, m_firstBookId);
    ASSERT_TRUE(scoped.has_value());
    EXPECT_EQ(scoped.value().size(), 1U) << "the copy still out must not be offered";
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | grep -E "error" | head -5
```

Expected: a compile error — `no matching function for call to
'listAvailableCopies(<brace-enclosed initializer list>, int64_t&)'`.

- [ ] **Step 3: Widen the signature**

In `libraries/Core/include/VLMS/Core/CirculationRepository.h`, replace the declaration:

```cpp
    [[nodiscard]] VLMS::Result<std::vector<LoanCopyOption>>
    listAvailableCopies(const std::string& search = {}, std::int64_t bookId = 0) const;
```

- [ ] **Step 4: Add the clause**

In `libraries/Core/src/CirculationRepository.cpp`, change the definition's signature to
match (no default arguments in the definition), and add the clause directly after the
`WHERE` block, before the search block:

```cpp
CirculationRepository::listAvailableCopies(const std::string& search,
                                           const std::int64_t bookId) const
{
```

```cpp
    // One book's free copies: the query already excludes copies on loan and
    // archived copies, so scoping it is one clause over the join it makes.
    if (bookId > 0) {
        sql += " AND bc.book_id = :book_id ";
    }
```

and bind it beside the search bind, before `ORDER BY` is appended — the bind must happen
after `prepare`, so add it below the existing `:search` bind:

```cpp
    if (bookId > 0 && !q->bind(":book_id", bookId)) {
        return RepoSql::sqlResult<std::vector<LoanCopyOption>>(m_session.lastError());
    }
```

- [ ] **Step 5: Run the Core suite**

```bash
cmake --build build -j$(nproc) >/dev/null 2>&1 && ctest --test-dir build -L core --output-on-failure 2>&1 | tail -8
```

Expected: `100% tests passed`.

- [ ] **Step 6: Commit**

```bash
git add libraries/Core/include/VLMS/Core/CirculationRepository.h \
        libraries/Core/src/CirculationRepository.cpp \
        libraries/Core/test/src/test_loan_book_filter.cpp
git commit -m "$(cat <<'EOF'
Scope available copies to one book.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: The two empty-state strings

**Files:**
- Modify: `libraries/Core/src/Strings.cpp` (three tables)

**Interfaces:**
- Produces: `catalog.loanHistoryEmpty`, `members.loanHistoryEmpty`, read by Tasks 5 and 6.

- [ ] **Step 1: Arabic**

After `{"catalog.loanHistoryTitle", "سجل الإعارات — {name}"},`:

```cpp
        {"catalog.loanHistoryEmpty", "لم تُعَر هذه النسخة بعد."},
```

After `{"members.loanHistoryTitle", "سجل الإعارات — {name}"},`:

```cpp
        {"members.loanHistoryEmpty", "لم يستعر هذا العضو أي كتاب بعد."},
```

- [ ] **Step 2: French**

After the French `catalog.loanHistoryTitle`:

```cpp
        {"catalog.loanHistoryEmpty", "Cet ouvrage n'a pas encore été prêté."},
```

After the French `members.loanHistoryTitle`:

```cpp
        {"members.loanHistoryEmpty", "Cet adhérent n'a encore rien emprunté."},
```

- [ ] **Step 3: English**

After the English `catalog.loanHistoryTitle`:

```cpp
        {"catalog.loanHistoryEmpty", "This book has not been circulated yet."},
```

After the English `members.loanHistoryTitle`:

```cpp
        {"members.loanHistoryEmpty", "This member has not borrowed anything yet."},
```

- [ ] **Step 4: Verify parity**

```bash
cmake --build build -j$(nproc) >/dev/null 2>&1 && ctest --test-dir build -L core --output-on-failure 2>&1 | tail -6
```

Expected: `100% tests passed`. A parity failure names the table missing a key.

- [ ] **Step 5: Commit**

```bash
git add libraries/Core/src/Strings.cpp
git commit -m "$(cat <<'EOF'
Add the empty loan-history strings.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: `LoanCheckoutDialog` takes a scope

**Files:**
- Modify: `applications/vlms/src/ui/circulation/LoanCheckoutDialog.h`
- Modify: `applications/vlms/src/ui/circulation/LoanCheckoutDialog.cpp`

**Interfaces:**
- Consumes: `listAvailableCopies(search, bookId)` from Task 1.
- Produces:
  ```cpp
  struct LoanScope {
      qint64 bookId = 0;
      qint64 memberId = 0;
      QString label;
  };
  LoanCheckoutDialog(CirculationRepository& repository,
                     const LoanScope& scope = {},
                     QWidget* parent = nullptr);
  ```
  Tasks 5 and 6 construct it with a scope; `CirculationPage` keeps its two-argument call,
  which still compiles because `scope` defaults and `parent` is passed positionally —
  **verify this**: `LoanCheckoutDialog dialog(m_repository, this)` would bind `this` to
  `const LoanScope&`, which does not compile, so `CirculationPage` must be changed to
  `LoanCheckoutDialog dialog(m_repository, {}, this);`.

- [ ] **Step 1: Declare the scope**

In `applications/vlms/src/ui/circulation/LoanCheckoutDialog.h`, above the class:

```cpp
/// Fixes one side of a checkout. Zero on both is the unscoped dialog the
/// Circulation page opens; a book id offers only that book's free copies, and
/// a member id replaces the member rows with the name.
struct LoanScope {
    qint64 bookId = 0;
    qint64 memberId = 0;
    QString label;
};
```

Replace the constructor and add two members:

```cpp
    LoanCheckoutDialog(CirculationRepository& repository,
                       const LoanScope& scope = {},
                       QWidget* parent = nullptr);

    /// False when the scope left nothing to lend — every copy is out.
    [[nodiscard]] bool hasLendableCopy() const;
```

```cpp
    LoanScope m_scope;
    QLabel* m_memberFixedLabel = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
```

and add `class QLabel;` / `class QDialogButtonBox;` to the forward declarations.

- [ ] **Step 2: Honour the scope**

In `applications/vlms/src/ui/circulation/LoanCheckoutDialog.cpp`, add
`#include <QDialogButtonBox>` if absent (it is already included) and change the
constructor:

```cpp
LoanCheckoutDialog::LoanCheckoutDialog(CirculationRepository& repository,
                                       const LoanScope& scope,
                                       QWidget* parent)
    : QDialog(parent),
      m_repository(repository),
      m_scope(scope) {
    buildUi();
    refreshMembers();
    refreshCopies();
    retranslateUi();
}
```

In `buildUi`, the member rows become a label when the member is fixed. Replace the two
member rows:

```cpp
    if (m_scope.memberId > 0) {
        // The member is already decided; the name is shown, not chosen.
        m_memberFixedLabel = new QLabel(m_scope.label, this);
        form->addRow(new QLabel(this), m_memberFixedLabel);
    } else {
        m_memberSearchEdit = new QLineEdit(this);
        connect(m_memberSearchEdit, &QLineEdit::textChanged, this, [this](const QString&) {
            refreshMembers();
        });
        form->addRow(new QLabel(this), m_memberSearchEdit);

        m_memberCombo = new QComboBox(this);
        form->addRow(new QLabel(this), m_memberCombo);
    }
```

and the copy search row only when the book is not fixed:

```cpp
    if (m_scope.bookId <= 0) {
        m_copySearchEdit = new QLineEdit(this);
        connect(m_copySearchEdit, &QLineEdit::textChanged, this, [this](const QString&) {
            refreshCopies();
        });
        form->addRow(new QLabel(this), m_copySearchEdit);
    }

    m_copyCombo = new QComboBox(this);
    form->addRow(new QLabel(this), m_copyCombo);
```

Keep the button box in a member so it can be disabled:

```cpp
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
```

and use `m_buttons` in place of `buttons` for the rest of that block.

In the accept lambda, the member check must tolerate the fixed member:

```cpp
        if (m_scope.memberId <= 0 && m_memberCombo->currentData().toLongLong() <= 0) {
```

- [ ] **Step 3: Scope the two queries**

`refreshMembers` returns early when the member is fixed:

```cpp
void LoanCheckoutDialog::refreshMembers() {
    if (m_scope.memberId > 0) {
        return;  // no combo to fill; the name is a label
    }
    const qint64 currentId = m_memberCombo->currentData().toLongLong();
```

`refreshCopies` passes the book and the (possibly absent) search field:

```cpp
void LoanCheckoutDialog::refreshCopies() {
    const qint64 currentId = m_copyCombo->currentData().toLongLong();
    m_copyCombo->clear();

    const std::string search =
        m_copySearchEdit == nullptr ? std::string() : ss(m_copySearchEdit->text());
    const auto copiesResult = m_repository.listAvailableCopies(search, m_scope.bookId);
```

and the empty branch disables OK, so a book with every copy out says so rather than
offering a button that cannot work:

```cpp
    const auto& copies = copiesResult.value();
    if (copies.empty()) {
        m_copyCombo->addItem(T("loan.noCopies"), 0);
        if (m_buttons != nullptr) {
            m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        }
        return;
    }
    if (m_buttons != nullptr) {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    }
```

(`#include <QPushButton>` is needed for `->button(...)->setEnabled`.)

Add the accessor:

```cpp
bool LoanCheckoutDialog::hasLendableCopy() const {
    return m_copyCombo != nullptr && m_copyCombo->currentData().toLongLong() > 0;
}
```

`loanInput` takes the member from the scope when it is fixed:

```cpp
    input.memberId = m_scope.memberId > 0 ? m_scope.memberId
                                          : m_memberCombo->currentData().toLongLong();
```

- [ ] **Step 4: Label the rows that exist**

`retranslateUi` currently sets labels by fixed row index, which the scope breaks. Replace
the six `setLabel(...)` calls with a running index:

```cpp
    int row = 0;
    if (m_scope.memberId > 0) {
        setLabel(row++, QStringLiteral("loan.field.member"));
    } else {
        setLabel(row++, QStringLiteral("loan.field.memberSearch"));
        setLabel(row++, QStringLiteral("loan.field.member"));
    }
    if (m_scope.bookId <= 0) {
        setLabel(row++, QStringLiteral("loan.field.copySearch"));
    }
    setLabel(row++, QStringLiteral("loan.field.copy"));
    setLabel(row++, QStringLiteral("loan.field.borrowedAt"));
    setLabel(row++, QStringLiteral("loan.field.dueAt"));
    setLabel(row++, QStringLiteral("loan.field.notes"));
```

Guard the two placeholder lines, since either edit may not exist:

```cpp
    if (m_memberSearchEdit != nullptr) {
        m_memberSearchEdit->setPlaceholderText(T("loan.memberSearchHint"));
    }
    if (m_copySearchEdit != nullptr) {
        m_copySearchEdit->setPlaceholderText(T("loan.copySearchHint"));
    }
```

and title the dialog with the scope when there is one:

```cpp
    setWindowTitle(m_scope.label.isEmpty()
                       ? T("loan.checkoutTitle")
                       : QStringLiteral("%1 — %2").arg(T("loan.checkoutTitle"), m_scope.label));
```

- [ ] **Step 5: Fix the Circulation page's call**

In `applications/vlms/src/ui/circulation/CirculationPage.cpp:606`:

```cpp
    LoanCheckoutDialog dialog(m_repository, {}, this);
```

- [ ] **Step 6: Build and run the UI suite**

```bash
cmake --build build -j$(nproc) >/dev/null 2>&1 && ctest --test-dir build -L ui --output-on-failure 2>&1 | tail -8
```

Expected: `100% tests passed`, 156 tests. `test_ui_LoanDialogs` and
`test_ui_DialogTranslations` both build this dialog unscoped and must be unaffected.

- [ ] **Step 7: Commit**

```bash
git add applications/vlms/src/ui/circulation/LoanCheckoutDialog.h \
        applications/vlms/src/ui/circulation/LoanCheckoutDialog.cpp \
        applications/vlms/src/ui/circulation/CirculationPage.cpp
git commit -m "$(cat <<'EOF'
Let a checkout be scoped to one book or one member.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: `LoanHistoryActions`, the shared button row

**Files:**
- Create: `applications/vlms/src/ui/circulation/LoanHistoryActions.h`
- Create: `applications/vlms/src/ui/circulation/LoanHistoryActions.cpp`
- Modify: `applications/vlms/CMakeLists.txt`

**Interfaces:**
- Consumes: `CirculationRepository::getLoan/extendLoan/returnLoan`, `LoanExtendDialog`,
  `LoanReturnDialog`.
- Produces:
  ```cpp
  class LoanHistoryActions final : public QWidget {
  public:
      LoanHistoryActions(CirculationRepository& repository,
                         QTableWidget* table,
                         QWidget* parent = nullptr);
      void retranslateUi();
      void updateEnabled();
  signals:
      void loanRequested();   ///< the owning dialog knows its own scope
      void loansChanged();    ///< something was written; re-query
  };
  ```
  Tasks 5 and 6 embed it, connect `loanRequested`, and re-query on `loansChanged`.

The table it watches must keep the loan id on column 0's `Qt::UserRole` — both dialogs
already do.

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include <VLMS/Core/CirculationRepository.h>

#include <QWidget>

class QPushButton;
class QTableWidget;

/// The Loan / Extend / Return row both loan-history dialogs carry. Extend and
/// Return follow the table's selected row and are disabled unless that row is
/// a loan still out; Loan means something different on each page, so it is a
/// signal rather than a method here.
class LoanHistoryActions final : public QWidget {
    Q_OBJECT

public:
    LoanHistoryActions(CirculationRepository& repository,
                       QTableWidget* table,
                       QWidget* parent = nullptr);

    void retranslateUi();
    void updateEnabled();

    [[nodiscard]] QPushButton* loanButton() const { return m_loanButton; }
    [[nodiscard]] QPushButton* extendButton() const { return m_extendButton; }
    [[nodiscard]] QPushButton* returnButton() const { return m_returnButton; }

signals:
    void loanRequested();
    void loansChanged();

private:
    void extendLoan();
    void returnLoan();
    [[nodiscard]] qint64 selectedLoanId() const;

    CirculationRepository& m_repository;
    QTableWidget* m_table = nullptr;
    QPushButton* m_loanButton = nullptr;
    QPushButton* m_extendButton = nullptr;
    QPushButton* m_returnButton = nullptr;
};
```

- [ ] **Step 2: Write the implementation**

```cpp
#include "ui/circulation/LoanHistoryActions.h"

#include <VLMS/Core/Strings.h>
#include "ui/UiHelpers.h"
#include "ui/circulation/LoanExtendDialog.h"
#include "ui/circulation/LoanReturnDialog.h"
#include "QtBridge.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QTableWidget>

using VLMS::T;
using VLMS::ss;

LoanHistoryActions::LoanHistoryActions(CirculationRepository& repository,
                                       QTableWidget* table,
                                       QWidget* parent)
    : QWidget(parent),
      m_repository(repository),
      m_table(table) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_loanButton = VLMS::makePrimaryButton({});
    m_extendButton = VLMS::makeSecondaryButton({});
    m_returnButton = VLMS::makeSecondaryButton({});
    layout->addWidget(m_loanButton);
    layout->addWidget(m_extendButton);
    layout->addWidget(m_returnButton);
    layout->addStretch(1);

    connect(m_loanButton, &QPushButton::clicked, this, &LoanHistoryActions::loanRequested);
    connect(m_extendButton, &QPushButton::clicked, this, &LoanHistoryActions::extendLoan);
    connect(m_returnButton, &QPushButton::clicked, this, &LoanHistoryActions::returnLoan);

    if (m_table != nullptr) {
        connect(m_table, &QTableWidget::itemSelectionChanged,
                this, &LoanHistoryActions::updateEnabled);
    }
    retranslateUi();
    updateEnabled();
}

void LoanHistoryActions::retranslateUi() {
    m_loanButton->setText(T("circulation.checkout"));
    m_extendButton->setText(T("circulation.extend"));
    m_returnButton->setText(T("circulation.return"));
}

qint64 LoanHistoryActions::selectedLoanId() const {
    if (m_table == nullptr) {
        return 0;
    }
    const int row = m_table->currentRow();
    if (row < 0 || m_table->item(row, 0) == nullptr) {
        return 0;
    }
    return m_table->item(row, 0)->data(Qt::UserRole).toLongLong();
}

void LoanHistoryActions::updateEnabled() {
    // Extend and Return only mean something for a loan still out. The Status
    // column already says why they are grey, so no warning box is needed.
    const qint64 loanId = selectedLoanId();
    bool stillOut = false;
    if (loanId > 0) {
        if (const auto loan = m_repository.getLoan(loanId); loan.has_value()) {
            stillOut = loan->returnedAt.empty();
        }
    }
    m_extendButton->setEnabled(stillOut);
    m_returnButton->setEnabled(stillOut);
}

void LoanHistoryActions::extendLoan() {
    const qint64 loanId = selectedLoanId();
    if (loanId <= 0) {
        return;
    }
    const auto loan = m_repository.getLoan(loanId);
    if (!loan) {
        VLMS::showRepoError(this, loan.error());
        return;
    }

    LoanExtendDialog dialog(loan.value(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (const auto extended = m_repository.extendLoan(loanId, ss(dialog.dueAt())); !extended) {
        VLMS::showRepoError(this, extended.error());
        return;
    }
    emit loansChanged();
}

void LoanHistoryActions::returnLoan() {
    const qint64 loanId = selectedLoanId();
    if (loanId <= 0) {
        return;
    }
    const auto loan = m_repository.getLoan(loanId);
    if (!loan) {
        VLMS::showRepoError(this, loan.error());
        return;
    }

    LoanReturnDialog dialog(loan.value(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (const auto returned =
            m_repository.returnLoan(loanId, ss(dialog.returnedAt()), ss(dialog.notes()));
        !returned) {
        VLMS::showRepoError(this, returned.error());
        return;
    }
    emit loansChanged();
}
```

- [ ] **Step 3: Register and build**

Add `src/ui/circulation/LoanHistoryActions.cpp` and `.h` to the two lists in
`applications/vlms/CMakeLists.txt`, beside the other circulation entries, then:

```bash
cmake -S . -B build >/dev/null && cmake --build build -j$(nproc) 2>&1 | grep -E "error" | head -5
```

Expected: no errors. A missing `vtable for LoanHistoryActions` means the header was not
added to the CMake list (AUTOMOC needs it there).

- [ ] **Step 4: Commit**

```bash
git add applications/vlms/src/ui/circulation/LoanHistoryActions.h \
        applications/vlms/src/ui/circulation/LoanHistoryActions.cpp \
        applications/vlms/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Add the shared loan-history action row.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: The catalogue dialog lends the book

**Files:**
- Modify: `applications/vlms/src/ui/catalog/BookLoansDialog.{h,cpp}`
- Modify: `applications/vlms/src/ui/catalog/CatalogPage.{h,cpp}`

**Interfaces:**
- Consumes: `LoanScope` (Task 3), `LoanHistoryActions` (Task 4),
  `catalog.loanHistoryEmpty` (Task 2).
- Produces: a `Loans` button on the Catalogue that is enabled whenever a book row is
  selected; Task 7's tests drive it.

- [ ] **Step 1: Give the dialog the row and the empty state**

In `BookLoansDialog.h`, add to the private section:

```cpp
    void lendACopy();

    QLabel* m_emptyLabel = nullptr;
    LoanHistoryActions* m_actions = nullptr;
```

with `class QLabel;` and `class LoanHistoryActions;` forward-declared.

In `BookLoansDialog.cpp`, add the includes:

```cpp
#include "ui/circulation/LoanCheckoutDialog.h"
#include "ui/circulation/LoanHistoryActions.h"

#include <QLabel>
```

In `buildUi`, after `layout->addWidget(m_table);`:

```cpp
    // Shown instead of an empty grid: a book nobody has borrowed is exactly
    // the book someone is about to, so the dialog says so and offers Loan.
    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setObjectName(QStringLiteral("loanHistoryEmpty"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setVisible(false);
    layout->addWidget(m_emptyLabel);

    m_actions = new LoanHistoryActions(m_repository, m_table, this);
    connect(m_actions, &LoanHistoryActions::loanRequested, this, &BookLoansDialog::lendACopy);
    connect(m_actions, &LoanHistoryActions::loansChanged, this, &BookLoansDialog::refresh);
```

and put the actions and the Close box on one row, replacing the bare
`layout->addWidget(closeBox);`:

```cpp
    auto* footer = new QHBoxLayout();
    footer->addWidget(m_actions, 1);
    footer->addWidget(closeBox, 0);
    layout->addLayout(footer);
```

(`#include <QHBoxLayout>`.)

In `retranslateUi`, after the header labels:

```cpp
    m_emptyLabel->setText(T("catalog.loanHistoryEmpty"));
    if (m_actions != nullptr) {
        m_actions->retranslateUi();
    }
```

At the end of `refresh`, after the loop:

```cpp
    const bool empty = loans.empty();
    m_table->setVisible(!empty);
    m_emptyLabel->setVisible(empty);
    m_actions->updateEnabled();
```

And the Loan click:

```cpp
void BookLoansDialog::lendACopy() {
    LoanScope scope;
    scope.bookId = m_bookId;
    scope.label = m_bookTitle;

    LoanCheckoutDialog dialog(m_repository, scope, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (const auto created = m_repository.createLoan(dialog.loanInput()); !created) {
        VLMS::showRepoError(this, created.error());
        return;
    }
    refresh();
}
```

- [ ] **Step 2: Unconditional Loans button on the Catalogue**

In `CatalogPage.h`, delete `void updateLoansButton();`.

In `CatalogPage.cpp`:

- delete the whole `updateLoansButton()` definition;
- delete the `updateLoansButton();` call at the top of `onSelectionChanged`;
- delete the `updateLoansButton();` call at the top of `refreshSelectedBookPreview`;
- in `buildUi`, delete `m_loansButton->setEnabled(false);` — the button starts enabled;
- in `showLoanHistory`, refresh the list after the dialog closes, since a loan changes the
  Available column:

```cpp
    BookLoansDialog dialog(m_circulation, bookId, qs(book->title), this);
    dialog.exec();
    refreshBooks();
    selectBookId(bookId);
```

- [ ] **Step 3: Build and run the UI suite**

```bash
cmake --build build -j$(nproc) >/dev/null 2>&1 && ctest --test-dir build -L ui --output-on-failure 2>&1 | tail -10
```

Expected: `test_ui_CatalogLoans.TheLoansButtonIsDisabledUntilABookWithLoansIsSelected`
**FAILS** — it asserts the old behaviour. That is correct; Task 7 replaces it. Everything
else passes.

- [ ] **Step 4: Commit**

```bash
git add applications/vlms/src/ui/catalog/BookLoansDialog.h \
        applications/vlms/src/ui/catalog/BookLoansDialog.cpp \
        applications/vlms/src/ui/catalog/CatalogPage.h \
        applications/vlms/src/ui/catalog/CatalogPage.cpp
git commit -m "$(cat <<'EOF'
Lend, extend and return from the book's history.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: The member dialog lends to the member

**Files:**
- Modify: `applications/vlms/src/ui/members/MemberLoansDialog.{h,cpp}`
- Modify: `applications/vlms/src/ui/members/MembersPage.{h,cpp}`

**Interfaces:**
- Consumes: the same three pieces as Task 5, with `members.loanHistoryEmpty`.
- Produces: a `Loans` button on the Members page enabled whenever a member is selected.

- [ ] **Step 1: Mirror Task 5 in the member dialog**

`MemberLoansDialog.h` gains exactly what `BookLoansDialog.h` gained, with the handler named
`lendABook`. `MemberLoansDialog.cpp` takes the same includes, the same `buildUi` additions,
the same `refresh` tail, `T("members.loanHistoryEmpty")` in `retranslateUi`, and:

```cpp
void MemberLoansDialog::lendABook() {
    LoanScope scope;
    scope.memberId = m_memberId;
    scope.label = m_memberName;

    LoanCheckoutDialog dialog(m_repository, scope, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (const auto created = m_repository.createLoan(dialog.loanInput()); !created) {
        VLMS::showRepoError(this, created.error());
        return;
    }
    refresh();
}
```

- [ ] **Step 2: Unconditional Loans button on the Members page**

In `MembersPage.h`, delete `void updateLoansButton();`.

In `MembersPage.cpp`:

- delete the `updateLoansButton()` definition;
- delete its three call sites (the end of the selection handler and the two places that
  disable the button alongside clearing the details);
- delete `m_loansButton->setEnabled(false);` in `buildUi`;
- delete the two remaining `m_loansButton->setEnabled(false);` lines that sat beside
  `clearMemberDetails()` — with no selection the button does nothing anyway, and
  `showLoanHistory` already returns early on `memberId <= 0`;
- in `showLoanHistory`, refresh after the dialog closes:

```cpp
    MemberLoansDialog dialog(m_circulationRepository, memberId, fullName, this);
    dialog.exec();
    refreshMembers();
    selectMemberId(memberId);
```

Check the exact names of the members page's refresh and re-select helpers before writing
this — `grep -n "void MembersPage::refresh\|selectMemberId" applications/vlms/src/ui/members/MembersPage.cpp` —
and use whatever it has rather than inventing a name.

- [ ] **Step 3: Build and run the UI suite**

```bash
cmake --build build -j$(nproc) >/dev/null 2>&1 && ctest --test-dir build -L ui --output-on-failure 2>&1 | tail -10
```

Expected: the one known failure from Task 5 and nothing new.

- [ ] **Step 4: Commit**

```bash
git add applications/vlms/src/ui/members/MemberLoansDialog.h \
        applications/vlms/src/ui/members/MemberLoansDialog.cpp \
        applications/vlms/src/ui/members/MembersPage.h \
        applications/vlms/src/ui/members/MembersPage.cpp
git commit -m "$(cat <<'EOF'
Lend, extend and return from the member's history.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: The tests

**Files:**
- Modify: `applications/vlms/test/src/test_catalog_loans.cpp`
- Create: `applications/vlms/test/src/test_member_loans_actions.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt`

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Replace the obsolete catalogue test**

In `test_catalog_loans.cpp`, replace `TheLoansButtonIsDisabledUntilABookWithLoansIsSelected`
entirely with:

```cpp
TEST_F(test_ui_CatalogLoans, TheLoansButtonOpensEvenWithNoHistory)
{
    BookSeed untouched = uniqueBookSeed(1);
    untouched.title = "Untouched Book";
    ASSERT_GT(seedBook(*m_db, untouched), 0);

    openPage();
    auto* loans = buttonWithText(m_page.get(), T("catalog.loans"));
    ASSERT_NE(loans, nullptr);

    selectRowWithTitle(QStringLiteral("Untouched Book"));
    EXPECT_TRUE(loans->isEnabled()) << "a book never borrowed is the one about to be";
}

TEST_F(test_ui_CatalogLoans, AnEmptyHistorySaysSoAndHidesTheGrid)
{
    BookSeed untouched = uniqueBookSeed(1);
    untouched.title = "Untouched Book";
    const std::int64_t bookId = seedBook(*m_db, untouched);
    ASSERT_GT(bookId, 0);

    BookLoansDialog dialog(*m_circulation, bookId, QStringLiteral("Untouched Book"));
    auto* empty = dialog.findChild<QLabel*>(QStringLiteral("loanHistoryEmpty"));
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->isVisibleTo(&dialog));
    EXPECT_EQ(empty->text(), T("catalog.loanHistoryEmpty"));

    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_FALSE(table->isVisibleTo(&dialog)) << "an empty grid says nothing";
}

TEST_F(test_ui_CatalogLoans, TheScopedCheckoutOffersOnlyThisBooksFreeCopies)
{
    BookSeed wanted = uniqueBookSeed(1);
    wanted.title = "Wanted Book";
    wanted.initialCopyCount = 2;
    const std::int64_t wantedId = seedBook(*m_db, wanted);
    ASSERT_GT(wantedId, 0);

    // A second book with free copies: without the scope these would be offered.
    BookSeed other = uniqueBookSeed(2);
    other.title = "Other Book";
    other.initialCopyCount = 2;
    ASSERT_GT(seedBook(*m_db, other), 0);

    LoanScope scope;
    scope.bookId = wantedId;
    scope.label = QStringLiteral("Wanted Book");
    LoanCheckoutDialog dialog(*m_circulation, scope);

    const auto combos = dialog.findChildren<QComboBox*>();
    ASSERT_FALSE(combos.isEmpty());
    QComboBox* copies = combos.last();
    EXPECT_EQ(copies->count(), 2) << "only Wanted Book's two copies";
    for (int i = 0; i < copies->count(); ++i) {
        EXPECT_TRUE(copies->itemText(i).contains(QStringLiteral("Wanted Book")))
            << copies->itemText(i).toStdString();
    }
}

TEST_F(test_ui_CatalogLoans, ExtendAndReturnFollowTheSelectedRow)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Two States";
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copies.size(), 2U);
    // Row order is the query's; find each row by its borrow date below.
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(0),
                            "2025-01-10", "2025-01-24", "2025-01-20"), 0);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(1), "2025-02-10", "2025-02-24"), 0);

    BookLoansDialog dialog(*m_circulation, bookId, QStringLiteral("Two States"));
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 2);

    auto* extend = buttonWithText(&dialog, T("circulation.extend"));
    auto* ret = buttonWithText(&dialog, T("circulation.return"));
    ASSERT_NE(extend, nullptr);
    ASSERT_NE(ret, nullptr);

    const auto rowWithBorrowDate = [table](const QString& date) {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 2)->text() == date) {
                return row;
            }
        }
        return -1;
    };

    const int openRow = rowWithBorrowDate(QStringLiteral("2025-02-10"));
    ASSERT_GE(openRow, 0);
    table->setCurrentCell(openRow, 0);
    EXPECT_TRUE(extend->isEnabled());
    EXPECT_TRUE(ret->isEnabled());

    const int returnedRow = rowWithBorrowDate(QStringLiteral("2025-01-10"));
    ASSERT_GE(returnedRow, 0);
    table->setCurrentCell(returnedRow, 0);
    EXPECT_FALSE(extend->isEnabled()) << "a returned loan cannot be extended";
    EXPECT_FALSE(ret->isEnabled());
}
```

Add the includes it needs at the top of the file:

```cpp
#include "ui/circulation/LoanCheckoutDialog.h"

#include <QComboBox>
#include <QLabel>
```

- [ ] **Step 2: The member-side test**

Create `applications/vlms/test/src/test_member_loans_actions.cpp`:

```cpp
#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/circulation/LoanCheckoutDialog.h"
#include "ui/members/MemberLoansDialog.h"

#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/MemberRepository.h>
#include <VLMS/Core/Strings.h>
#include "QtBridge.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

#include <gtest/gtest.h>

#include <cstdint>
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

class test_ui_MemberLoansActions : public ::testing::Test {
protected:
    void SetUp() override
    {
        Locale::setCode("en");
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());
        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_GT(m_memberId, 0);
    }

    void TearDown() override
    {
        m_circulation.reset();
        m_db.reset();
        Locale::setCode("en");
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::int64_t m_memberId = 0;
};

TEST_F(test_ui_MemberLoansActions, AnEmptyHistorySaysSo)
{
    MemberLoansDialog dialog(*m_circulation, m_memberId, QStringLiteral("Amina Ben Salah"));
    auto* empty = dialog.findChild<QLabel*>(QStringLiteral("loanHistoryEmpty"));
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->isVisibleTo(&dialog));
    EXPECT_EQ(empty->text(), T("members.loanHistoryEmpty"));
}

TEST_F(test_ui_MemberLoansActions, TheDialogCarriesTheThreeActions)
{
    MemberLoansDialog dialog(*m_circulation, m_memberId, QStringLiteral("Amina Ben Salah"));
    EXPECT_NE(buttonWithText(&dialog, T("circulation.checkout")), nullptr);
    EXPECT_NE(buttonWithText(&dialog, T("circulation.extend")), nullptr);
    EXPECT_NE(buttonWithText(&dialog, T("circulation.return")), nullptr);
}

TEST_F(test_ui_MemberLoansActions, TheScopedCheckoutFixesTheMemberAndOffersEveryBook)
{
    BookSeed first = uniqueBookSeed(1);
    first.title = "First Book";
    ASSERT_GT(seedBook(*m_db, first), 0);
    BookSeed second = uniqueBookSeed(2);
    second.title = "Second Book";
    ASSERT_GT(seedBook(*m_db, second), 0);

    LoanScope scope;
    scope.memberId = m_memberId;
    scope.label = QStringLiteral("Amina Ben Salah");
    LoanCheckoutDialog dialog(*m_circulation, scope);

    // One combo only: the copy one. The member is a label now.
    const auto combos = dialog.findChildren<QComboBox*>();
    ASSERT_EQ(combos.size(), 1);
    EXPECT_EQ(combos.first()->count(), 2) << "both books' copies are on offer";

    bool namesTheMember = false;
    for (QLabel* label : dialog.findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("Amina Ben Salah"))) {
            namesTheMember = true;
        }
    }
    EXPECT_TRUE(namesTheMember) << "the fixed member is named in the dialog";
}

TEST_F(test_ui_MemberLoansActions, TheHistoryShowsWhatTheMemberBorrowed)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Borrowed Book";
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_FALSE(copies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(0), "2025-01-10", "2025-01-24"), 0);

    // Another member's loan of another book: it must not appear here.
    const std::int64_t otherMember = seedMember(*m_db, uniqueMemberSeed(2));
    ASSERT_GT(otherMember, 0);
    BookSeed other = uniqueBookSeed(2);
    other.title = "Other Book";
    const std::int64_t otherBook = seedBook(*m_db, other);
    ASSERT_GT(otherBook, 0);
    const auto otherCopies = copyIdsOf(*m_db, otherBook);
    ASSERT_FALSE(otherCopies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, otherMember, otherCopies.at(0), "2025-02-10", "2025-02-24"), 0);

    MemberLoansDialog dialog(*m_circulation, m_memberId, QStringLiteral("Amina Ben Salah"));
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("Borrowed Book"));
}

}  // namespace
```

Register it in `applications/vlms/test/CMakeLists.txt` after
`src/test_catalog_loans.cpp`:

```cmake
        src/test_member_loans_actions.cpp
```

- [ ] **Step 3: Run everything**

```bash
cmake -S . -B build >/dev/null && cmake --build build -j$(nproc) 2>&1 | grep -E "error" | head -5
ctest --test-dir build --output-on-failure 2>&1 | tail -12
```

Expected: `100% tests passed`.

- [ ] **Step 4: Prove the scoping tests can fail**

Temporarily change `m_scope.bookId` to `0` in `LoanCheckoutDialog::refreshCopies`, rebuild,
and re-run. `TheScopedCheckoutOffersOnlyThisBooksFreeCopies` must FAIL. Restore and re-run
to green. Last round a scoping test that could not fail shipped green; this step is the
guard against repeating it.

- [ ] **Step 5: Commit**

```bash
git add applications/vlms/test/src/test_catalog_loans.cpp \
        applications/vlms/test/src/test_member_loans_actions.cpp \
        applications/vlms/test/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Guard the loan-history actions.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: See it, then write it down

**Files:** `CLAUDE.md` only.

- [ ] **Step 1: Render the widgets offscreen**

Check `pgrep -x vlms` first. If the user's own instance is running against the live
database, do **not** drive the desktop — synthetic clicks are global and would land on real
records. Render instead: add a throwaway `zz_shot_tmp.cpp` to the `test_vlms_ui`
source list (back up `applications/vlms/test/CMakeLists.txt` first), grab with
`widget.grab().save(...)` under `QT_QPA_PLATFORM=offscreen`, then delete the file and
restore the CMakeLists.

Grab, in English light and Arabic dark at least:

1. `BookLoansDialog` on a book with history — the three buttons and Close on one row.
2. `BookLoansDialog` on a book with none — the empty line, no grid, Loan live.
3. `LoanCheckoutDialog` scoped to a book — no copy-search row, copies all one book.
4. `LoanCheckoutDialog` scoped to a member — no member rows, the name as text.
5. `MemberLoansDialog` with history.

Read each PNG back and confirm the RTL round mirrors rather than overlaps.

- [ ] **Step 2: Write the session log entry**

Add a dated entry at the top of the Session Log in `CLAUDE.md`: what the Loans buttons now
do, that `updateLoansButton` is gone from both pages and why, `LoanScope` and the one
clause behind it, `LoanHistoryActions` as the shared row, and anything surprising from
step 1.

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "$(cat <<'EOF'
Note the loan-history actions in the session log.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
EOF
)"
```
