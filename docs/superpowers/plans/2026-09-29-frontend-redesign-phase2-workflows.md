# Frontend Redesign Phase 2: Workflows

> **For agentic workers:** REQUIRED SUB-SKILL: subagent-driven-development.

**Goal:** Move repository writes out of pages into workflows.

**Branch:** `redesign/2-workflows` (worktree `.worktrees/redesign-1-foundations`)
**Depends on:** Phase 1 foundations (DataEvents, Navigator, ErrorPresenter, DialogFactory)

## Done

| Unit | Status |
|---|---|
| BulkRunner | done |
| BookWorkflow + CatalogPage | done |
| MemberWorkflow + MembersPage | done |
| LoanWorkflow + CirculationPage + LoanHistoryActions | done |
| Book/Member loan dialogs take LoanWorkflow | done |

## Remaining

1. **ArchiveWorkflow** — `restore`, `purge`, `reuseNumber`, `showLoans`; wire `ArchivePage`
2. **test_vlms_workflows** — FakeDialogFactory / FakeErrorPresenter / FakeNavigator tests per use case
3. Confirm pages no longer call `save*` / `archive*` / `returnLoan` / `extendLoan` directly (grep gate)

## Verification so far

`ctest -L ui`: **290/290** passing after LoanWorkflow wiring.
