# Frontend redesign — use-case checklist status

Completed 2026-09-29 on branch `redesign/6-docs-release` (worktree
`.worktrees/redesign-1-foundations`). Each row was run with the Phase 6
command; evidence is the ctest summary line.

Also run:

```text
cd build && ctest -L ui --output-on-failure
→ 100% tests passed, 0 tests failed out of 422

cd build && ctest -L bench --output-on-failure
→ 100% tests passed, 0 tests failed out of 1
```

## Checklist

| # | Use case | Exact test name(s) | Command result |
|---|---|---|---|
| 1 | Search/filter/sort/page books | `BookListSpec.MapsFacetsSortAndPagingIntoBookQuery`; `BookListSpec.DisplayRowsUsePrebuiltStrings`; `BookListSpec.RetranslateDoesNotQuery`; `BookListSpec.RankOfMatchesRepository`; `BookListSpec.PreviewLoadsCoverPathAndFields`; `BookListSpec.DependsOnBooksCopiesAndCategories` | **PASS** — 6/6 |
| 2 | Preview cover | `BookListSpec.PreviewLoadsCoverPathAndFields` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 3 | Add/edit book | `BookWorkflow.Add*` (3: cancel, success, missing title); `BookWorkflow.Edit*` (4: full record, cancel, success, no selection); `Smoke.AddABookInTheRealEditor`; `Smoke.EditABookInTheRealEditor` | **PASS** — 9/9 |
| 4 | OCR cover | `BookEditorDialog.OcrButtonOffersInstalledLanguageMenu` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 5 | Lookups completion | `BookEditorDialog.LookupsPopulateAuthorCompleter`; `BookEditorDialog.LookupsPopulatePublisherCompleter` | **PASS** — 100% tests passed, 0 tests failed out of 2 |
| 6 | Categories | `BookWorkflow.Categories*` (3: closed without edits emits nothing, create emits once, failed write shows its error once); `BookWorkflow.CategoryRenameAndDeleteGoThroughTheWorkflow`; `Smoke.OpenAndCloseTheCategoryManager` | **PASS** — 5/5 |
| 7 | Archive books | `BookWorkflow.Archive*` (4: confirm, decline, open loan, many through confirmBulk); `Smoke.ArchiveABookFromARealQuestion` | **PASS** — 5/5 |
| 8 | Book loan history | `LoanHistoryDialog.BookPinSetsQueryStateBookId` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 9 | Members list | `MemberListSpec.MapsFacetsSortAndPagingIntoMemberQuery`; `MemberListSpec.DisplayRowsUsePrebuiltStrings`; `MemberListSpec.RetranslateDoesNotQuery`; `MemberListSpec.RankOfMatchesRepository`; `MemberListSpec.PreviewLoadsPhotoPathAndFields`; `MemberListSpec.DependsOnMembersAndLoans` | **PASS** — 6/6 |
| 10 | Add/edit member | `MemberWorkflow.Add*` (3: cancel, success, bad email); `MemberWorkflow.Edit*` (3: full record, cancel, success); `Smoke.SaveAMemberUnchangedInTheRealEditor` | **PASS** — 7/7 |
| 11 | Archive blocked → Navigator | `MemberWorkflow.ArchiveBlockedByOpenLoansOffersGoToLoansAndDoesNotArchive`; `MemberWorkflow.ArchiveBlockedAndDismissedDoesNotNavigate`; `MemberWorkflow.ArchiveManySkipsMembersWithLoans` | **PASS** — 3/3 |
| 12 | Member loan history | `LoanHistoryDialog.MemberPinSetsQueryStateMemberId` | **PASS** — 100% tests passed, 0 tests failed out of 1 |
| 13 | Loan filters | `LoanListSpec.FilterOpenShowsOnlyOpenLoans`; `LoanListSpec.FilterOverdueShowsOnlyOverdueLoans`; `LoanListSpec.FilterReturnedShowsOnlyReturnedLoans`; `LoanListSpec.FilterAllShowsEveryLoan` | **PASS** — 100% tests passed, 0 tests failed out of 4 |
| 14 | Checkout/return/extend | `LoanWorkflow.Checkout*` (4: cancel, prefill, success, lent copy); `LoanWorkflow.Return*` (3: cancel, success, already returned); `LoanWorkflow.Extend*` (3: cancel, success, earlier date); `Smoke.CheckoutExtendAndReturnThroughTheRealLoanDialogs` | **PASS** — 11/11 |
| 15 | Archive loans | `LoanWorkflow.Archive*` (3: confirm, decline, many skipping open loans) | **PASS** — 3/3 |
| 16 | Archived lists | `ArchivedMemberSpec.*` (5); `ArchivedBookSpec.*` (5); `ArchivedCopySpec.*` (5); `ArchivedLoanSpec.*` (5) | **PASS** — 100% tests passed, 0 tests failed out of 20 |
| 17 | Restore/purge | `ArchiveWorkflow.Restore*` (3: decline, confirm, many through confirmBulk); `ArchiveWorkflow.Purge*` (2: decline, confirm); `Smoke.ArchiveRestoreAndPurgeAMember` | **PASS** — 6/6 |
| 18 | Reuse number | `ArchiveWorkflow.ReuseNumber*` (5: zero id, cancel at chooser, cancel in editor, success on a new book, failed save keeps the number); `Smoke.ReuseAnArchivedNumberOnANewBook` | **PASS** — 6/6 |
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

## Fixes after the 2026-09-29 review

Re-run on `redesign/6-docs-release` after the review of the stack against the
spec. `ctest` → 430/430 (`-L ui` 422, `-L bench` 1, the rest are backend
libraries).

| Review finding | Fix |
|---|---|
| Workflows built their own dialogs and message boxes; `DialogFactory` was mostly unused | Every window goes through `DialogFactory`; workflow tests run headless and fail if a real window opens |
| `CategoryManagerDialog` wrote categories through its own repository; Categories emitted even when nothing changed | Writes go through `BookWorkflow` via `CategoryEditor`; Categories is emitted once, only after a change |
| `ReuseNumberFlow` saved books itself; `reuseNumber` always reported success | `ArchiveWorkflow::reuseNumber` saves, reports a failed save once and returns it |
| `BulkRunner` never used `confirmBulk` | Confirmation, progress and the failure summary go through `DialogFactory` |
| Facet lists and metrics page held repositories | They read through `BookFacetSource`, `MemberFacetSource`, `MetricsSource` |
| Lists stale after related changes | `dependsOn` covers categories, loan counts, copy counts, titles and names shown |
| Fakes shipped in `src/` | Moved to `test/src/Fakes.h` |
| No `test_vlms_smoke` | Added: 8 end-to-end passes through the real dialogs |
| Layer lint not run by ctest and whitelisting violations | `ui_layer_deps` is a ctest test with no exceptions |
