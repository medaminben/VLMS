# Frontend Redesign Phase 2: Workflows

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move repository writes out of pages into workflows.

**Architecture:** Four workflows (`BookWorkflow`, `MemberWorkflow`, `LoanWorkflow`, `ArchiveWorkflow`) plus `BulkRunner` own all repository write calls. Pages call workflow methods and refresh on `DataEvents`. Dialogs still open from workflows (Phase 4 moves forms/Lookups).

**Tech Stack:** C++17, Qt 6 Widgets, CMake ≥ 3.21, GoogleTest, existing `VLMS::Core` / `VLMS::Repositories`.

**Spec:** `docs/superpowers/specs/2026-09-29-frontend-redesign-design.md` (Phase 2).

**Worktree:** `<repo>/.worktrees/redesign-1-foundations` on branch `redesign/2-workflows`.

## Global Constraints

- Dependencies point downwards only (shell → views → presentation → workflows → services → repositories).
- Only workflows call repository write methods: `save*`, `create*`, `update*`, `delete*`, `archive*`, `restore*`, `purge*`, `set*Image`, `returnLoan`, `extendLoan`.
- Every public workflow method returns `Core::Status` after showing any error itself via `ErrorPresenter`.
- Cancel returns `Status::ok()` and emits nothing on `DataEvents`.
- Backend libraries and schema do not change.
- Do not invent Phase 3+ features (`ListSpec`, Lookups refactor, LanguageChange).
- Commits OK after each task when green.

## Done

| Unit | Status |
|---|---|
| BulkRunner | done |
| BookWorkflow + CatalogPage | done |
| MemberWorkflow + MembersPage | done |
| LoanWorkflow + CirculationPage + LoanHistoryActions | done |
| Book/Member loan dialogs take LoanWorkflow | done |

## File map (remaining)

| Path | Responsibility |
|---|---|
| `applications/vlms/src/ui/workflows/ArchiveWorkflow.h` / `.cpp` | restore / purge / reuseNumber / showLoans |
| `applications/vlms/src/ui/archive/ArchivePage.h` / `.cpp` | Takes `ArchiveWorkflow&`; drops direct restore/purge/reuse repo writes and `reuseNumberRequested` |
| `applications/vlms/src/ui/MainWindow.h` / `.cpp` | Owns `ArchiveWorkflow`; passes it to `ArchivePage` |
| `applications/vlms/CMakeLists.txt` | Add ArchiveWorkflow sources |
| `applications/vlms/test/src/WorkflowTestSupport.h` | Add `ArchiveWorkflowHarness` |
| `applications/vlms/test/src/test_archive_workflow.cpp` | ArchiveWorkflow unit tests (Fake* doubles) |
| `applications/vlms/test/src/test_book_workflow.cpp` | BookWorkflow unit tests |
| `applications/vlms/test/src/test_member_workflow.cpp` | MemberWorkflow unit tests |
| `applications/vlms/test/src/test_loan_workflow.cpp` | LoanWorkflow unit tests |
| `applications/vlms/test/CMakeLists.txt` | Add workflow test sources to `test_vlms_ui` (label `ui`; dedicated `test_vlms_workflows` target if easy, else same suite) |
| Archive/page tests that construct `ArchivePage` | Update constructors |

---

### Task 5: ArchiveWorkflow + ArchivePage wiring

**Files:**
- Create: `applications/vlms/src/ui/workflows/ArchiveWorkflow.h`
- Create: `applications/vlms/src/ui/workflows/ArchiveWorkflow.cpp`
- Modify: `applications/vlms/src/ui/archive/ArchivePage.h`
- Modify: `applications/vlms/src/ui/archive/ArchivePage.cpp`
- Modify: `applications/vlms/src/ui/MainWindow.h`
- Modify: `applications/vlms/src/ui/MainWindow.cpp`
- Modify: `applications/vlms/CMakeLists.txt`
- Modify: any test that constructs `ArchivePage` (pass a workflow or harness)

**Interfaces:**
- Consumes: `MemberRepository`, `CatalogRepository`, `CirculationRepository`, `DialogFactory`, `ErrorPresenter`, `DataEvents`, `Navigator`, `BulkRunner`, `LoanWorkflow` (for showLoans if opening live loan dialogs later; for Phase 2 keep `ArchiveLoansDialog` opened from workflow), `runReuseNumberFlow`
- Produces:

```cpp
namespace VLMS {

class ArchiveWorkflow {
public:
    ArchiveWorkflow(Repositories::MemberRepository& members,
                    Repositories::CatalogRepository& catalog,
                    Repositories::CirculationRepository& circulation,
                    DialogFactory& dialogs,
                    ErrorPresenter& errors,
                    DataEvents& events,
                    Navigator& navigator,
                    BulkRunner& bulk);

    Core::Status restore(QWidget* parent, ArchiveType type,
                         const QList<qint64>& ids, const QList<QString>& labels);
    Core::Status purge(QWidget* parent, ArchiveType type,
                       const QList<qint64>& ids, const QList<QString>& labels);
    Core::Status reuseNumber(QWidget* parent, qint64 copyId);
    Core::Status showLoans(QWidget* parent, ArchiveType type, qint64 id,
                           const QString& displayName);
};

}
```

Map `ArchivePage::Type` ↔ `ArchiveType` 1:1 (Members/Books/Copies/Loans).

**Behaviour (port from today's `ArchivePage`):**

- **restore:** empty ids → ok. One id → `askYesNo` (`archive.restore` / `archive.restoreConfirm`); on yes call `restoreMember` / `restoreBook` / `restoreCopy` / `restoreLoan`; on error `m_errors.show`; on success `m_events.emitChanged` with matching `Entity` and `{id}`. Many ids → `BulkRunner::run` with always-ok check, restore act, emit matching Entity (bulk already emits).
- **purge:** same shape; single confirm keys: `archive.purgeConfirmMember|Book|Copy|Loan`. Bulk uses `canPurge*` then `purge*`. On success purge of live-visible entities: **do not** require DataEvents for purge (today's page skips emit on single purge; BulkRunner will emit — pass `EntitySet{}` or the matching entity; prefer matching entity with succeeded ids for consistency with BulkRunner API — if BulkRunner always emits, that is fine).
- **reuseNumber:** load archived copy via catalog; if missing/empty localId return ok; call `runReuseNumberFlow`; on true emit `Entity::Copies` (and Books if new book) — at minimum `Entity::Copies | Entity::Books` with `{copyId}` like today's `notifyRestored`.
- **showLoans:** build `LoanQuery` pinned by type; open `ArchiveLoansDialog` (keep as today until Phase 4). Loans type → ok no-op.

**ArchivePage changes:**
- Constructor gains `ArchiveWorkflow& workflow` (keep repos for reads).
- `restoreSelected` / `purgeSelected` / `reuseSelected` / `showLoanHistory` call workflow; then `refreshRows()` on ok (workflow already emitted events).
- Remove `notifyRestored`, remove `reuseNumberRequested` signal (MainWindow must not connect it — Reuse lives in workflow).
- Drop includes of BulkAction / ReuseNumberFlow from page if unused.

**MainWindow:** create `m_archiveWorkflow` after other workflows; pass to `ArchivePage`; remove any `reuseNumberRequested` connection if present.

- [ ] **Step 1: Implement ArchiveWorkflow.h/.cpp and CMake entry**
- [ ] **Step 2: Wire ArchivePage + MainWindow + fix constructing tests**
- [ ] **Step 3: Build and run `ctest -L ui` (or focused archive tests)**
- [ ] **Step 4: Commit** `feat(ui): add ArchiveWorkflow and route ArchivePage writes through it`

---

### Task 6: Workflow unit tests (`test_vlms_workflows` content)

**Files:**
- Modify: `applications/vlms/test/src/WorkflowTestSupport.h` (add `ArchiveWorkflowHarness`)
- Create: `applications/vlms/test/src/test_archive_workflow.cpp`
- Create: `applications/vlms/test/src/test_book_workflow.cpp`
- Create: `applications/vlms/test/src/test_member_workflow.cpp`
- Create: `applications/vlms/test/src/test_loan_workflow.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt`

**Preferred CMake:** add a second `build_gtest_executable` named `test_vlms_workflows` with LABELS `ui` (and optionally `workflows`), linking `vlms_ui` + same deps as `test_vlms_ui`, sources = the four workflow test files + `test_main.cpp`. If that fights discovery macros, append the four sources to `test_vlms_ui` instead — either satisfies the design's `test_vlms_workflows` coverage.

**Test style:** Use `UiTest` / temp DB fixtures already used by archive/catalog tests. Drive workflows with `FakeDialogFactory` / `FakeErrorPresenter` / `FakeNavigator` / real `DataEvents`. No `qWait`, no real modals.

**Minimum cases (each file):**

`test_book_workflow.cpp`:
- Cancel add → no event, no error
- Successful archive single (askYesNo true) → Books changed with id
- Archive cancel → no event

`test_member_workflow.cpp`:
- Archive blocked by open loans → `warnWithAction`; if true, navigator `showMemberLoans` called; no archive
- Archive confirm → Members changed

`test_loan_workflow.cpp`:
- Return cancel → no event
- Checkout cancel → no event
- Archive single confirm → Loans changed

`test_archive_workflow.cpp`:
- Restore cancel → no event
- Restore confirm member → Members changed
- Purge cancel → no event
- showLoans Members opens without crash (dialog may need ModalTest or skip exec — prefer constructing query path; if dialog.exec blocks, use a test-only seam or only assert Status::ok after stubbing — **simplest:** test restore/purge/reuseNumber only; for showLoans assert Status::ok when id<=0 / Loans type)

Use existing seed helpers from `test_archive_page.cpp` / `UiTest.h` / `vlms_testsupport`.

- [ ] **Step 1: Write failing tests / harness**
- [ ] **Step 2: Implement until green**
- [ ] **Step 3: `ctest -L ui` green**
- [ ] **Step 4: Grep gate — pages must not call `restoreMember|restoreBook|restoreCopy|restoreLoan|purgeMember|purgeBook|purgeCopy|purgeLoan|runReuseNumberFlow` directly (only workflows / ReuseNumberFlow.cpp)**
- [ ] **Step 5: Commit** `test(ui): add workflow suite for Book/Member/Loan/Archive`

## Verification

`ctest -L ui` all pass. Grep gate above clean.
