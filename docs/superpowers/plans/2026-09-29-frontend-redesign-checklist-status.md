# Frontend redesign — use-case checklist status

Completed 2026-09-29 on branch `redesign/6-docs-release` (worktree
`.worktrees/redesign-1-foundations`). Each row was run with the Phase 6
command; evidence is the ctest summary line.

Also run:

```text
cd build && ctest -L ui --output-on-failure
→ 100% tests passed, 0 tests failed out of 375

cd build && ctest -L bench --output-on-failure
→ 100% tests passed, 0 tests failed out of 1
```

## Checklist

| # | Use case | Exact test name(s) | Command result |
|---|---|---|---|
| 1 | Search/filter/sort/page books | `BookListSpec.MapsFacetsSortAndPagingIntoBookQuery`; `BookListSpec.DisplayRowsUsePrebuiltStrings`; `BookListSpec.RetranslateDoesNotQuery`; `BookListSpec.RankOfMatchesRepository`; `BookListSpec.PreviewLoadsCoverPathAndFields`; `BookListSpec.DependsOnBooksAndCopies` | **PASS** — 100% tests passed, 0 tests failed out of 6 |
| 2 | Preview cover | `BookListSpec.PreviewLoadsCoverPathAndFields` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 3 | Add/edit book | `BookWorkflow.AddCancelEmitsNothingAndShowsNoError`; `BookWorkflow.AddSuccessEmitsBooksChanged`; `BookWorkflow.EditCancelEmitsNothing` | **PASS** — 100% tests passed, 0 tests failed out of 3 |
| 4 | OCR cover | `BookEditorDialog.OcrButtonOffersInstalledLanguageMenu` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 5 | Lookups completion | `BookEditorDialog.LookupsPopulateAuthorCompleter`; `BookEditorDialog.LookupsPopulatePublisherCompleter` | **PASS** — 100% tests passed, 0 tests failed out of 2 |
| 6 | Categories | `BookWorkflow.CategoriesCancelStillEmitsCategoriesChanged` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 7 | Archive books | `BookWorkflow.ArchiveSingleConfirmEmitsBooksChanged`; `BookWorkflow.ArchiveCancelEmitsNothing` | **PASS** — 100% tests passed, 0 tests failed out of 2 |
| 8 | Book loan history | `LoanHistoryDialog.BookPinSetsQueryStateBookId` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 9 | Members list | `MemberListSpec.MapsFacetsSortAndPagingIntoMemberQuery`; `MemberListSpec.DisplayRowsUsePrebuiltStrings`; `MemberListSpec.RetranslateDoesNotQuery`; `MemberListSpec.RankOfMatchesRepository`; `MemberListSpec.PreviewLoadsPhotoPathAndFields`; `MemberListSpec.DependsOnMembers` | **PASS** — 100% tests passed, 0 tests failed out of 6 |
| 10 | Add/edit member | `MemberWorkflow.AddCancelEmitsNothing`; `MemberWorkflow.EditCancelEmitsNothing` | **PASS** — 100% tests passed, 0 tests failed out of 2 |
| 11 | Archive blocked → Navigator | `MemberWorkflow.ArchiveBlockedByOpenLoansOffersGoToLoansAndDoesNotArchive` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 12 | Member loan history | `LoanHistoryDialog.MemberPinSetsQueryStateMemberId` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 13 | Loan filters | `LoanListSpec.FilterOpenShowsOnlyOpenLoans`; `LoanListSpec.FilterOverdueShowsOnlyOverdueLoans`; `LoanListSpec.FilterReturnedShowsOnlyReturnedLoans`; `LoanListSpec.FilterAllShowsEveryLoan` | **PASS** — 100% tests passed, 0 tests failed out of 4 |
| 14 | Checkout/return/extend | `LoanWorkflow.ReturnCancelEmitsNothing`; `LoanWorkflow.CheckoutCancelEmitsNothing`; `LoanWorkflow.ArchiveSingleConfirmEmitsLoansChanged` (also matches `LoanHistoryDialog.ReturnActionCallsLoanWorkflow`) | **PASS** — 100% tests passed, 0 tests failed out of 4 |
| 15 | Archive loans | `LoanWorkflow.ArchiveSingleConfirmEmitsLoansChanged` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 16 | Archived lists | `ArchivedMemberSpec.*` (5); `ArchivedBookSpec.*` (5); `ArchivedCopySpec.*` (5); `ArchivedLoanSpec.*` (5) | **PASS** — 100% tests passed, 0 tests failed out of 20 |
| 17 | Restore/purge | `ArchiveWorkflow.RestoreCancelEmitsNothing`; `ArchiveWorkflow.RestoreConfirmMemberEmitsMembersChanged`; `ArchiveWorkflow.PurgeCancelEmitsNothing` | **PASS** — 100% tests passed, 0 tests failed out of 3 |
| 18 | Reuse number | `ArchiveWorkflow.ReuseNumberZeroIdIsOkWithoutCrash`; `ArchiveWorkflow.ReuseNumberCancelLeavesArchivedNumber` | **PASS** — 100% tests passed, 0 tests failed out of 2 |
| 19 | Archived loan history | `LoanHistoryDialog.ArchivedScopeUsesArchivedOrAny` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 20 | Metrics dashboard | `MetricsPage.RefreshPopulatesOverviewLabels` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 21 | LanguageChange RTL | `LanguageChange.SetUiLocaleSendsLanguageChangeToTopLevelWidgets`; `LanguageChange.NoLanguageChangedSignalExists`; `LanguageChange.ListPageRebuildsColumnHeadersWithoutReloadQuery`; `LanguageChange.RtlLayoutAppliedForArabic` (also matches `test_ui_DialogTranslations.StandardButtonLabelsSurviveALanguageChange`) | **PASS** — 100% tests passed, 0 tests failed out of 5 |
| 22 | Theme/manual/licence | `test_ui_ThemeModes.*` (6); `test_ui_PreviewTheme.*` (4); `test_ui_LicenceDialog.*` (7); `test_ui_ManualLocation.*` (15); `test_ui_ManualButtonIcon.RightToLeftDrawsTheArabicQuestionMark` | **PASS** — 100% tests passed, 0 tests failed out of 33 |

## Gaps closed in this task

| Preflight gap | Fix |
|---|---|
| `BookListSpec.Preview` | Renamed suite `BookListSpecTest` → `BookListSpec` |
| `BookWorkflow.(Add\|Edit)` | Renamed suite; renamed `CancelAdd*` → `AddCancel*`; added `AddSuccess*`, `EditCancel*` |
| `BookWorkflow.Categories` | Added `CategoriesCancelStillEmitsCategoriesChanged` |
| `MemberWorkflow.(Add\|Edit)` | Renamed suite; added `AddCancel*`, `EditCancel*` |
| `LoanListSpec.Filter` | Renamed suite `LoanListSpecTest` → `LoanListSpec` |
| `ArchiveWorkflow.ReuseNumber` | Added `ReuseNumberZeroId*` and `ReuseNumberCancel*` |
| `MetricsPage\|DashboardPage` | Added `MetricsPage.RefreshPopulatesOverviewLabels` |
| `MemberWorkflow.ArchiveBlocked*` | Renamed suite `test_ui_MemberWorkflow` → `MemberWorkflow` |
| `Archived*Spec.*` | Renamed `Archived*SpecTest` → `Archived*Spec` |
| Design Test column row 20 | Updated to `MetricsPage.*` |
