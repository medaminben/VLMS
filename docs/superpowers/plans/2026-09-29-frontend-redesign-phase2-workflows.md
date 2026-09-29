# Frontend Redesign Phase 2: Workflows (in progress)

> **For agentic workers:** Continue from BookWorkflow/BulkRunner already on
> branch `redesign/2-workflows`. REQUIRED SUB-SKILL: subagent-driven-development.

**Goal:** Move all repository writes out of pages into BookWorkflow,
MemberWorkflow, LoanWorkflow, ArchiveWorkflow + BulkRunner; pages call
workflows only.

**Done so far:**
- `BulkRunner` wraps `runBulkAction` and emits `DataEvents`
- `BookWorkflow` with add/edit/archive/showLoans/manageCategories

**Remaining tasks:**
1. Wire `CatalogPage` to `BookWorkflow` (constructor + action methods)
2. `MemberWorkflow` + wire `MembersPage`
3. `LoanWorkflow` + remove duplicated checkout/return/extend from pages and `LoanHistoryActions`
4. `ArchiveWorkflow` + wire `ArchivePage`
5. Own workflows from Application/MainWindow composition root
6. `test_vlms_workflows` suite with FakeDialogFactory / FakeErrorPresenter / FakeNavigator
7. Delete write paths from pages once covered

**Depends on:** Phase 1 foundations (`redesign/1-foundations` / commit `5dc8c94`)
