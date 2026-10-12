# Frontend Redesign Phase 1: Foundations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce the shell composition root and the three cross-cutting workflow interfaces (`DataEvents`, `Navigator`, `ErrorPresenter`, `DialogFactory`) so UI pages no longer look up `Application` via `qobject_cast`, no longer refresh live lists through `ArchivePage::recordRestored` → `retranslateUi()`, and no longer jump pages through `MainWindow` lambdas on `memberLoansRequested`.

**Architecture:** `Application` stays at `applications/vlms/src/Application.*` for this phase (folder move to `ui/shell/` is Phase 5) and becomes the composition root that owns `DataEvents`, a `QtErrorPresenter`, and a `QtDialogFactory`. New interfaces and Qt wrappers live under `ui/workflows/`; `Navigator` lives under `ui/shell/` and is implemented by `MainWindow`. Pages still call repositories that write until Phase 2; Phase 1 only adds the interfaces, wires the shell, and replaces the two banned cross-page patterns.

**Tech Stack:** C++17, Qt 6 Widgets, CMake ≥ 3.21, GoogleTest via the existing `test_vlms_ui` target, existing `VLMS::Core` / `VLMS::Repositories` libraries (unchanged).

**Spec:** `docs/superpowers/specs/2026-09-29-frontend-redesign-design.md` (Phase 1 only).

**Worktree:** `<repo>/.worktrees/redesign-1-foundations` on branch `redesign/1-foundations` (based on `Beta`).

## Global Constraints

- Dependencies point downwards only (shell → views → presentation → workflows → services → repositories).
- Only workflows write — **Phase 1 exception:** pages still call repository write methods until Phase 2; Phase 1 introduces the interfaces and shell wiring only, and does not move saves into workflows.
- Nothing calls `qobject_cast<Application*>(QApplication::instance())` (or `qApp`) for language, theme, or repositories in the UI shell after this phase.
- If the database fails to open, `Application` builds no specs/workflows and `MainWindow` shows today's placeholder pages (already true; keep it; do not invent new placeholder behaviour).
- `Strings::t()` keys stay unchanged except new menu/shortcut keys — Phase 1 adds **no** new string keys unless a compile or test forces an unavoidable label (prefer reusing existing keys).
- Backend libraries and the database schema do not change.
- Do not invent features beyond Phase 1. No `ListSpec`, no workflows (`BookWorkflow` etc.), no `PagedQueryModel`, no dialog Lookups refactor.
- Commits in task steps are **optional** for SDD hygiene. The controller may skip commits per the user's commit rule; still leave the commit step in each task so a human or SDD run can use it.

---

## File map

| Path | Responsibility |
|---|---|
| `applications/vlms/src/ui/workflows/DataEvents.h` | `Entity`, `EntitySet`, `DataEvents` signal bus |
| `applications/vlms/src/ui/workflows/DataEvents.cpp` | MOC translation unit for `DataEvents` |
| `applications/vlms/src/ui/workflows/ErrorPresenter.h` | `ErrorPresenter` interface |
| `applications/vlms/src/ui/workflows/QtErrorPresenter.h` / `.cpp` | Wraps `VLMS::showRepoError` |
| `applications/vlms/src/ui/workflows/DialogForms.h` | Form structs + `BulkPlan` used by `DialogFactory` |
| `applications/vlms/src/ui/workflows/DialogFactory.h` | `DialogFactory` interface |
| `applications/vlms/src/ui/workflows/QtDialogFactory.h` / `.cpp` | Opens today's dialogs; uses `UiHelpers` for message boxes |
| `applications/vlms/src/ui/shell/Navigator.h` | `PageId`, `ArchiveType`, `Navigator` interface |
| `applications/vlms/src/Application.h` / `.cpp` | Composition root: owns and exposes `DataEvents`, `ErrorPresenter`, `DialogFactory` |
| `applications/vlms/src/ui/MainWindow.h` / `.cpp` | Implements `Navigator`; takes `Application&`; no `qobject_cast<Application*>`; subscribes to `DataEvents` |
| `applications/vlms/src/ui/archive/ArchivePage.h` / `.cpp` | Takes `DataEvents&`; emits `changed` instead of `recordRestored`; gains `setType` for Navigator |
| `applications/vlms/src/ui/members/MembersPage.h` / `.cpp` | Takes `Navigator&`; calls `showMemberLoans`; drops `memberLoansRequested` |
| `applications/vlms/src/main.cpp` | Passes `Application&` into `MainWindow` |
| `applications/vlms/CMakeLists.txt` | Lists every new `.cpp` / `.h` in `vlms_ui` |
| `applications/vlms/test/src/test_data_events.cpp` | Unit tests for `EntitySet` / `DataEvents` |
| `applications/vlms/test/src/test_error_presenter.cpp` | Recorder + `QtErrorPresenter` smoke |
| `applications/vlms/test/src/test_navigator_fakes.cpp` | Fake navigator recording |
| `applications/vlms/test/src/RecordingErrorPresenter.h` | Test double shared by later workflow tests |
| `applications/vlms/test/src/FakeNavigator.h` | Test double for `Navigator` |
| `applications/vlms/test/CMakeLists.txt` | Adds the new test sources to `test_vlms_ui` |
| `applications/vlms/test/src/test_member_removal.cpp` (and any test that constructs `MembersPage` / `ArchivePage`) | Update constructors for new deps |

**Stays put (Phase 5 moves):** `Application.*` at `src/`, `MainWindow.*` at `ui/`, `Theme.*` at `ui/` (services later).

**Not created in Phase 1:** `BookWorkflow`, `ListSpec`, `PagedQueryModel`, `Lookups`, `OcrService`, fake dialog factories for smoke (Phase 6 / later).

---

### Task 1: DataEvents + Entity / EntitySet

**Files:**
- Create: `applications/vlms/src/ui/workflows/DataEvents.h`
- Create: `applications/vlms/src/ui/workflows/DataEvents.cpp`
- Create: `applications/vlms/test/src/test_data_events.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (add the two new sources to `vlms_ui`)
- Modify: `applications/vlms/test/CMakeLists.txt` (add `src/test_data_events.cpp`)

**Interfaces:**
- Consumes: Qt `QObject`, `QFlags`, `QList`
- Produces:

```cpp
namespace VLMS {

enum class Entity {
    Books = 1,
    Copies = 2,
    Members = 4,
    Loans = 8,
    Categories = 16,
};
Q_DECLARE_FLAGS(EntitySet, Entity)

class DataEvents : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

signals:
    void changed(VLMS::EntitySet what, QList<qint64> ids);
};

}  // namespace VLMS

Q_DECLARE_OPERATORS_FOR_FLAGS(VLMS::EntitySet)
```

- [ ] **Step 1: Write the failing test**

Create `applications/vlms/test/src/test_data_events.cpp`:

```cpp
#include "ui/workflows/DataEvents.h"

#include <QList>
#include <QSignalSpy>

#include <gtest/gtest.h>

TEST(DataEvents, FlagsCombineWithOr)
{
    const VLMS::EntitySet set = VLMS::Entity::Books | VLMS::Entity::Copies;
    EXPECT_TRUE(set.testFlag(VLMS::Entity::Books));
    EXPECT_TRUE(set.testFlag(VLMS::Entity::Copies));
    EXPECT_FALSE(set.testFlag(VLMS::Entity::Members));
}

TEST(DataEvents, ChangedEmitsWhatAndIds)
{
    VLMS::DataEvents events;
    QSignalSpy spy(&events, &VLMS::DataEvents::changed);

    const QList<qint64> ids{7, 9};
    emit events.changed(VLMS::Entity::Members | VLMS::Entity::Loans, ids);

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).value<VLMS::EntitySet>(),
              VLMS::Entity::Members | VLMS::Entity::Loans);
    EXPECT_EQ(spy.at(0).at(1).value<QList<qint64>>(), ids);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
```

Expected: FAIL — `ui/workflows/DataEvents.h` not found / target missing sources.

- [ ] **Step 3: Write headers and MOC unit; register in CMake**

`applications/vlms/src/ui/workflows/DataEvents.h` — exact content from **Produces** above (include `<QObject>`, `<QList>`, `<QtGlobal>` / `QFlags`).

`applications/vlms/src/ui/workflows/DataEvents.cpp`:

```cpp
#include "ui/workflows/DataEvents.h"

#include "moc_DataEvents.cpp"
```

(If this tree uses AUTOMOC only and never includes `moc_*.cpp`, omit the include and rely on AUTOMOC like `MainWindow.cpp` — match neighbouring `vlms_ui` files. Prefer AUTOMOC-only if `MainWindow.cpp` has no `moc_` include.)

In `applications/vlms/CMakeLists.txt`, add under the `add_library(vlms_ui STATIC` source list:

```cmake
    src/ui/workflows/DataEvents.cpp
    src/ui/workflows/DataEvents.h
```

In `applications/vlms/test/CMakeLists.txt`, add `src/test_data_events.cpp` to the `SRC` list of `test_vlms_ui`.

- [ ] **Step 4: Run tests and make sure they pass**

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
cd build && ctest -R 'DataEvents' --output-on-failure
```

Expected: PASS for `DataEvents.*`.

- [ ] **Step 5: Commit (optional)**

```bash
git add applications/vlms/src/ui/workflows/DataEvents.h \
        applications/vlms/src/ui/workflows/DataEvents.cpp \
        applications/vlms/test/src/test_data_events.cpp \
        applications/vlms/CMakeLists.txt \
        applications/vlms/test/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): add DataEvents entity change bus

EOF
)"
```

---

### Task 2: ErrorPresenter + QtErrorPresenter + recorder

**Files:**
- Create: `applications/vlms/src/ui/workflows/ErrorPresenter.h`
- Create: `applications/vlms/src/ui/workflows/QtErrorPresenter.h`
- Create: `applications/vlms/src/ui/workflows/QtErrorPresenter.cpp`
- Create: `applications/vlms/test/src/RecordingErrorPresenter.h`
- Create: `applications/vlms/test/src/test_error_presenter.cpp`
- Modify: `applications/vlms/CMakeLists.txt`
- Modify: `applications/vlms/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `VLMS::Core::Error` (`<VLMS/Core/Result.h>`), `VLMS::showRepoError` from `ui/UiHelpers.h`
- Produces:

```cpp
// ErrorPresenter.h
namespace VLMS {

class ErrorPresenter {
public:
    virtual ~ErrorPresenter() = default;
    virtual void show(QWidget* parent, const Core::Error& error) = 0;
};

}  // namespace VLMS

// QtErrorPresenter.h
namespace VLMS {

class QtErrorPresenter final : public ErrorPresenter {
public:
    void show(QWidget* parent, const Core::Error& error) override;
};

}  // namespace VLMS

// RecordingErrorPresenter.h (tests only)
namespace VLMS {

class RecordingErrorPresenter final : public ErrorPresenter {
public:
    struct Call {
        QWidget* parent = nullptr;
        Core::Error error;
    };
    QList<Call> calls;

    void show(QWidget* parent, const Core::Error& error) override
    {
        calls.push_back(Call{parent, error});
    }
};

}  // namespace VLMS
```

- [ ] **Step 1: Write the failing test**

```cpp
#include "RecordingErrorPresenter.h"
#include "ui/workflows/ErrorPresenter.h"
#include "ui/workflows/QtErrorPresenter.h"

#include <VLMS/Core/Result.h>

#include <gtest/gtest.h>

TEST(ErrorPresenter, RecordingCapturesCall)
{
    VLMS::RecordingErrorPresenter presenter;
    VLMS::Core::Error err;
    err.key = "error.sql";
    err.detail = "boom";

    presenter.show(nullptr, err);

    ASSERT_EQ(presenter.calls.size(), 1);
    EXPECT_EQ(presenter.calls[0].parent, nullptr);
    EXPECT_EQ(presenter.calls[0].error.key, "error.sql");
    EXPECT_EQ(presenter.calls[0].error.detail, "boom");
}

TEST(ErrorPresenter, QtPresenterIsErrorPresenter)
{
    VLMS::QtErrorPresenter presenter;
    VLMS::ErrorPresenter* asInterface = &presenter;
    EXPECT_NE(asInterface, nullptr);
}
```

Do **not** call `QtErrorPresenter::show` with a real modal in unit tests (hangs). Behaviour is covered by existing `showRepoError` usage; this task only wires the wrapper.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
```

Expected: FAIL — missing headers.

- [ ] **Step 3: Implement**

`ErrorPresenter.h` and `QtErrorPresenter.h` as in **Produces**.

`QtErrorPresenter.cpp`:

```cpp
#include "ui/workflows/QtErrorPresenter.h"

#include "ui/UiHelpers.h"

namespace VLMS {

void QtErrorPresenter::show(QWidget* parent, const Core::Error& error)
{
    showRepoError(parent, error);
}

}  // namespace VLMS
```

Register `QtErrorPresenter.cpp` / `.h` and `ErrorPresenter.h` in `vlms_ui`. Add `test_error_presenter.cpp` to `test_vlms_ui` (header-only recorder needs no CMake entry beyond the test `.cpp`).

- [ ] **Step 4: Run tests**

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
cd build && ctest -R 'ErrorPresenter' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit (optional)**

```bash
git add applications/vlms/src/ui/workflows/ErrorPresenter.h \
        applications/vlms/src/ui/workflows/QtErrorPresenter.h \
        applications/vlms/src/ui/workflows/QtErrorPresenter.cpp \
        applications/vlms/test/src/RecordingErrorPresenter.h \
        applications/vlms/test/src/test_error_presenter.cpp \
        applications/vlms/CMakeLists.txt \
        applications/vlms/test/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): add ErrorPresenter wrapping showRepoError

EOF
)"
```

---

### Task 3: Navigator interface + PageId / ArchiveType

**Files:**
- Create: `applications/vlms/src/ui/shell/Navigator.h`
- Create: `applications/vlms/test/src/FakeNavigator.h`
- Create: `applications/vlms/test/src/test_navigator_fakes.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (header-only; list `Navigator.h` for IDE/install consistency like other headers)
- Modify: `applications/vlms/test/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from Tasks 1–2
- Produces:

```cpp
#pragma once

#include <QString>

#include <cstdint>

namespace VLMS {

enum class PageId {
    Catalog,
    Members,
    Circulation,
    Archive,
    Metrics,
};

enum class ArchiveType {
    Members,
    Books,
    Copies,
    Loans,
};

class Navigator {
public:
    virtual ~Navigator() = default;
    virtual void show(PageId page) = 0;
    virtual void showMemberLoans(const QString& membershipNumber) = 0;
    virtual void showBook(qint64 id) = 0;
    virtual void showArchive(ArchiveType type) = 0;
};

}  // namespace VLMS
```

`FakeNavigator.h`:

```cpp
#pragma once

#include "ui/shell/Navigator.h"

#include <QList>
#include <QString>

namespace VLMS {

class FakeNavigator final : public Navigator {
public:
    QList<PageId> shownPages;
    QList<QString> memberLoans;
    QList<qint64> books;
    QList<ArchiveType> archives;

    void show(PageId page) override { shownPages.push_back(page); }
    void showMemberLoans(const QString& membershipNumber) override
    {
        memberLoans.push_back(membershipNumber);
    }
    void showBook(qint64 id) override { books.push_back(id); }
    void showArchive(ArchiveType type) override { archives.push_back(type); }
};

}  // namespace VLMS
```

- [ ] **Step 1: Write the failing test**

```cpp
#include "FakeNavigator.h"

#include <gtest/gtest.h>

TEST(Navigator, FakeRecordsCalls)
{
    VLMS::FakeNavigator nav;
    nav.show(VLMS::PageId::Circulation);
    nav.showMemberLoans(QStringLiteral("M-1"));
    nav.showBook(42);
    nav.showArchive(VLMS::ArchiveType::Copies);

    ASSERT_EQ(nav.shownPages.size(), 1);
    EXPECT_EQ(nav.shownPages[0], VLMS::PageId::Circulation);
    ASSERT_EQ(nav.memberLoans.size(), 1);
    EXPECT_EQ(nav.memberLoans[0], QStringLiteral("M-1"));
    ASSERT_EQ(nav.books.size(), 1);
    EXPECT_EQ(nav.books[0], 42);
    ASSERT_EQ(nav.archives.size(), 1);
    EXPECT_EQ(nav.archives[0], VLMS::ArchiveType::Copies);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
```

Expected: FAIL — missing `Navigator.h`.

- [ ] **Step 3: Add `Navigator.h`, `FakeNavigator.h`, CMake entries, test source**

- [ ] **Step 4: Run tests**

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
cd build && ctest -R 'Navigator' --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit (optional)**

```bash
git add applications/vlms/src/ui/shell/Navigator.h \
        applications/vlms/test/src/FakeNavigator.h \
        applications/vlms/test/src/test_navigator_fakes.cpp \
        applications/vlms/CMakeLists.txt \
        applications/vlms/test/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): add Navigator interface and page ids

EOF
)"
```

---

### Task 4: DialogFactory interface + DialogForms + QtDialogFactory

**Files:**
- Create: `applications/vlms/src/ui/workflows/DialogForms.h`
- Create: `applications/vlms/src/ui/workflows/DialogFactory.h`
- Create: `applications/vlms/src/ui/workflows/QtDialogFactory.h`
- Create: `applications/vlms/src/ui/workflows/QtDialogFactory.cpp`
- Modify: `applications/vlms/CMakeLists.txt`

**Interfaces:**
- Consumes: existing dialogs (`BookEditorDialog`, `MemberEditorDialog`, `LoanCheckoutDialog` + `LoanScope`, `LoanReturnDialog`, `LoanExtendDialog`, `CategoryManagerDialog`), `runBulkAction` / `BulkActionTexts` / `BulkRow` from `ui/BulkAction.h`, `VLMS::askYesNo`, `VLMS::showWarningWithAction` from `ui/UiHelpers.h`, repository references
- Produces:

```cpp
// DialogForms.h — thin structs over today's dialog result types
#pragma once

#include "ui/BulkAction.h"

#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Repositories/LoanTypes.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QString>
#include <QWidget>

#include <optional>
#include <vector>

struct LoanScope;  // from LoanCheckoutDialog.h — include that header in cpp/impl

namespace VLMS {

struct BookForm {
    Repositories::BookInput book;
    std::vector<Repositories::BookCopyInput> copies;
    QString coverSourcePath;
    bool coverChanged = false;
    /// Non-zero when editing an existing title; 0 when adding.
    qint64 existingBookId = 0;
};

struct MemberForm {
    Repositories::MemberInput member;
    QString photoSourcePath;
    QString idImageSourcePath;
    bool photoChanged = false;
    bool idImageChanged = false;
    bool photoRemoved = false;
    bool idImageRemoved = false;
    qint64 existingMemberId = 0;
};

struct LoanForm {
    Repositories::LoanInput loan;
    /// Checkout scope passed into LoanCheckoutDialog (book and/or member).
    qint64 scopeBookId = 0;
    qint64 scopeMemberId = 0;
    QString scopeLabel;
};

struct ReturnForm {
    QString returnedAt;
    QString notes;
    Repositories::LoanRecord loan;  // dialog needs the open loan to display
};

struct ExtendForm {
    QString dueAt;
    Repositories::LoanRecord loan;
};

/// Category manager mutates through the repository today; Phase 1 only needs
/// "open the dialog". Empty struct kept so the factory signature matches the
/// design's Form list for Phase 2 workflows.
struct CategoryEdits {};

struct BulkPlan {
    BulkActionTexts texts;
    QList<BulkRow> rows;
    /// Already-translated intro listing blocked rows (may be empty).
    QString blockedIntro;
};

}  // namespace VLMS
```

```cpp
// DialogFactory.h
#pragma once

#include "ui/workflows/DialogForms.h"

#include <optional>

class QWidget;

namespace VLMS {

class DialogFactory {
public:
    virtual ~DialogFactory() = default;

    virtual std::optional<BookForm> editBook(QWidget* parent, const BookForm& initial) = 0;
    virtual std::optional<MemberForm> editMember(QWidget* parent, const MemberForm& initial) = 0;
    virtual std::optional<LoanForm> checkout(QWidget* parent, const LoanForm& initial) = 0;
    virtual std::optional<ReturnForm> returnLoan(QWidget* parent, const ReturnForm& initial) = 0;
    virtual std::optional<ExtendForm> extendLoan(QWidget* parent, const ExtendForm& initial) = 0;
    /// Opens CategoryManagerDialog. Returns nullopt if the dialog was not shown;
    /// returns CategoryEdits{} after the dialog closes (in-place edits already saved).
    virtual std::optional<CategoryEdits> manageCategories(QWidget* parent) = 0;

    /// Yes = proceed. Mirrors askYesNo wording via BulkPlan texts; Phase 1
    /// implementation asks once with askYesNo using texts.title / a composed body.
    virtual bool confirmBulk(QWidget* parent, const BulkPlan& plan) = 0;
    virtual bool askYesNo(QWidget* parent, const QString& title, const QString& text) = 0;
    virtual bool warnWithAction(QWidget* parent,
                                const QString& title,
                                const QString& text,
                                const QString& actionText) = 0;
};

}  // namespace VLMS
```

```cpp
// QtDialogFactory.h
namespace VLMS {

class QtDialogFactory final : public DialogFactory {
public:
    QtDialogFactory(Repositories::CatalogRepository& catalog,
                    Repositories::MemberRepository& members,
                    Repositories::CirculationRepository& circulation);

    std::optional<BookForm> editBook(QWidget* parent, const BookForm& initial) override;
    std::optional<MemberForm> editMember(QWidget* parent, const MemberForm& initial) override;
    std::optional<LoanForm> checkout(QWidget* parent, const LoanForm& initial) override;
    std::optional<ReturnForm> returnLoan(QWidget* parent, const ReturnForm& initial) override;
    std::optional<ExtendForm> extendLoan(QWidget* parent, const ExtendForm& initial) override;
    std::optional<CategoryEdits> manageCategories(QWidget* parent) override;
    bool confirmBulk(QWidget* parent, const BulkPlan& plan) override;
    bool askYesNo(QWidget* parent, const QString& title, const QString& text) override;
    bool warnWithAction(QWidget* parent,
                        const QString& title,
                        const QString& text,
                        const QString& actionText) override;

private:
    Repositories::CatalogRepository& m_catalog;
    Repositories::MemberRepository& m_members;
    Repositories::CirculationRepository& m_circulation;
};

}  // namespace VLMS
```

- [ ] **Step 1: Write a compile-only / type test**

Add to `test_error_presenter.cpp` or a small `test_dialog_factory_types.cpp`:

```cpp
#include "ui/workflows/DialogFactory.h"
#include "ui/workflows/QtDialogFactory.h"

#include <type_traits>

#include <gtest/gtest.h>

TEST(DialogFactory, QtFactoryImplementsInterface)
{
    static_assert(std::is_base_of_v<VLMS::DialogFactory, VLMS::QtDialogFactory>);
    SUCCEED();
}
```

Do not exec real modals here.

- [ ] **Step 2: Run to verify missing types fail the build**

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
```

Expected: FAIL — missing `DialogFactory.h`.

- [ ] **Step 3: Implement QtDialogFactory methods**

`editBook` sketch (mirror for others):

```cpp
std::optional<BookForm> QtDialogFactory::editBook(QWidget* parent, const BookForm& initial)
{
    std::unique_ptr<BookEditorDialog> dialog;
    if (initial.existingBookId > 0) {
        const auto loaded = m_catalog.getBook(initial.existingBookId);
        if (!loaded) {
            return std::nullopt;
        }
        dialog = std::make_unique<BookEditorDialog>(m_catalog, *loaded, parent);
    } else {
        dialog = std::make_unique<BookEditorDialog>(m_catalog, parent);
    }
    if (dialog->exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    BookForm out = initial;
    out.book = dialog->bookInput();
    out.copies = dialog->copyInputs();
    out.coverSourcePath = dialog->coverSourcePath();
    out.coverChanged = dialog->coverChanged();
    return out;
}
```

`editMember`: same pattern with `MemberEditorDialog` and `m_members`.

`checkout`:

```cpp
LoanScope scope;
scope.bookId = initial.scopeBookId;
scope.memberId = initial.scopeMemberId;
scope.label = initial.scopeLabel;
LoanCheckoutDialog dialog(m_circulation, scope, parent);
if (dialog.exec() != QDialog::Accepted) {
    return std::nullopt;
}
LoanForm out = initial;
out.loan = dialog.loanInput();
return out;
```

`returnLoan` / `extendLoan`: construct with `initial.loan`, map `returnedAt`/`notes` or `dueAt`.

`manageCategories`:

```cpp
CategoryManagerDialog dialog(m_catalog, parent);
dialog.exec();
return CategoryEdits{};
```

`confirmBulk` (Phase 1 thin wrap — full BulkRunner stays Phase 2):

```cpp
bool QtDialogFactory::confirmBulk(QWidget* parent, const BulkPlan& plan)
{
    QString body = plan.blockedIntro;
    if (body.isEmpty()) {
        body = plan.texts.verb;  // sufficient placeholder until BulkRunner owns composition
    }
    return askYesNo(parent, plan.texts.title, body);
}
```

Prefer composing with existing `bulk.confirm` / `bulk.confirmOne` string keys via `T(...)` the same way `runBulkAction` does if you read that function — **copy the confirmation message construction from `BulkAction.cpp`** into `confirmBulk` so wording stays identical. Do not invent new keys.

`askYesNo` → `VLMS::askYesNo`.  
`warnWithAction` → `VLMS::showWarningWithAction`.

Register all new files in `vlms_ui`.

- [ ] **Step 4: Build and run the type test**

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
cd build && ctest -R 'DialogFactory' --output-on-failure
```

Expected: PASS. Full `test_vlms_ui` should still build (factory unused by pages yet).

- [ ] **Step 5: Commit (optional)**

```bash
git add applications/vlms/src/ui/workflows/DialogForms.h \
        applications/vlms/src/ui/workflows/DialogFactory.h \
        applications/vlms/src/ui/workflows/QtDialogFactory.h \
        applications/vlms/src/ui/workflows/QtDialogFactory.cpp \
        applications/vlms/CMakeLists.txt \
        applications/vlms/test/CMakeLists.txt \
        applications/vlms/test/src/test_dialog_factory_types.cpp
git commit -m "$(cat <<'EOF'
feat(ui): add DialogFactory wrapping existing dialogs

EOF
)"
```

---

### Task 5: Wire Application as composition root; MainWindow takes Application&

**Files:**
- Modify: `applications/vlms/src/Application.h`
- Modify: `applications/vlms/src/Application.cpp`
- Modify: `applications/vlms/src/ui/MainWindow.h`
- Modify: `applications/vlms/src/ui/MainWindow.cpp`
- Modify: `applications/vlms/src/main.cpp`
- Modify: `applications/vlms/src/ui/LanguageSelector.h` (comment only: stop claiming MainWindow must qobject_cast)

**Interfaces:**
- Consumes: `DataEvents`, `QtErrorPresenter`, `QtDialogFactory`, `Navigator` (MainWindow will implement in Task 7; for this task MainWindow may not yet inherit Navigator — either inherit now with stub methods that call existing `showPage`, or complete inheritance in Task 7. **Prefer inheriting in Task 7.** This task only removes `qobject_cast`.)
- Produces on `Application`:

```cpp
[[nodiscard]] VLMS::DataEvents& dataEvents();
[[nodiscard]] VLMS::ErrorPresenter& errorPresenter();
[[nodiscard]] VLMS::DialogFactory& dialogFactory();
```

Owned as `std::unique_ptr` members created **only when `m_databaseReady`** (same gate as repositories). When the DB is not ready, accessors are not called by `MainWindow` (placeholder path).

MainWindow constructor becomes:

```cpp
explicit MainWindow(Application& app, QWidget* parent = nullptr);
```

- [ ] **Step 1: Write a compile-focused assertion in a comment-free smoke — adjust `main.cpp` and rebuild the app target**

No new gtest that constructs `Application` (still opens the real DB). Verification is:

```bash
cmake --build build --target vlms -j$(nproc)
```

Plus existing `test_vlms_ui` still builds without constructing `MainWindow`.

- [ ] **Step 2: Confirm current `qobject_cast` sites** (read-only before edits)

Sites to eliminate in `MainWindow.cpp`:

1. Constructor `languageChanged` / `themeChanged` connect  
2. Theme toggle lambda  
3. Page construction block (`app->members()` etc.)  
4. `onLanguageChanged`

- [ ] **Step 3: Extend Application**

In `Application.h` add includes and members:

```cpp
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/DialogFactory.h"
#include "ui/workflows/ErrorPresenter.h"

#include <memory>

// ...
[[nodiscard]] VLMS::DataEvents& dataEvents() { return *m_dataEvents; }
[[nodiscard]] VLMS::ErrorPresenter& errorPresenter() { return *m_errorPresenter; }
[[nodiscard]] VLMS::DialogFactory& dialogFactory() { return *m_dialogFactory; }

// private:
std::unique_ptr<VLMS::DataEvents> m_dataEvents;
std::unique_ptr<VLMS::ErrorPresenter> m_errorPresenter;
std::unique_ptr<VLMS::DialogFactory> m_dialogFactory;
```

In `Application.cpp`, after repositories are constructed on the ready path:

```cpp
#include "ui/workflows/QtDialogFactory.h"
#include "ui/workflows/QtErrorPresenter.h"

m_dataEvents = std::make_unique<VLMS::DataEvents>(this);
m_errorPresenter = std::make_unique<VLMS::QtErrorPresenter>();
m_dialogFactory = std::make_unique<VLMS::QtDialogFactory>(
    *m_catalogRepository, *m_memberRepository, *m_circulationRepository);
```

If DB open fails, leave the three unique_ptrs null (mirrors today's null repositories).

- [ ] **Step 4: Rewire MainWindow**

```cpp
// MainWindow.h
explicit MainWindow(Application& app, QWidget* parent = nullptr);
// ...
Application& m_app;
```

```cpp
// MainWindow.cpp
MainWindow::MainWindow(Application& app, QWidget* parent)
    : QMainWindow(parent)
    , m_app(app)
{
    buildUi();
    connect(&m_app, &Application::languageChanged, this, &MainWindow::retranslateUi);
    connect(&m_app, &Application::themeChanged, this, &MainWindow::onThemeChanged);
    showPage(Page::Catalog);
    retranslateUi();
}
```

Theme toggle:

```cpp
connect(m_themeToggle, &QPushButton::clicked, this, [this]() { m_app.toggleTheme(); });
```

Page construction:

```cpp
if (m_app.isDatabaseReady()) {
    m_membersPage = new MembersPage(m_app.members(), m_app.circulation(), m_stack);
    m_catalogPage = new CatalogPage(m_app.catalog(), m_app.circulation(), m_stack);
    m_circulationPage =
        new CirculationPage(m_app.circulation(), m_app.catalog(), m_app.members(), m_stack);
    m_archivePage =
        new ArchivePage(m_app.members(), m_app.catalog(), m_app.circulation(), m_stack);
    m_metricsPage = new MetricsPage(m_app.metrics(), m_stack);
} else {
    // nullptrs + placeholders as today
}
```

(Signature updates for `DataEvents` / `Navigator` land in Tasks 6–7; this step only switches to `m_app`.)

`onLanguageChanged`:

```cpp
if (code.isEmpty() || code == m_app.uiLocale()) {
    return;
}
m_app.setUiLocale(code);
```

`main.cpp`:

```cpp
MainWindow window(app);
```

Grep the worktree and ensure **zero** remaining `qobject_cast<Application*>` under `applications/vlms/src/ui/`.

- [ ] **Step 5: Build app + UI tests**

```bash
cmake --build build --target vlms test_vlms_ui -j$(nproc)
cd build && ctest -L ui --output-on-failure
```

Expected: all UI tests PASS; `vlms` links.

- [ ] **Step 6: Commit (optional)**

```bash
git add applications/vlms/src/Application.h applications/vlms/src/Application.cpp \
        applications/vlms/src/ui/MainWindow.h applications/vlms/src/ui/MainWindow.cpp \
        applications/vlms/src/main.cpp applications/vlms/src/ui/LanguageSelector.h
git commit -m "$(cat <<'EOF'
feat(ui): make Application the composition root for shell deps

EOF
)"
```

---

### Task 6: Replace recordRestored with DataEvents::changed

**Files:**
- Modify: `applications/vlms/src/ui/archive/ArchivePage.h`
- Modify: `applications/vlms/src/ui/archive/ArchivePage.cpp`
- Modify: `applications/vlms/src/ui/MainWindow.cpp` (and `.h` if a slot is added)
- Modify: any test that constructs `ArchivePage` (search `new ArchivePage` / `ArchivePage(` in `applications/vlms/test`)

**Interfaces:**
- Consumes: `VLMS::DataEvents&`, `VLMS::Entity` / `EntitySet`
- Produces: `ArchivePage` constructor gains `VLMS::DataEvents& events`; **remove** `signals: void recordRestored();`

Entity mapping for restores / reuse:

| Action | `EntitySet` | `ids` |
|---|---|---|
| Restore member(s) | `Entity::Members` | restored id(s) |
| Restore book(s) | `Entity::Books` | restored id(s) |
| Restore copy(ies) | `Entity::Copies` | restored id(s) |
| Restore loan(s) | `Entity::Loans` | restored id(s) |
| Reuse number success | `Entity::Books \| Entity::Copies` | `{archivedCopy.id}` (and book id if known from the flow; otherwise copy id only) |

Helper inside `ArchivePage.cpp`:

```cpp
VLMS::EntitySet entityFor(ArchivePage::Type type)
{
    switch (type) {
    case Type::Members: return VLMS::Entity::Members;
    case Type::Books: return VLMS::Entity::Books;
    case Type::Copies: return VLMS::Entity::Copies;
    case Type::Loans: return VLMS::Entity::Loans;
    }
    return {};
}
```

Replace every `emit recordRestored();` with:

```cpp
emit m_events.changed(entityFor(m_type), QList<qint64>{id});  // or all succeeded ids for bulk
```

For bulk restore, collect succeeded ids inside the `act` lambda (push into a `QList<qint64>` captured by reference) and emit once with that list.

- [ ] **Step 1: Update / add a focused test**

If `test_archive_page.cpp` (or similar) asserts on `recordRestored`, rewrite to `QSignalSpy` on `DataEvents::changed`. Example:

```cpp
VLMS::DataEvents events;
QSignalSpy spy(&events, &VLMS::DataEvents::changed);
ArchivePage page(members, catalog, circulation, events);
// ... drive a restore with ModalTest helpers as the existing test does ...
ASSERT_GE(spy.count(), 1);
```

If no existing test covers the signal, add `TEST(ArchivePage, RestoreEmitsDataEvents)` that uses the same fixture pattern as `test_archive_*.cpp` (temp DB + repositories). Keep it minimal: one single-row restore path.

- [ ] **Step 2: Run the old symbol to see failures after header change**

After removing `recordRestored` but before fixing call sites:

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
```

Expected: FAIL on `recordRestored` / constructor arity.

- [ ] **Step 3: Implement ArchivePage + MainWindow subscription**

`ArchivePage` constructor:

```cpp
ArchivePage(VLMS::Repositories::MemberRepository& members,
            VLMS::Repositories::CatalogRepository& catalog,
            VLMS::Repositories::CirculationRepository& circulation,
            VLMS::DataEvents& events,
            QWidget* parent = nullptr);
```

Store `VLMS::DataEvents& m_events;`.

In `MainWindow::buildUi`, pass `m_app.dataEvents()`.

Remove the `connect(..., &ArchivePage::recordRestored, ...)` block.

Add:

```cpp
// MainWindow.h
private slots:
    void onDataChanged(VLMS::EntitySet what, QList<qint64> ids);

// MainWindow.cpp
connect(&m_app.dataEvents(), &VLMS::DataEvents::changed,
        this, &MainWindow::onDataChanged);

void MainWindow::onDataChanged(VLMS::EntitySet what, QList<qint64> /*ids*/)
{
    if (m_catalogPage != nullptr
        && (what.testFlag(VLMS::Entity::Books) || what.testFlag(VLMS::Entity::Copies)
            || what.testFlag(VLMS::Entity::Categories))) {
        m_catalogPage->refreshBooks();
    }
    if (m_membersPage != nullptr && what.testFlag(VLMS::Entity::Members)) {
        m_membersPage->refreshMembers();
    }
    if (m_circulationPage != nullptr && what.testFlag(VLMS::Entity::Loans)) {
        m_circulationPage->refreshLoans();
    }
    if (m_metricsPage != nullptr && what != VLMS::EntitySet{}) {
        m_metricsPage->refreshMetrics();
    }
}
```

Use the public refresh slots (`refreshBooks` / `refreshMembers` / `refreshLoans` / `refreshMetrics`), **not** `retranslateUi()`, for data refresh.

`refreshMembers` / `refreshLoans` are currently `private slots` — if the compiler rejects the call from `MainWindow`, either make them public (preferred, one-line visibility change) or call `retranslateUi()` only as a last resort. Prefer promoting the refresh methods to `public`.

- [ ] **Step 4: Run UI tests**

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
cd build && ctest -L ui --output-on-failure
```

Expected: PASS. Grep confirms no `recordRestored` left under `applications/vlms`.

- [ ] **Step 5: Commit (optional)**

```bash
git add applications/vlms/src/ui/archive/ArchivePage.h \
        applications/vlms/src/ui/archive/ArchivePage.cpp \
        applications/vlms/src/ui/MainWindow.h \
        applications/vlms/src/ui/MainWindow.cpp \
        applications/vlms/src/ui/catalog/CatalogPage.h \
        applications/vlms/src/ui/members/MembersPage.h \
        applications/vlms/src/ui/circulation/CirculationPage.h \
        applications/vlms/test/src/test_archive_page.cpp \
        applications/vlms/test/src/test_archive_catalog_ui.cpp \
        applications/vlms/test/src/test_archive_circulation_ui.cpp
git commit -m "$(cat <<'EOF'
feat(ui): refresh live pages through DataEvents on restore

EOF
)"
```

---

### Task 7: Replace memberLoansRequested with Navigator::showMemberLoans

**Files:**
- Modify: `applications/vlms/src/ui/MainWindow.h`
- Modify: `applications/vlms/src/ui/MainWindow.cpp`
- Modify: `applications/vlms/src/ui/members/MembersPage.h`
- Modify: `applications/vlms/src/ui/members/MembersPage.cpp`
- Modify: `applications/vlms/src/ui/archive/ArchivePage.h` / `.cpp` (add `setType` for `showArchive`)
- Modify: `applications/vlms/test/src/test_member_removal.cpp` (and any `MembersPage(` construction)

**Interfaces:**
- Consumes: `VLMS::Navigator` from Task 3; `CirculationPage::focusMemberLoans`; `CatalogPage::selectBookId`
- Produces:

```cpp
class MainWindow final : public QMainWindow, public VLMS::Navigator {
public:
    void show(VLMS::PageId page) override;
    void showMemberLoans(const QString& membershipNumber) override;
    void showBook(qint64 id) override;
    void showArchive(VLMS::ArchiveType type) override;
};
```

Map `PageId` ↔ existing private `MainWindow::Page` 1:1. Prefer renaming the private enum to `PageId` **or** keep a thin private enum and convert in `show(PageId)` — do not leave two divergent lists.

`ArchivePage` gains:

```cpp
void setType(Type type);
```

Implementation selects the matching `m_typeList` row (with `QSignalBlocker` if needed) and calls the same refresh path as `onTypeChanged`.

`MembersPage` constructor:

```cpp
MembersPage(VLMS::Repositories::MemberRepository& repository,
            VLMS::Repositories::CirculationRepository& circulationRepository,
            VLMS::Navigator& navigator,
            QWidget* parent = nullptr);
```

Remove `signals: void memberLoansRequested(...)`.

Blocked-delete path becomes:

```cpp
if (VLMS::showWarningWithAction(...)) {
    const auto member = m_repository.getMember(memberId);
    m_navigator.showMemberLoans(member ? qs(member->membershipNumber) : QString());
}
```

MainWindow implementations:

```cpp
void MainWindow::show(VLMS::PageId page)
{
    showPage(toInternal(page));  // or unify enums
}

void MainWindow::showMemberLoans(const QString& membershipNumber)
{
    show(VLMS::PageId::Circulation);
    if (m_circulationPage != nullptr) {
        m_circulationPage->focusMemberLoans(membershipNumber);
    }
}

void MainWindow::showBook(qint64 id)
{
    show(VLMS::PageId::Catalog);
    if (m_catalogPage != nullptr) {
        m_catalogPage->selectBookId(id);
    }
}

void MainWindow::showArchive(VLMS::ArchiveType type)
{
    show(VLMS::PageId::Archive);
    if (m_archivePage != nullptr) {
        m_archivePage->setType(toArchivePageType(type));
        m_archivePage->refresh();
    }
}
```

Remove the `connect(m_membersPage, &MembersPage::memberLoansRequested, ...)` lambda entirely.

Construct MembersPage as:

```cpp
m_membersPage = new MembersPage(m_app.members(), m_app.circulation(), *this, m_stack);
```

- [ ] **Step 1: Update member-removal UI test**

Wherever the test listened for `memberLoansRequested`, inject `FakeNavigator` and assert `memberLoans` was recorded. Example pattern:

```cpp
VLMS::FakeNavigator navigator;
MembersPage page(members, circulation, navigator);
// ... trigger blocked delete with ModalTest answering the warn-with-action ...
ASSERT_EQ(navigator.memberLoans.size(), 1);
EXPECT_FALSE(navigator.memberLoans[0].isEmpty());
```

- [ ] **Step 2: Build expecting constructor / signal breakage**

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
```

Expected: FAIL until call sites updated.

- [ ] **Step 3: Implement Navigator on MainWindow + MembersPage + ArchivePage::setType**

- [ ] **Step 4: Run UI tests**

```bash
cmake --build build --target vlms test_vlms_ui -j$(nproc)
cd build && ctest -L ui --output-on-failure
```

Expected: PASS. Grep: no `memberLoansRequested`, no `qobject_cast<Application*>` under `applications/vlms/src`.

- [ ] **Step 5: Commit (optional)**

```bash
git add applications/vlms/src/ui/MainWindow.h applications/vlms/src/ui/MainWindow.cpp \
        applications/vlms/src/ui/members/MembersPage.h applications/vlms/src/ui/members/MembersPage.cpp \
        applications/vlms/src/ui/archive/ArchivePage.h applications/vlms/src/ui/archive/ArchivePage.cpp \
        applications/vlms/test/src/test_member_removal.cpp
git commit -m "$(cat <<'EOF'
feat(ui): route cross-page jumps through Navigator

EOF
)"
```

---

### Task 8: Port tests and verify the full UI suite

**Files:**
- Modify: every remaining `applications/vlms/test/src/*.cpp` that constructs `MembersPage`, `ArchivePage`, or mentions `recordRestored` / `memberLoansRequested` / `qobject_cast<Application*>`
- Modify: `applications/vlms/test/CMakeLists.txt` comment at top (replace the "MainWindow reaches for qobject_cast" rationale with: tests still avoid constructing `Application`/`MainWindow` because `Application` opens the real DB; pages now take explicit deps / fakes)
- Verify: `applications/vlms/CMakeLists.txt` lists all Phase 1 sources

**Interfaces:**
- Consumes: all of Tasks 1–7
- Produces: green `test_vlms_ui`; app binary builds

- [ ] **Step 1: Inventory call sites**

```bash
grep -Rn "recordRestored\|memberLoansRequested\|qobject_cast<Application\|ArchivePage(\|MembersPage(" \
  applications/vlms --include='*.cpp' --include='*.h'
```

Expected after Tasks 6–7: only historical comments (update those) or zero hits for the three removed symbols.

- [ ] **Step 2: Fix any leftover test constructors**

For `ArchivePage`: pass a stack-local `VLMS::DataEvents events`.  
For `MembersPage`: pass `VLMS::FakeNavigator navigator`.  
Do not construct `Application` or `MainWindow` in tests.

- [ ] **Step 3: Full UI suite**

```bash
cmake --build build --target vlms test_vlms_ui -j$(nproc)
cd build && ctest -L ui --output-on-failure
```

Expected: all discovered `test_vlms_ui` cases PASS.

Also confirm backend-only still configures (no UI change required, smoke check):

```bash
cmake -S . -B build-core-only -DBUILD_APPS=OFF -DBUILD_TESTING=ON
```

(Configure only is enough; do not require a full core test run unless already cheap in this worktree.)

- [ ] **Step 4: Phase 1 acceptance checklist (manual)**

- [ ] `Application` exposes `dataEvents()`, `errorPresenter()`, `dialogFactory()`
- [ ] No `qobject_cast<Application*>` in UI shell
- [ ] No `recordRestored` signal
- [ ] No `memberLoansRequested` signal / MainWindow lambda
- [ ] Live list refresh after archive restore goes through `DataEvents::changed`
- [ ] Blocked member delete uses `Navigator::showMemberLoans`
- [ ] Pages still perform writes (Phase 1 exception documented)
- [ ] Backend libraries untouched (`git diff --stat libraries/` empty)

- [ ] **Step 5: Commit (optional)**

```bash
git add applications/vlms/test applications/vlms/CMakeLists.txt
git commit -m "$(cat <<'EOF'
test(ui): adapt page tests for DataEvents and Navigator

EOF
)"
```

---

## Self-review (Phase 1 spec coverage)

| Spec Phase 1 item | Task |
|---|---|
| `Application` as composition root | Task 5 |
| `DataEvents` | Task 1, wired in Tasks 5–6 |
| `Navigator` | Task 3, implemented Task 7 |
| `ErrorPresenter` | Task 2, owned Task 5 |
| `DialogFactory` with real Qt wrapper over today's dialogs | Task 4, owned Task 5 |
| Remove `qobject_cast<Application*>` lookups | Task 5 |
| Remove `recordRestored` → `retranslateUi()` refresh | Task 6 |
| Remove `MainWindow` cross-page lambdas (`memberLoansRequested`) | Task 7 |
| Folder start: `ui/shell/`, `ui/workflows/` | Tasks 1–4 (`Application` stays at `src/` until Phase 5) |
| DB fail → placeholders, no workflows | Task 5 (null unique_ptrs; existing placeholder branch) |
| Backend / schema unchanged | Global constraint; Task 8 verifies `libraries/` clean |
| Pages may still write | Global Phase 1 exception; no Task moves saves |
| UI tests still pass; no Application/MainWindow in tests | Task 8 |

**Out of scope (correctly absent):** `BookWorkflow` / `MemberWorkflow` / `LoanWorkflow` / `ArchiveWorkflow` / `BulkRunner` (Phase 2); `ListSpec` / `PagedQueryModel` / `ListPage` (Phase 3); Lookups / `LoanHistoryDialog` / `OcrService` (Phase 4); `QEvent::LanguageChange` / folder move of all pages (Phase 5); manual docs (Phase 6).

**Placeholder scan:** no TBD / TODO / "implement later" left in task steps.

**Type consistency:** `Entity` / `EntitySet` / `DataEvents::changed(EntitySet, QList<qint64>)`, `ErrorPresenter::show(QWidget*, const Core::Error&)`, `Navigator::{show,showMemberLoans,showBook,showArchive}`, `DialogFactory` form methods — names match across tasks.
