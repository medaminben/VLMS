# Frontend redesign: one list page, workflows, change events

Approved 2026-09-29. Restructures `applications/vlms/src/ui` without losing
any use case. The backend libraries and the database schema do not change.
Work lands phase by phase on the `Beta` branch; `Beta` merges into `main`
once, at the end.

Diagrams: the current architecture is at
https://claude.ai/artifact/MCaV1S2SMcyxiqAzsQN7bn and the first sketch of
this design at https://claude.ai/artifact/8Hxu8hN96Pgdh2RQgXaYEF. Where the
sketch and this spec differ, this spec wins.

## Why

The UI follows a pattern, but only by convention:

- Catalog, Members, Circulation and Archive repeat the same methods
  (`buildList`, `currentXQuery`, `refreshX`, `onSelectionChanged`,
  `onSortChanged`, `selectedXId`/`selectXId`, `retranslateUi`). Nothing
  enforces the shape.
- Each page is view and controller in one class.
- Tables are `QTableWidget`s filled item by item. With Show All, the default,
  the Catalog allocates about 25,000 `QTableWidgetItem`s on every refresh.
- Checkout, return and extend exist twice: in `CirculationPage` and in
  `LoanHistoryActions`.
- A restore in the archive refreshes the live pages by calling their
  `retranslateUi()`.
- Cross-page jumps are lambdas in `MainWindow`.
- Dialogs hold whole repositories to look up a few values.
- Language changes travel through a hand-written `retranslateUi()` chain.

## Decisions

| Topic | Decision |
|---|---|
| Scope | One spec for the whole redesign, delivered in phases |
| Toolkit | Qt Widgets, as now |
| Visible change | Small harmonisation allowed: same button order, context menu and shortcuts on every list; one loan history dialog; 250 ms search debounce. Manual screenshots are regenerated. |
| List page | Runtime `ListSpec` interface, one `ListPage`, one `PagedQueryModel` (approach A) |
| Tests | UI test suite rewritten around specs and workflows |
| Delivery | Phase branches → PR into `Beta` → CI green → merge. `Beta` → `main` once at the end. |

## Architecture

Code is organised by layer. Dependencies point downwards only.

| Layer | Folder | Units | May use |
|---|---|---|---|
| Shell | `ui/shell/` | `Application` (composition root), `MainWindow`, `Navigator` | all layers below |
| Views | `ui/views/` | `ListPage`, `DashboardPage`, `LoanHistoryDialog`, form dialogs | presentation; `DialogFactory` interface |
| Presentation | `ui/presentation/` | `ListSpec` and its 7 implementations, `PagedQueryModel`, `QueryState`, `ColumnSpec`, `FacetSpec`, `PageAction` | repositories, read calls only |
| Use cases | `ui/workflows/` | `BookWorkflow`, `MemberWorkflow`, `LoanWorkflow`, `ArchiveWorkflow`, `BulkRunner`, `DialogFactory`, `ErrorPresenter`, `DataEvents` | repositories, all calls; `Navigator` interface |
| Services | `ui/services/` | `OcrService` (wraps `Ocr::Job`), `Theme` | Ocr, Core |

Rules:

- Only workflows call repository methods that write: `save*`, `create*`,
  `update*`, `delete*`, `archive*`, `restore*`, `purge*`, `set*Image`,
  `returnLoan`, `extendLoan`.
- Specs only read. Dialogs never hold a repository; they receive a `Lookups`
  interface.
- Views never call each other. Navigation goes through `Navigator`,
  refreshes through `DataEvents`.
- `Application` builds every object and passes it down by constructor.
  Nothing calls `qobject_cast<Application*>(QApplication::instance())`.
- If the database fails to open, `Application` builds no specs or workflows
  and `MainWindow` shows today's placeholder pages.

Unchanged: backend libraries, schema, `Strings::t()` keys (new keys only for
the new menu and shortcut labels), the three languages including
right-to-left Arabic, theme handling, manual button, licence dialog.

## Component contracts

### QueryState

```cpp
struct QueryState {
    QString search;
    QHash<QString, QStringList> facets;   // facet key -> ticked values
    QString sortKey;
    bool ascending = true;
    int pageSize = 0;                     // 0 = Show All, today's default
    int page = 1;
    Repositories::ArchiveScope scope = Repositories::ArchiveScope::Live;
    std::optional<LoanScope> pinned;      // member, book or copy id
};
```

Each spec maps it onto its repository query (`BookQuery`, `CopyQuery`,
`MemberQuery`, `LoanQuery`). All four already carry search, facet lists,
`archive`, `limit`/`offset` and `sortColumn`/`sortAscending`; `LoanQuery`
carries `memberId`/`bookId`/`copyId` for `pinned`.

### ListSpec

```cpp
class ListSpec {
public:
    virtual ~ListSpec() = default;
    virtual QString id() const = 0;
    virtual QList<ColumnSpec> columns() const = 0;   // key, titleKey, width, sortKey
    virtual QList<FacetSpec> facets() const = 0;     // key, titleKey, values loader
    virtual QList<PageAction> actions() const = 0;
    virtual EntitySet dependsOn() const = 0;
    virtual Core::Status load(const QueryState& state) = 0;
    virtual void retranslate() = 0;                  // rebuild display rows, no query
    virtual int rowCount() const = 0;
    virtual int totalCount() const = 0;
    virtual qint64 idAt(int row) const = 0;
    virtual QVariant cell(int row, int column, int role) const = 0;
    virtual Core::Result<int> rankOf(qint64 id, const QueryState& state) const = 0;
    virtual PreviewContent preview(qint64 id) const = 0;
};
```

Implementations: `BookListSpec`, `MemberListSpec`, `LoanListSpec`,
`ArchivedMemberSpec`, `ArchivedBookSpec`, `ArchivedCopySpec`,
`ArchivedLoanSpec`.

Performance rules, required because Show All loads 3,071 books and 4,712
copies from the bundled database:

- `load()` builds every display string once (formatted dates, translated
  statuses, flags). `cell()` only reads prebuilt values.
- `retranslate()` rebuilds display strings from the kept records without
  querying.
- Covers and photos load in `preview()` for the selected row only.

### PagedQueryModel

A `QAbstractTableModel` over one `ListSpec*` and one `QueryState`.

- `setQuery(state)`, `reload()`, `sort(column, order)` call `spec->load()`
  and reset the model.
- `data()` and `headerData()` forward to `spec->cell()` and
  `spec->columns()`.
- Ticks are a `QSet<qint64>` of ids, exposed through `Qt::CheckStateRole`.
  `checkedIds()` returns them; `tickAll()` ticks every row currently listed,
  as today. Ids no longer listed after a load are unticked.
- `revealId(id)` uses `spec->rankOf()` to move to the right page and select
  the row.
- `setSpec(spec)` swaps the list; the Archive type switch uses it.
- Signals: `loaded(int total)`, `failed(Core::Error)`.
- Subscribes to `DataEvents::changed` and reloads when the change overlaps
  `spec->dependsOn()`.

The `QTableView` in `ListPage` uses column widths from `ColumnSpec` and a
fixed row height. No `ResizeToContents` on list views.

### PageAction

```cpp
struct PageAction {
    QString id;
    QString labelKey;
    QKeySequence shortcut;
    enum class Needs { Nothing, One, Some } needs;
    std::function<Core::Status(const ActionContext&)> run;   // ids + parent widget
    std::function<bool(const ActionContext&)> enabled = {};  // optional extra rule
};
```

`ListPage` builds buttons, context menu entries and shortcuts from the same
list. Standard order and shortcuts on every list:

| Action | Shortcut | Needs |
|---|---|---|
| Add | Ctrl+N | Nothing |
| Edit | Enter | One |
| Loans | Ctrl+L | One |
| Archive / Restore | Del | Some |
| Purge | Shift+Del | Some |
| Tick all | Ctrl+A | Nothing |
| Refresh | F5 | Nothing |

Each spec lists only the actions its page offers today; the table fixes
their order and shortcuts, not their presence.

Shortcut scope: Enter, Del, Shift+Del, Ctrl+L and Ctrl+A are bound to the
table (`Qt::WidgetWithChildrenShortcut` on the `QTableView`), so in the
search box Enter still searches at once and Ctrl+A still selects the text.
Ctrl+N and F5 are bound to the page.

### Workflows

Each workflow takes the repositories it needs plus `DialogFactory&`,
`ErrorPresenter&`, `DataEvents&` and `Navigator&`. Every public method
returns `Core::Status` after it has shown any error itself.

| Workflow | Methods |
|---|---|
| `BookWorkflow` | `add()`, `edit(id)`, `archive(ids)`, `showLoans(id)`, `manageCategories()` |
| `MemberWorkflow` | `add()`, `edit(id)`, `archive(ids)`, `showLoans(id)` |
| `LoanWorkflow` | `checkout(prefill)`, `returnLoan(id)`, `extend(id)`, `archive(ids)` |
| `ArchiveWorkflow` | `restore(type, ids)`, `purge(type, ids)`, `reuseNumber(copyId)`, `showLoans(type, id)` |

`BulkRunner` keeps `runBulkAction`'s check-then-act logic. Confirmation goes
through `DialogFactory::confirmBulk(plan)`.

### DialogFactory, Lookups, dialogs

`DialogFactory` is an interface with one method per dialog. Each takes a
form struct and returns `std::optional<Form>`; empty means cancelled. Forms:
`BookForm`, `MemberForm`, `LoanForm`, `ReturnForm`, `ExtendForm`,
`CategoryEdits`, plus `confirmBulk(BulkPlan)`, `askYesNo(...)` and
`warnWithAction(...)`.

Dialogs take their form and a `Lookups` interface:

- `CatalogLookups`: author names, publisher names, categories, book
  languages, free local numbers, suggested copy ids.
- `CirculationLookups`: borrowable members, available copies.
- `MemberLookups`: suggested membership number, cities.

`BookEditorDialog` also takes `OcrService&`. `BookLoansDialog`,
`MemberLoansDialog` and `ArchiveLoansDialog` become one `LoanHistoryDialog`:
a `ListPage` on a `LoanListSpec` with `pinned` set, using `LoanWorkflow`.

### DataEvents and Navigator

```cpp
enum class Entity { Books = 1, Copies = 2, Members = 4, Loans = 8, Categories = 16 };
// EntitySet = QFlags<Entity>

class DataEvents : public QObject {
    Q_OBJECT
signals:
    void changed(EntitySet what, QList<qint64> ids);
};
```

`Navigator` routes: `show(PageId)`, `showMemberLoans(membershipNumber)`,
`showBook(id)`, `showArchive(type)`. `MainWindow` implements it.

## Data flow

**Search.** `ListPage` restarts a 250 ms timer on each keystroke; Enter
searches at once. On timeout it sets `search`, resets `page` to 1 and calls
`model.setQuery()`. The spec loads, the model resets and emits `loaded`, the
pager updates, ticks and selection are reconciled. Selection change calls
`spec.preview(id)`. Facet and sort changes enter at `setQuery()`.

**Save.** An action calls a workflow method. The workflow reads the record,
builds a form, calls `DialogFactory`, builds the `Write` struct, calls the
repository, then either shows the error or emits `changed`. Models whose
specs depend on that entity reload; `DashboardPage` reloads on any change.
The calling page then calls `revealId(id)`.

**Bulk.** `BulkRunner` checks each id, asks once, runs the passing ids and
emits one `changed` with every id that succeeded.

**Language.** `Application::setUiLocale` keeps setting the locale, saving
it, applying layout direction and installing the Qt catalogue. It then sends
`QEvent::LanguageChange` to every top-level widget. Widgets re-read their
labels in `changeEvent`. `ListPage` re-reads column, action and facet
titles and calls `spec.retranslate()`. The `languageChanged` signal and the
`retranslateUi()` chain are removed.

**Navigation.** A blocked member archive offers the jump through
`DialogFactory::warnWithAction`; on yes, `Navigator::showMemberLoans`
switches to Circulation with the membership number as search.

## Error handling

| Kind | Example | Caught by | Shown as |
|---|---|---|---|
| Field input | bad email, return before borrow | the dialog, before OK is accepted (`firstValidationFailure`, `LoanPolicy::validate*`) | inline; the dialog stays open |
| Repository refusal | copy not available, duplicate key | the workflow | `ErrorPresenter::show` → `showRepoError` |
| Database | locked file, disk full | workflow, or model on load | same presenter; on load failure `ListPage` keeps the old rows and shows a strip with Retry |

- One place shows an error: the workflow shows it, callers only read the
  returned `Status`.
- Specs never show dialogs.
- A bulk run with some failed rows ends with one summary listing each failed
  row and its reason, and one `changed` event for the rows that succeeded.
- Cancel returns `Status::ok()` and emits nothing.
- Each save stays one repository call inside one transaction, as today.
- `ErrorPresenter` is an interface; tests use a recorder.

## Testing

The UI test suite is rewritten. Four levels:

| Target | Covers | Setup |
|---|---|---|
| `test_vlms_presentation` | Per spec: query mapping (facets, sort, scope, pinned), display rows, `retranslate()` without a query, `rankOf`, `preview`, `dependsOn` | Real repositories on a temporary copy of a small fixture database; no widgets |
| `test_vlms_workflows` | Every use case: repository call made, event emitted, cancel emits nothing, error reported once with its key, bulk skip and summary, blocked archive routes through `Navigator` | `FakeDialogFactory`, `FakeErrorPresenter`, `FakeNavigator`, a `DataEvents` recorder; no `qWait`, no real modals |
| `test_vlms_ui` | `PagedQueryModel` paging, sort, ticks by id, `tickAll`, `revealId`, reload on `changed`, Retry strip; `ListPage` action enabling, shortcuts, context menu, debounce; each dialog's validation and returned form; RTL; `LanguageChange` | Offscreen `QApplication`; dialogs built and filled directly; `ModalTest.h` only for message boxes |
| `test_vlms_smoke` | One pass per page through the real `DialogFactory`: add, edit, archive, restore, purge, checkout, return, extend, reuse number | Fewer than 15 tests, `runAndAnswerModals` |

**Show-All benchmark**, target `test_vlms_bench`, CTest label `bench`:

- Copies `database/vlms.db` (3,071 books, 4,712 copies) to a temporary
  directory.
- Measures Catalog with Show All: first load, refresh after a search, repaint
  of the table at 1280×800.
- Fails if the refresh takes 300 ms or more on the CI runner.
- Until the end of phase 3 it also measures the old `QTableWidget` path in
  the same run and fails if the new path is slower. That comparison is
  removed with the last `QTableWidget` list page.
- Built in every CI job; skipped at run time in the `sanitizers` job.

Rules:

- A phase deletes the old tests for code it removes in the same PR that adds
  the replacement tests.
- The coverage checklist below is part of the `Beta` → `main` review. Every
  row must name a passing test.
- `core-only` builds (`BUILD_APPS=OFF`) are unaffected.

## Use-case coverage checklist

| # | Use case | Today | Target | Test |
|---|---|---|---|---|
| 1 | Search, filter, sort, page books | `CatalogPage`, `BookFacetFilters` | `ListPage` + `BookListSpec` | `BookListSpec.*` |
| 2 | Preview cover and details | `CatalogPage` preview | `BookListSpec::preview` | `BookListSpec.Preview*` |
| 3 | Add or edit a book with copies and cover | `CatalogPage` + `BookEditorDialog`, `BookCopiesTable` | `BookWorkflow::add/edit` | `BookWorkflow.Add*`, `BookWorkflow.Edit*` |
| 4 | Read a cover with OCR | `BookOcrController` | `OcrService` in `BookEditorDialog` | `BookEditorDialog.Ocr*` |
| 5 | Author/publisher completion, free local numbers | editor calls `CatalogRepository` | `CatalogLookups` | `BookEditorDialog.Lookups*` |
| 6 | Manage categories | `CategoryManagerDialog` | `BookWorkflow::manageCategories` | `BookWorkflow.Categories*` |
| 7 | Archive books, one or many | `CatalogPage::deleteBook` + `runBulkAction` | `BookWorkflow::archive` | `BookWorkflow.Archive*` |
| 8 | Book loan history with checkout, return, extend | `BookLoansDialog` + `LoanHistoryActions` | `LoanHistoryDialog` + `LoanWorkflow` | `LoanHistoryDialog.Book*` |
| 9 | Search, filter, sort, page members | `MembersPage`, `MemberFacetFilters` | `ListPage` + `MemberListSpec` | `MemberListSpec.*` |
| 10 | Add or edit a member with photo, ID image, birth date | `MembersPage` + `MemberEditorDialog`, `ImageViewerDialog` | `MemberWorkflow::add/edit` | `MemberWorkflow.Add*`, `MemberWorkflow.Edit*` |
| 11 | Archive members; jump to their loans if blocked | `MembersPage` + `memberLoansRequested` | `MemberWorkflow::archive` → `Navigator` | `MemberWorkflow.ArchiveBlocked*` |
| 12 | Member loan history | `MemberLoansDialog` | `LoanHistoryDialog` | `LoanHistoryDialog.Member*` |
| 13 | Loans by open / overdue / returned / all | `CirculationPage` filter | facet in `LoanListSpec` | `LoanListSpec.Filter*` |
| 14 | Checkout, return, extend | `CirculationPage` + loan dialogs | `LoanWorkflow` | `LoanWorkflow.*` |
| 15 | Archive loans, one or many | `CirculationPage::archiveLoan` | `LoanWorkflow::archive` | `LoanWorkflow.Archive*` |
| 16 | Browse archived members, books, copies, loans | `ArchivePage` type switch | `ListPage` + four `Archived*Spec` | `Archived*Spec.*` |
| 17 | Restore or purge | `ArchivePage` + `runBulkAction` | `ArchiveWorkflow::restore/purge` | `ArchiveWorkflow.Restore*`, `ArchiveWorkflow.Purge*` |
| 18 | Reuse an archived copy's number | `ReuseNumberFlow` | `ArchiveWorkflow::reuseNumber` | `ArchiveWorkflow.ReuseNumber*` |
| 19 | Archived loan history | `ArchiveLoansDialog` | `LoanHistoryDialog`, archived scope | `LoanHistoryDialog.Archived*` |
| 20 | Metrics, activity periods, top categories | `MetricsPage` | `DashboardPage`, reloads on any change | `DashboardPage.*` |
| 21 | Switch language, including RTL | `LanguageSelector` + `retranslateUi` chain | `QEvent::LanguageChange` | `LanguageChange.*` |
| 22 | Theme toggle, manual, licence | `MainWindow`, `Theme`, `LicenceDialog` | unchanged in the shell | existing tests, ported |

## Phases and delivery

**Phase 0, done 2026-09-29.** `Beta` created from `origin/main` at
`3243443` and pushed. `ci.yml` already runs on pushes to `Beta` and on pull
requests, so no workflow change is needed. `release.yml` runs only on
`main`, so merges into `Beta` cut no release. Branch protection on `Beta`
(require the `default` and `sanitizers` checks) is a repository setting the
owner applies.

**Phase branches** are named `redesign/<n>-<name>`, opened as PRs into
`Beta`, merged only with green CI. At the start of each phase, `main` is
merged into `Beta`.

| # | Phase | Delivers | Removes |
|---|---|---|---|
| 1 | Foundations | `Application` as composition root, `DataEvents`, `Navigator`, `ErrorPresenter`, `DialogFactory` with a real implementation wrapping today's dialogs | `qobject_cast<Application*>` lookups, the `recordRestored` → `retranslateUi()` refresh, `MainWindow` lambdas |
| 2 | Workflows | The four workflows and `BulkRunner`; pages call workflows | Duplicated checkout/return/extend, `LoanHistoryActions` saving itself |
| 3 | Model and list page | `ListSpec`, `PagedQueryModel`, `ListPage`, the benchmark. One PR per page: Members, Catalog, Circulation, Archive. | `TablePager`, `TableHeaderSort`, `TableRowChecks`, the four page classes, the benchmark's old-path comparison |
| 4 | Dialogs | Forms and `Lookups`, `OcrService`, `LoanHistoryDialog` | Repositories in dialogs, the three loan dialogs, `BookOcrController` |
| 5 | Harmonisation | Shared actions, menus and shortcuts, debounce, `QEvent::LanguageChange`, layer folders | `retranslateUi` chain, `ui/catalog`, `ui/members`, `ui/circulation`, `ui/archive` |
| 6 | Docs and release | Manual text in en, fr, ar for menus, shortcuts and the loan history dialog; regenerated screenshots; completed checklist | nothing |

`MetricsPage`'s two small fixed tables stay `QTableWidget`s.

**Beta into main.**

1. One PR `Beta` → `main`: green CI, every checklist row names a passing
   test, owner review.
2. Version: minor. Tag `vX.(Y+1).0` by hand and push the tag before merging
   into `main`, so `release.yml` does not pick the level itself.
3. After the merge, `Beta` is kept or deleted by the owner.

## Out of scope

- Backend libraries, schema, repositories' public API.
- QML or any toolkit change.
- New features. Every visible change is listed under Decisions.
