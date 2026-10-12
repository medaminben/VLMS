# Frontend Redesign Phase 3: Model and List Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce `ListSpec`, `PagedQueryModel`, and one `ListPage`, then replace Members → Catalog → Circulation → Archive so every list is a model-backed `QTableView` with 250 ms search debounce, shared action/menu/shortcut wiring, and a Show-All benchmark that proves the new path is faster than the old `QTableWidget` path until that path is gone.

**Architecture:** Presentation types and the seven `ListSpec` implementations live under `ui/presentation/` and only read repositories. `PagedQueryModel` is a `QAbstractTableModel` over one `ListSpec*` and one `QueryState`, owning ticks by id and reloading on `DataEvents`. `ListPage` (under `ui/` for now; Phase 5 moves it to `ui/views/`) hosts search, facets, pager, preview, and builds buttons/context menu/shortcuts from `PageAction`. Each page migration deletes that page class and its `QTableWidget` helpers; after the last migration, delete `TablePager`, `TableHeaderSort`, and `TableRowChecks`.

**Tech Stack:** C++17, Qt 6 Widgets (`QAbstractTableModel`, `QTableView`, `QTimer`), CMake ≥ 3.21, GoogleTest, existing `VLMS::Core` / `VLMS::Repositories` (unchanged public API), `DataEvents` / workflows from Phases 1–2.

**Spec:** `docs/superpowers/specs/2026-09-29-frontend-redesign-design.md` (Phase 3).

**Branch:** `redesign/3-model-list-page` (from up-to-date `Beta` after Phase 2 merges).

**Worktree:** `<repo>/.worktrees/redesign-1-foundations` (create/switch to the Phase 3 branch at execution time via using-git-worktrees).

## Global Constraints

- Dependencies point downwards only (shell → views → presentation → workflows → services → repositories). Specs only read; only workflows write.
- `load()` builds every display string once; `cell()` only reads prebuilt values; covers/photos load in `preview()` for the selected row only.
- `retranslate()` rebuilds display strings from kept records without querying.
- `QTableView` uses column widths from `ColumnSpec` and a fixed row height; no `ResizeToContents` on list views.
- Search debounce is 250 ms; Enter searches at once. Enter/Del/Shift+Del/Ctrl+L/Ctrl+A are `Qt::WidgetWithChildrenShortcut` on the table; Ctrl+N and F5 are page-scoped.
- Each spec lists only the actions its page offers today; the standard table fixes order and shortcuts, not presence.
- Show-All benchmark (`test_vlms_bench`, CTest label `bench`) fails if Catalog Show-All refresh ≥ 300 ms on CI; until the last `QTableWidget` list page is gone it also fails if the new path is slower than the old path. Built in every CI job; skipped at run time in the `sanitizers` job.
- Backend libraries and schema do not change. No Lookups/`OcrService`/`LoanHistoryDialog`/`LanguageChange` (Phases 4–5).
- A phase deletes the old tests for code it removes in the same PR that adds the replacement tests.
- Commits in task steps are optional for SDD hygiene; leave the commit step so a human or SDD run can use it.

---

## File map

| Path | Responsibility |
|---|---|
| `applications/vlms/src/ui/presentation/QueryState.h` | `LoanScope`, `QueryState` |
| `applications/vlms/src/ui/presentation/ColumnSpec.h` | Column metadata |
| `applications/vlms/src/ui/presentation/FacetSpec.h` | Facet metadata + values loader |
| `applications/vlms/src/ui/presentation/PageAction.h` | `ActionContext`, `PageAction` |
| `applications/vlms/src/ui/presentation/PreviewContent.h` | Preview image paths + detail fields |
| `applications/vlms/src/ui/presentation/ListSpec.h` | Abstract list contract |
| `applications/vlms/src/ui/presentation/PagedQueryModel.h` / `.cpp` | `QAbstractTableModel` over a spec |
| `applications/vlms/src/ui/presentation/MemberListSpec.h` / `.cpp` | Live members |
| `applications/vlms/src/ui/presentation/BookListSpec.h` / `.cpp` | Live catalogue |
| `applications/vlms/src/ui/presentation/LoanListSpec.h` / `.cpp` | Live loans (+ pinned/history later) |
| `applications/vlms/src/ui/presentation/ArchivedMemberSpec.h` / `.cpp` | Archived members |
| `applications/vlms/src/ui/presentation/ArchivedBookSpec.h` / `.cpp` | Archived books |
| `applications/vlms/src/ui/presentation/ArchivedCopySpec.h` / `.cpp` | Archived copies |
| `applications/vlms/src/ui/presentation/ArchivedLoanSpec.h` / `.cpp` | Archived loans |
| `applications/vlms/src/ui/ListPage.h` / `.cpp` | One list view controller |
| `applications/vlms/src/ui/MainWindow.h` / `.cpp` | Owns specs + `ListPage`s; Navigator reveals |
| `applications/vlms/src/Application.h` / `.cpp` | Construct specs when DB opens (optional; may stay in MainWindow) |
| `applications/vlms/CMakeLists.txt` | Register presentation + ListPage sources |
| `applications/vlms/test/CMakeLists.txt` | `test_vlms_presentation`, `test_vlms_bench`; trim `test_vlms_ui` |
| `applications/vlms/test/src/test_*_list_spec.cpp` | Per-spec presentation tests |
| `applications/vlms/test/src/test_paged_query_model.cpp` | Model behaviour |
| `applications/vlms/test/src/test_list_page.cpp` | Debounce, actions, shortcuts, retry |
| `applications/vlms/test/src/test_vlms_bench.cpp` | Show-All timing |
| Delete when last page migrates | `TablePager.*`, `TableHeaderSort.*`, `TableRowChecks.*`, `MembersPage.*`, `CatalogPage.*`, `CirculationPage.*`, `ArchivePage.*`, and their page-specific tests that only exercise the old widgets |

**Stays until Phase 4–5:** loan dialogs, `ListPageFrame` dashboard path for Metrics, `retranslateUi` chain. Facet UI: `ListPage::addFilterWidget` hosts today's `BookFacetFilters` / `MemberFacetFilters` / loan-status `QListWidget`; `FacetSpec` documents keys for presentation tests and for mapping widget picks into `QueryState::facets`. Do not rewrite facet widgets in Phase 3.

**Not in Phase 3:** `Lookups`, `OcrService`, `LoanHistoryDialog`, layer folder moves, manual text.

---

### Task 1: Presentation value types

**Files:**
- Create: `applications/vlms/src/ui/presentation/QueryState.h`
- Create: `applications/vlms/src/ui/presentation/ColumnSpec.h`
- Create: `applications/vlms/src/ui/presentation/FacetSpec.h`
- Create: `applications/vlms/src/ui/presentation/PageAction.h`
- Create: `applications/vlms/src/ui/presentation/PreviewContent.h`
- Modify: `applications/vlms/CMakeLists.txt` (add headers to `vlms_ui` for IDE/install consistency; headers-only OK)

**Interfaces:**
- Consumes: `VLMS::Repositories::ArchiveScope`, Qt containers, `Core::Status`
- Produces: the types below (exact names used by later tasks)

```cpp
// QueryState.h
#pragma once
#include <VLMS/Repositories/ArchiveTypes.h>
#include <QHash>
#include <QString>
#include <QStringList>
#include <optional>

namespace VLMS {

struct LoanScope {
    qint64 memberId = 0;
    qint64 bookId = 0;
    qint64 copyId = 0;
};

struct QueryState {
    QString search;
    QHash<QString, QStringList> facets;  // facet key -> ticked values
    QString sortKey;
    bool ascending = true;
    int pageSize = 0;  // 0 = Show All (today's default)
    int page = 1;
    Repositories::ArchiveScope scope = Repositories::ArchiveScope::Live;
    std::optional<LoanScope> pinned;
};

}  // namespace VLMS
```

```cpp
// ColumnSpec.h
#pragma once
#include <QString>

namespace VLMS {

struct ColumnSpec {
    QString key;
    QString titleKey;   // Strings::t key
    int width = 100;
    QString sortKey;    // empty = not sortable
};

}  // namespace VLMS
```

```cpp
// FacetSpec.h
#pragma once
#include <QString>
#include <QStringList>
#include <functional>

namespace VLMS {

struct FacetSpec {
    QString key;
    QString titleKey;
    /// Fresh values for the facet list (codes or display tokens the page
    /// already understands). Empty loader = static facet filled elsewhere
    /// (e.g. loan open/overdue/returned/all).
    std::function<QStringList()> values;
};

}  // namespace VLMS
```

```cpp
// PageAction.h
#pragma once
#include <VLMS/Core/Result.h>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <functional>

class QWidget;

namespace VLMS {

struct ActionContext {
    QList<qint64> ids;
    QWidget* parent = nullptr;
};

struct PageAction {
    QString id;
    QString labelKey;
    QKeySequence shortcut;
    enum class Needs { Nothing, One, Some } needs = Needs::Nothing;
    std::function<Core::Status(const ActionContext&)> run;
    std::function<bool(const ActionContext&)> enabled;  // optional extra rule
};

}  // namespace VLMS
```

```cpp
// PreviewContent.h
#pragma once
#include <QString>
#include <QVector>

namespace VLMS {

struct PreviewField {
    QString labelKey;
    QString value;
    bool scrollable = false;
};

struct PreviewContent {
    QString imagePath;
    QString secondImagePath;
    QVector<PreviewField> fields;
};

}  // namespace VLMS
```

- [ ] **Step 1: Create the five headers and register them in `vlms_ui`**

Add under `add_library(vlms_ui STATIC` in `applications/vlms/CMakeLists.txt`:

```cmake
    src/ui/presentation/QueryState.h
    src/ui/presentation/ColumnSpec.h
    src/ui/presentation/FacetSpec.h
    src/ui/presentation/PageAction.h
    src/ui/presentation/PreviewContent.h
```

- [ ] **Step 2: Build `vlms_ui`**

```bash
cmake --build build --target vlms_ui -j$(nproc)
```

Expected: PASS (headers compile as part of the target).

- [ ] **Step 3: Commit (optional)**

```bash
git add applications/vlms/src/ui/presentation/*.h applications/vlms/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(ui): add presentation value types for ListSpec

EOF
)"
```

---

### Task 2: ListSpec interface

**Files:**
- Create: `applications/vlms/src/ui/presentation/ListSpec.h`
- Modify: `applications/vlms/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 types, `EntitySet` from `ui/workflows/DataEvents.h`, `Core::Status` / `Core::Result`
- Produces:

```cpp
#pragma once

#include "ui/presentation/ColumnSpec.h"
#include "ui/presentation/FacetSpec.h"
#include "ui/presentation/PageAction.h"
#include "ui/presentation/PreviewContent.h"
#include "ui/presentation/QueryState.h"
#include "ui/workflows/DataEvents.h"

#include <VLMS/Core/Result.h>

#include <QList>
#include <QString>
#include <QVariant>

namespace VLMS {

class ListSpec {
public:
    virtual ~ListSpec() = default;

    virtual QString id() const = 0;
    virtual QList<ColumnSpec> columns() const = 0;
    virtual QList<FacetSpec> facets() const = 0;
    virtual QList<PageAction> actions() const = 0;
    virtual EntitySet dependsOn() const = 0;

    virtual Core::Status load(const QueryState& state) = 0;
    virtual void retranslate() = 0;
    virtual int rowCount() const = 0;
    virtual int totalCount() const = 0;
    virtual qint64 idAt(int row) const = 0;
    virtual QVariant cell(int row, int column, int role) const = 0;
    virtual Core::Result<int> rankOf(qint64 id, const QueryState& state) const = 0;
    virtual PreviewContent preview(qint64 id) const = 0;
};

}  // namespace VLMS
```

- [ ] **Step 1: Add `ListSpec.h` and CMake entry**
- [ ] **Step 2: Build `vlms_ui` — Expected: PASS**
- [ ] **Step 3: Commit (optional)** `feat(ui): add ListSpec interface`

---

### Task 3: PagedQueryModel

**Files:**
- Create: `applications/vlms/src/ui/presentation/PagedQueryModel.h`
- Create: `applications/vlms/src/ui/presentation/PagedQueryModel.cpp`
- Create: `applications/vlms/test/src/test_paged_query_model.cpp`
- Modify: `applications/vlms/CMakeLists.txt`
- Modify: `applications/vlms/test/CMakeLists.txt` (add to `test_vlms_ui` for now; Task 5 introduces `test_vlms_presentation`)

**Interfaces:**
- Consumes: `ListSpec*`, `QueryState`, `DataEvents&`
- Produces:

```cpp
class PagedQueryModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    PagedQueryModel(ListSpec* spec, DataEvents& events, QObject* parent = nullptr);

    void setSpec(ListSpec* spec);           // Archive type switch
    void setQuery(const QueryState& state);
    void reload();
    [[nodiscard]] const QueryState& query() const;

    [[nodiscard]] QList<qint64> checkedIds() const;
    void tickAll();
    void clearTicks();
    /// Uses spec->rankOf to change page and select; returns whether found.
    bool revealId(qint64 id);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QVariant headerData(int section, Qt::Orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    void sort(int column, Qt::SortOrder order) override;

signals:
    void loaded(int total);
    void failed(Core::Error error);
    void selectionTargetChanged(int row);  // revealId helper for the view

private:
    void onDataChanged(EntitySet what, const QList<qint64>& ids);
    void applyLoad(Core::Status status);

    ListSpec* m_spec = nullptr;
    DataEvents& m_events;
    QueryState m_state;
    QSet<qint64> m_checked;
};
```

Behaviour rules (port from today's helpers):

- `setQuery` / `reload` / `sort` call `spec->load(m_state)`, `beginResetModel`/`endResetModel`, emit `loaded(total)` or `failed`.
- On failure keep previous rows (do not clear the model) and emit `failed`.
- `data(..., Qt::CheckStateRole)` on column 0 reads/writes `m_checked` by `idAt(row)`.
- After a successful load, drop ids from `m_checked` that are no longer listed.
- `tickAll()` ticks every `idAt(row)` currently listed.
- `revealId`: `rankOf` → set `page` from rank and `pageSize` (if `pageSize==0`, page stays 1) → `reload` → emit `selectionTargetChanged(rowInPage)`.
- Subscribe to `DataEvents::changed`; reload when `what & spec->dependsOn()`.

- [ ] **Step 1: Write failing tests** in `test_paged_query_model.cpp` using a tiny in-memory `FakeListSpec` (local to the test file) that returns fixed rows and records `load` calls:

```cpp
TEST(PagedQueryModel, ReloadEmitsLoadedAndRowCount)
TEST(PagedQueryModel, CheckStateTracksIdsAcrossReload)
TEST(PagedQueryModel, TickAllChecksEveryListedId)
TEST(PagedQueryModel, UntickIdsMissingAfterReload)
TEST(PagedQueryModel, SortUpdatesQueryStateAndReloads)
TEST(PagedQueryModel, RevealIdMovesPageUsingRankOf)
TEST(PagedQueryModel, DataEventsMatchingDependsOnReloads)
TEST(PagedQueryModel, FailedLoadKeepsOldRowsAndEmitsFailed)
```

- [ ] **Step 2: Run tests — Expected: FAIL (PagedQueryModel missing)**

```bash
cmake --build build --target test_vlms_ui -j$(nproc)
```

- [ ] **Step 3: Implement `PagedQueryModel` and register sources**
- [ ] **Step 4: Run tests — Expected: PASS for `PagedQueryModel.*`**

```bash
cd build && ctest -R 'PagedQueryModel' --output-on-failure
```

- [ ] **Step 5: Commit (optional)** `feat(ui): add PagedQueryModel over ListSpec`

---

### Task 4: ListPage shell

**Files:**
- Create: `applications/vlms/src/ui/ListPage.h`
- Create: `applications/vlms/src/ui/ListPage.cpp`
- Create: `applications/vlms/test/src/test_list_page.cpp`
- Modify: `applications/vlms/CMakeLists.txt`
- Modify: `applications/vlms/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `ListSpec&` (or owning unique_ptr swap via model), `PagedQueryModel`, workflows only through `PageAction::run` closures built by the page owner / MainWindow
- Produces:

```cpp
class ListPage final : public QWidget {
    Q_OBJECT
public:
    struct Config {
        QString titleKey;
        QString bodyKey;
        QString searchPlaceholderKey;
        PreviewImageBounds imageBounds;
        QString imageObjectName;
        QString secondImageObjectName;  // empty = one image
        PreviewImageBounds secondImageBounds;
        int previewPanelMinWidth = 200;
    };

    ListPage(ListSpec* spec, DataEvents& events, const Config& config,
             QWidget* parent = nullptr);

    void setSpec(ListSpec* spec);  // Archive type switch
    [[nodiscard]] PagedQueryModel* model() const;
    [[nodiscard]] QTableView* table() const;
    void revealId(qint64 id);
    void setSearchText(const QString& text);  // Navigator focusMemberLoans
    void focusSearch();

    /// Rebuild action buttons/menus from spec->actions() and retranslate titles.
    void rebuildChrome();

protected:
    void changeEvent(QEvent* event) override;  // prepare for Phase 5; Phase 3 may still use retranslateUi()

public slots:
    void retranslateUi();  // temporary until Phase 5 LanguageChange

private:
    void buildUi(const Config& config);
    void onSearchEdited(const QString& text);
    void searchNow();
    void onFacetChanged();
    void onPageChanged();
    void onSelectionChanged();
    void onModelFailed(const Core::Error& error);
    void retryLoad();
    void rebuildActions();
    void showContextMenu(const QPoint& pos);
    void installShortcuts();
    void updateActionEnabled();
    void applyPreview(const PreviewContent& content);
    ActionContext currentContext() const;

    ListSpec* m_spec = nullptr;
    PagedQueryModel* m_model = nullptr;
    QLineEdit* m_search = nullptr;
    QTimer* m_debounce = nullptr;  // 250 ms, singleShot
    QTableView* m_table = nullptr;
    // Pager controls ported from TablePager (or wrap TablePager until Task 14)
    QWidget* m_pager = nullptr;
    QWidget* m_filterColumn = nullptr;
    QWidget* m_buttonPad = nullptr;
    QLabel* m_image = nullptr;
    QLabel* m_secondImage = nullptr;
    QWidget* m_details = nullptr;
    QWidget* m_errorStrip = nullptr;  // hidden unless failed
    QList<QPushButton*> m_actionButtons;
    QList<QShortcut*> m_shortcuts;
};
```

Standard action order when present (filter `spec->actions()` by id, sort by this table):

| id | Shortcut | Needs | Scope |
|---|---|---|---|
| `add` | Ctrl+N | Nothing | Page |
| `edit` | Enter / Return | One | Table |
| `loans` | Ctrl+L | One | Table |
| `archive` / `restore` | Del | Some | Table |
| `purge` | Shift+Del | Some | Table |
| `tickAll` | Ctrl+A | Nothing | Table |
| `refresh` | F5 | Nothing | Page |

Extra circulation actions (`return`, `extend`, `checkout`) keep their existing label keys and sit in the button pad in today's visual order (Return, Checkout, Extend, Archive) while still participating in enabling rules.

Search behaviour:

```cpp
m_debounce->setSingleShot(true);
m_debounce->setInterval(250);
connect(m_search, &QLineEdit::textChanged, this, [this](const QString&) {
    m_debounce->start();
});
connect(m_debounce, &QTimer::timeout, this, &ListPage::searchNow);
connect(m_search, &QLineEdit::returnPressed, this, [this] {
    m_debounce->stop();
    searchNow();
});
```

`searchNow` sets `state.search`, `state.page = 1`, `model->setQuery(state)`.

Pager: port `TablePager` behaviour (Show All default, page size combo, first/prev/next/last). Either embed a `TablePager` temporarily or copy its UI into `ListPage`; Task 14 deletes `TablePager` once nothing else includes it.

Facets: for Phase 3, `ListPage` may accept optional external facet widgets added via `addFilterWidget(QWidget*)` so Member/Book/loan filter UIs stay correct. Spec `facets()` still documents keys for tests; widget emits → update `QueryState::facets` → `setQuery`.

Error strip: on `failed`, show a strip with the error text + Retry button calling `reload()`; do not clear rows.

- [ ] **Step 1: Write failing UI tests** (`test_list_page.cpp`, offscreen `QApplication` via existing `test_main.cpp`):

```cpp
TEST(ListPage, SearchDebounce250msBeforeSetQuery)
TEST(ListPage, EnterSearchesImmediately)
TEST(ListPage, ActionButtonsEnableByNeeds)
TEST(ListPage, ContextMenuListsEnabledActions)
TEST(ListPage, TableScopedShortcutsDoNotStealSearchCtrlA)
TEST(ListPage, FailedLoadShowsRetryStrip)
TEST(ListPage, SelectionCallsPreview)
```

Use `QSignalSpy` / `QTest::qWait(250)` only where necessary; prefer advancing `QTimer` via `QTest::qWait(260)` for debounce. Fake spec records `load`/`preview` calls.

- [ ] **Step 2: Run — Expected: FAIL**
- [ ] **Step 3: Implement ListPage (may reuse layout code from `ListPageFrame` by composition or by copying the list-mode layout into ListPage; do not break Metrics' `buildDashboard`)**
- [ ] **Step 4: Run — Expected: PASS for `ListPage.*`**
- [ ] **Step 5: Commit (optional)** `feat(ui): add ListPage with debounce and PageAction chrome`

---

### Task 5: `test_vlms_presentation` target + MemberListSpec

**Files:**
- Create: `applications/vlms/src/ui/presentation/MemberListSpec.h` / `.cpp`
- Create: `applications/vlms/test/src/test_member_list_spec.cpp`
- Modify: `applications/vlms/CMakeLists.txt`
- Modify: `applications/vlms/test/CMakeLists.txt` — new executable

**Interfaces:**
- Consumes: `MemberRepository&`, `MemberWorkflow&` (for action closures), `Navigator` unused in spec itself
- Produces: `MemberListSpec` implementing `ListSpec`

Facet keys (map into `MemberQuery`):

| Facet key | Query field |
|---|---|
| `status` | `statuses` |
| `sex` | `sexes` |
| `year` | `inscriptionYears` |
| `ageGroup` | `ageGroups` |
| `city` | `cities` |

Columns (widths match today's `{140, name, phone, 140, 120, 80}`):

| key | titleKey | sortKey |
|---|---|---|
| number | `members.col.number` | `MemberSort::kNumber` |
| name | `members.col.name` | `MemberSort::kName` |
| phone | `members.col.phone` | `MemberSort::kPhone` |
| city | `members.col.city` | `MemberSort::kCity` |
| status | `members.col.status` | `MemberSort::kStatus` |
| loans | `members.col.loans` | `MemberSort::kLoans` |

`dependsOn()` → `Entity::Members`.
`scope` always `Live` for this spec.
`id()` → `"members"`.

`load()`:

```cpp
MemberQuery q = toQuery(state);  // search, facets, sort, limit/offset from pageSize/page
auto rows = m_members.listMembers(q);
auto total = m_members.countMembers(q);
// keep records; build DisplayRow { id, cells[6] as QString already translated }
```

`cell(row, col, Qt::DisplayRole)` returns prebuilt string; `Qt::UserRole` on col 0 may return id.
`rankOf` → `m_members.rankOfMember`.
`preview(id)` → `getMember` + photo path via `resolveImagePath` + fields using today's `member.field.*` keys (notes scrollable).
`retranslate()` rebuilds status/sex/age labels from kept `MemberRecord`s without calling `listMembers`.
`actions()`: `loans`, `add`, `edit`, `archive`, `tickAll`, `refresh` wired to `MemberWorkflow` methods (ids from context).

CMake for presentation tests:

```cmake
build_gtest_executable(
    NAME test_vlms_presentation
    SRC
        src/test_main.cpp
        src/test_member_list_spec.cpp
        # later tasks append other spec tests
    DEPENDS
        vlms_testsupport
        vlms_ui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Test
    DISCOVER ON
    NO_GTEST_MAIN
    LABELS ui presentation
)
```

Tests (real temp fixture DB, no widgets):

```cpp
TEST(MemberListSpec, MapsFacetsSortAndPagingIntoMemberQuery)
TEST(MemberListSpec, DisplayRowsUsePrebuiltStrings)
TEST(MemberListSpec, RetranslateDoesNotQuery)
TEST(MemberListSpec, RankOfMatchesRepository)
TEST(MemberListSpec, PreviewLoadsPhotoPathAndFields)
TEST(MemberListSpec, DependsOnMembers)
```

- [ ] **Step 1: Write failing tests**
- [ ] **Step 2: Implement MemberListSpec + CMake target**
- [ ] **Step 3:** `ctest -R 'MemberListSpec' --output-on-failure` — Expected: PASS
- [ ] **Step 4: Commit (optional)** `feat(ui): add MemberListSpec and presentation test target`

---

### Task 6: Replace MembersPage with ListPage

**Files:**
- Modify: `applications/vlms/src/ui/MainWindow.h` / `.cpp` — own `MemberListSpec` + `ListPage* m_membersPage`; drop `MembersPage`
- Delete: `applications/vlms/src/ui/members/MembersPage.h` / `.cpp`
- Modify: `applications/vlms/CMakeLists.txt` — remove MembersPage; keep `MemberFacetFilters`, editors, loan dialog
- Modify/Delete tests that construct `MembersPage`: port coverage into `MemberListSpec.*` / `ListPage.*` / workflow tests; remove obsolete `test_member_sort.cpp` / `test_member_filters.cpp` page-level cases that only poke the old table (keep editor/filter unit tests that do not need `MembersPage`)
- Wire `Navigator::showMemberLoans` still focuses circulation (unchanged); members reveal after add/edit via `ListPage::revealId`

Construction sketch in MainWindow:

```cpp
m_memberListSpec = std::make_unique<MemberListSpec>(m_app.members(), m_memberWorkflow);
ListPage::Config cfg;
cfg.titleKey = QStringLiteral("page.members.title");
cfg.bodyKey = QStringLiteral("page.members.body");
cfg.searchPlaceholderKey = QStringLiteral("members.searchPlaceholder");
cfg.imageObjectName = QStringLiteral("memberPhoto");
cfg.imageBounds = memberPhotoPreviewBounds();
m_membersPage = new ListPage(m_memberListSpec.get(), m_app.dataEvents(), cfg, this);
m_membersPage->addFilterWidget(new MemberFacetFilters(
    m_app.members(), Repositories::ArchiveScope::Live, m_membersPage->filterColumn()));
// map facet widget -> QueryState keys in ListPage or a thin adapter
```

After successful `MemberWorkflow::add/edit`, call `m_membersPage->revealId(id)`.

- [ ] **Step 1: Wire MainWindow; delete MembersPage**
- [ ] **Step 2: Fix compiling tests; delete obsolete MembersPage-only tests**
- [ ] **Step 3:** `ctest -L ui --output-on-failure` — Expected: PASS
- [ ] **Step 4: Grep gate** — no references to `MembersPage`
- [ ] **Step 5: Commit (optional)** `feat(ui): replace MembersPage with ListPage + MemberListSpec`

---

### Task 7: BookListSpec

**Files:**
- Create: `applications/vlms/src/ui/presentation/BookListSpec.h` / `.cpp`
- Create: `applications/vlms/test/src/test_book_list_spec.cpp`
- Modify: CMake lists

Facet keys → `BookQuery`: `language`→`languages`, `category`→`categoryCodes`, `cover`→`CoverFilter` (`all`/`with`/`without`).
Columns: title/author/category/localNumber/copies/available with today's widths and `BookSort::*` keys.
`dependsOn()` → `Books | Copies`.
Local-number column keeps `LocalNumberDelegate` on the `QTableView` (set in MainWindow/Catalog wiring).
`preview`: cover via `resolveCoverPath` + `book.field.*` details (description scrollable).
Actions: `loans`, `add`, `edit`, `archive`, `tickAll`, `refresh` → `BookWorkflow`. Categories remain available via existing `BookWorkflow::manageCategories` entry point if MainWindow already exposes it; if only reachable from a removed menu, keep a `categories` PageAction only if Catalog offered it as a button today (it does not — do not invent a button).

Tests mirror MemberListSpec naming: `BookListSpec.Maps*`, `Display*`, `Retranslate*`, `RankOf*`, `Preview*`, `DependsOn*`.

- [ ] **Step 1–4:** TDD as Task 5; commit `feat(ui): add BookListSpec`

---

### Task 8: Replace CatalogPage with ListPage

**Files:**
- Modify: `MainWindow.*` — `BookListSpec` + Catalog `ListPage`; `Navigator::showBook` → `revealId`
- Delete: `CatalogPage.h` / `.cpp`
- Port/delete catalog page tests (`test_catalog_sort`, `test_catalog_removal`, `test_catalog_local_number`, `test_catalog_loans` page shells) into presentation/workflow/ListPage coverage; keep delegate/editor OCR tests
- Install `LocalNumberDelegate` on the catalog table's local-number column index

- [ ] **Step 1–5:** Wire, delete, `ctest -L ui`, grep no `CatalogPage`, commit `feat(ui): replace CatalogPage with ListPage + BookListSpec`

---

### Task 9: LoanListSpec

**Files:**
- Create: `LoanListSpec.h` / `.cpp`
- Create: `test_loan_list_spec.cpp`

Facet keys: `loanFilter` values `open`/`overdue`/`returned`/`all` → `LoanQuery::filters`; member facet keys as Members but years go to `loanYears` when using loan-year source; `pinned` maps to `memberId`/`bookId`/`copyId`.
Columns: member/number/title/borrowed/due/status with `LoanSort::*`.
Status cell preformats returned/overdue/open labels (`circulation.status.*`).
`dependsOn()` → `Loans | Members | Copies | Books`.
`preview`: cover + member photo + `loan.field.*`.
Actions: `return`, `checkout`, `extend`, `archive`, `tickAll`, `refresh` with enabling: return/extend need one open loan; archive Needs::Some (and today's rule: enabled when returned or any ticked).

Tests include `LoanListSpec.Filter*` for open/overdue/returned/all and `LoanListSpec.Pinned*`.

- [ ] **Step 1–4:** TDD; commit `feat(ui): add LoanListSpec`

---

### Task 10: Replace CirculationPage with ListPage

**Files:**
- Modify: `MainWindow.*` — LoanListSpec + ListPage; `focusMemberLoans` becomes `setSearchText` + loan filter All + `focusSearch`
- Delete: `CirculationPage.*`
- Host loan filter `QListWidget` + `MemberFacetFilters` (ArchiveScope::Any + `useLoanYears`) as filter widgets
- Port `test_circulation_filters.cpp` assertions onto LoanListSpec / ListPage

- [ ] **Step 1–5:** Wire, delete, test, grep, commit `feat(ui): replace CirculationPage with ListPage + LoanListSpec`

---

### Task 11: Archived*Spec (four specs)

**Files:**
- Create: `ArchivedMemberSpec`, `ArchivedBookSpec`, `ArchivedCopySpec`, `ArchivedLoanSpec` (+ `.cpp`)
- Create: `test_archived_member_spec.cpp`, `test_archived_book_spec.cpp`, `test_archived_copy_spec.cpp`, `test_archived_loan_spec.cpp`

Each forces `QueryState::scope = Archived` in `load` (ignore Live).
Columns/sort keys match `ArchivePage.cpp` `columnsFor` (including `archivedAt`, copy `loans`, loan `returnedAt`).
Copy list uses `listCopyRows` / `countCopyRows`. There is no `rankOfCopy` on `CatalogRepository` and **must not** be added (backend API frozen). Implement `ArchivedCopySpec::rankOf` by loading the ordered id list with the same `CopyQuery` filters/sort and `limit=0` meaning unrestricted (set `limit` to `countCopyRows` result, `offset=0`), then return the 0-based index of `id`, or `Core::Error` if missing. Archived volumes are small enough for this read-only scan.
Actions per type: `loans` (not for Loans type), `restore`, `reuse` (copies only, Needs::One), `purge`, `tickAll`, `refresh` → `ArchiveWorkflow`.
`dependsOn` matches entity.
Preview/details: port ArchivePage `showDetails` field sets.

- [ ] **Step 1–4:** TDD all four; commit `feat(ui): add Archived Member/Book/Copy/Loan specs`

---

### Task 12: Replace ArchivePage with ListPage + setSpec

**Files:**
- Modify: `MainWindow.*` — one `ListPage` for archive; type list switches `setSpec` among the four archived specs; `Navigator::showArchive` selects type + shows page
- Delete: `ArchivePage.*`
- Port `test_archive_page.cpp` / archive UI tests to specs + ListPage type switch
- Keep `ReuseNumberFlow` (called from workflow)

Type list remains a filter-column widget owned by MainWindow or ListPage config callback.

- [ ] **Step 1–5:** Wire, delete, test, grep no `ArchivePage`, commit `feat(ui): replace ArchivePage with ListPage and archived specs`

---

### Task 13: Show-All benchmark (`test_vlms_bench`)

**Files:**
- Create: `applications/vlms/test/src/test_vlms_bench.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt`

```cmake
build_gtest_executable(
    NAME test_vlms_bench
    SRC src/test_main.cpp src/test_vlms_bench.cpp
    DEPENDS vlms_testsupport vlms_ui Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Test
    DISCOVER ON
    NO_GTEST_MAIN
    LABELS bench
)
# Skip at runtime in sanitizers job: in test body,
# if (qEnvironmentVariableIsSet("VLMS_SKIP_BENCH")) GTEST_SKIP();
```

Test body:

1. Copy `database/vlms.db` to a temp dir (3,071 books / 4,712 copies).
2. Open repositories; construct `BookListSpec` + `PagedQueryModel` + optional offscreen `QTableView` at 1280×800.
3. Measure: first `load` Show All; refresh after setting a search string then clearing; repaint (`viewport()->repaint()`).
4. `EXPECT_LT(refreshMs, 300)`.
5. Until Task 14 removes the old path, also build a minimal old-style fill (or keep a `#if` harness that fills a `QTableWidget` like CatalogPage did) in the same run and `EXPECT_LE(newRefreshMs, oldRefreshMs)`.
6. Document in CI workflow: sanitizers job exports `VLMS_SKIP_BENCH=1` when invoking ctest, or use `ctest -LE bench` there if labels are already excluded — match existing sanitizers invocation; if sanitizers runs all labels, add the env skip.

- [ ] **Step 1: Implement bench + CMake**
- [ ] **Step 2: Run locally**

```bash
cmake --build build --target test_vlms_bench -j$(nproc)
cd build && ctest -L bench --output-on-failure
```

Expected: PASS on a normal Release/RelWithDebInfo runner; may be tight under ASan (hence skip).

- [ ] **Step 3: Commit (optional)** `test(ui): add Catalog Show-All benchmark`

---

### Task 14: Remove TablePager, TableHeaderSort, TableRowChecks + old-path bench comparison

**Files:**
- Delete: `applications/vlms/src/ui/TablePager.cpp` / `.h`
- Delete: `applications/vlms/src/ui/TableHeaderSort.cpp` / `.h`
- Delete: `applications/vlms/src/ui/TableRowChecks.cpp` / `.h`
- Delete tests: `test_table_pager.cpp`, `test_table_header_sort.cpp`, `test_table_row_checks.cpp`
- Modify: `ListPage` must not include the deleted headers (pager UI inlined)
- Modify: `ListPageFrame` — if `buildList` still creates `QTableWidget`+`TablePager` and nothing calls it, remove list-mode table/pager or leave dashboard-only API; Metrics must keep working
- Modify: bench — remove old `QTableWidget` comparison
- Modify: CMake lists

Grep gates (must be empty):

```bash
git grep -n 'TablePager\|TableHeaderSort\|TableRowChecks\|MembersPage\|CatalogPage\|CirculationPage\|ArchivePage' \
  applications/vlms/
```

- [ ] **Step 1: Delete helpers + update ListPage/ListPageFrame/CMake/tests/bench**
- [ ] **Step 2:** `ctest -L ui` and `ctest -L bench` — Expected: PASS
- [ ] **Step 3: Commit (optional)** `refactor(ui): remove QTableWidget list helpers after ListPage migration`

---

## Verification (phase exit)

```bash
cmake --build build -j$(nproc)
cd build && ctest -L ui --output-on-failure
cd build && ctest -L bench --output-on-failure
git grep -n 'TablePager\|TableHeaderSort\|TableRowChecks\|class MembersPage\|class CatalogPage\|class CirculationPage\|class ArchivePage' applications/vlms/
```

Expected: all tests PASS; grep empty (except comments/docs if any — prefer zero).

Open PR `redesign/3-model-list-page` → `Beta`.

---

## Self-review (Phase 3)

| Spec item | Task |
|---|---|
| QueryState / ListSpec / ColumnSpec / FacetSpec / PageAction / PreviewContent | 1–2 |
| PagedQueryModel (ticks, revealId, DataEvents, signals) | 3 |
| ListPage debounce, actions, shortcuts, preview, retry | 4 |
| Seven specs | 5, 7, 9, 11 |
| Page replacements Members→Catalog→Circulation→Archive | 6, 8, 10, 12 |
| test_vlms_presentation | 5+ |
| test_vlms_bench + old-path comparison then removal | 13–14 |
| Remove TablePager/HeaderSort/RowChecks + four pages | 6–12, 14 |
| No ResizeToContents; prebuilt display strings | 3–5 constraints |
| Checklist rows 1–2, 9, 13, 16 | specs + ListPage tests |

**Placeholder scan:** none intentional.
**Type consistency:** `LoanScope` / `ActionContext` / `PageAction::Needs` / `PreviewContent` names match across tasks.
