# Row ticks and bulk actions — design

*2026-09-23*

## The problem

Every destructive action in the application works on one row: the highlighted one. Clearing forty
returned loans from Circulation, or permanently removing a page of archived members, means forty
rounds of select → Delete → Yes. The funnel in
`2026-09-23-permanent-removal-funnel-design.md` is deliberately a layer at a time; it was never meant
to be a row at a time as well.

What is wanted: a tick box on each row of the list tables, a select-all box in the header that works
on the rows on screen, and the destructive buttons acting on every ticked row at once.

## Decisions

| Question | Decision |
|---|---|
| Some ticked rows cannot be acted on | Ask first: "2 of 14 can't be archived — archive the other 12?", listing why. |
| Which actions go bulk | Catalogue / Members / Circulation **Delete**, Archive **Restore** and **Purge** (all four types), book editor Copies-tab **Remove**. Reuse stays single-row: it gives one number to one book. |
| Nothing ticked | The button acts on the highlighted row, exactly as today. |
| How long a tick lasts | Until the rows are reloaded: page, page size, filter, search, sort, or the action's own refresh. |
| Select all | Ticks the rows currently on screen, not the whole query beyond the page. Under **ALL** that is every row the filter matches; the confirmation states the count. |
| Where the box sits | Inside the first cell, not in a new column. |

Bulk does not cascade. Deleting a ticked batch of members still refuses any whose loans are out, and
purging still needs the layer beneath it gone first. The funnel keeps its order; only the number of
clicks per layer changes.

## The tick column — `TableRowChecks`

`src/ui/TableRowChecks.{h,cpp}`. Attaches to a `QTableWidget` whose column-0 item carries the row.

- **Row boxes.** Column-0 items get `Qt::ItemIsUserCheckable`. The box sits at the leading edge of
  the first cell, and under RTL it moves to the right edge, which Qt does unaided. The id already
  lives on column 0 (`Qt::UserRole`), so no column index moves. A separate column was rejected
  because it would shift every delegate, sort key and id lookup, and the 62 `item(row, N)` uses in
  the tests.
- **Header box.** `CheckHeaderView`, a `QHeaderView` subclass installed as the horizontal header,
  paints a tri-state box (none / some / all) at the leading edge of section 0. A press inside the
  box toggles every row on screen and does **not** sort; a press anywhere else in the header sorts
  as before.
- **Interface.** `checkedIds()` (from `Qt::UserRole` on column 0), `checkedRows()` (row indices, for
  tables whose rows have no id yet), `clear()`, and the signal `checkedCountChanged(int)`.
- **Ticks and the highlighted row stay separate.** Ticking moves neither the highlighted row nor the
  details panel. Edit and Loans follow the highlighted row and ignore ticks.

Attached on: Catalogue, Members, Circulation, the Archive (all four types; the type switch rebuilds
the columns and clears the ticks), and `BookCopiesTable`. Not on Metrics or the loan-history
dialogs.

Each page calls `clear()` from its row-refresh method, which is the one path every reload already
goes through.

### The Copies tab

`BookCopiesTable` differs in three ways:

- Column 0 is the editable local number, drawn by `FreeLocalNumberDelegate`. The delegate must paint
  the check indicator as well; a click on the box ticks it, and a double-click on the number still
  edits it.
- Rows are in memory until Save, and a new row has no id, so the table uses `checkedRows()`.
- The table is never reloaded while it is being edited. Ticks go when their rows are removed, or when
  `loadCopies` loads another book.

Remove's blockers are already flags on each row: `kCopyOnLoanRole` gives `book.copy.cannotRemoveOnLoan`,
and `kCopyReservedRole` marks a number that must stay on a live row until save.

## Core — the `can…` checks

Each action that can refuse gets a read-only twin returning `Status`: `ok()` when the action would go
through, otherwise the same validation key the action gives.

| Call | Refuses with |
|---|---|
| `CatalogRepository::canArchiveBook(id)` | `error.book.hasActiveLoans` |
| `CatalogRepository::canPurgeBook(id)` | `error.book.notArchived`, `error.book.hasCopies` |
| `CatalogRepository::canPurgeCopy(id)` | `error.copy.notArchived`, `error.copy.hasHistory` |
| `MemberRepository::canArchiveMember(id)` | `error.member.archiveHasLoans` |
| `MemberRepository::canPurgeMember(id)` | `error.member.notArchived`, `error.member.deleteHasLoans`, `error.member.hasHistory` |
| `CirculationRepository::canArchiveLoan(id)` | `error.loan.archiveOpen` |
| `CirculationRepository::canPurgeLoan(id)` | `error.loan.notArchived` |

A missing row gives `notFound`, as the actions do. Each action calls its own `can…` first, inside
its transaction, so the rule is written once and the check cannot drift from the action.

Restore gets no checks. `restoreBook`, `restoreCopy`, `restoreMember` and `restoreLoan` refuse only
`notFound` / `notArchived`, which for a row the Archive is showing means someone else changed it in
the meantime. Bulk Restore is the loop, with those failures in the summary.

`bookHasOpenLoans` and `MemberRepository::removalBlock` stay as they are. The single-row flows keep
using them for their own messages (the member "Go to loans" button), so a single-row action behaves
exactly as today.

## The bulk flow — `runBulkAction`

`src/ui/BulkAction.{h,cpp}`:

```cpp
struct BulkRow { qint64 id; int row; QString label; };
struct BulkActionTexts { /* title, noun, verb, confirm keys */ };

int runBulkAction(QWidget* parent,
                  const BulkActionTexts& texts,
                  const QList<BulkRow>& rows,
                  const std::function<VLMS::Status(const BulkRow&)>& check,
                  const std::function<VLMS::Status(const BulkRow&)>& act);
```

It returns the number of rows acted on. The page decides what to refresh.

1. **Sort.** Run `check` on each row into *passes* and *blocked (label, reason)*. The reason is the
   translated error key.
2. **Ask.**
   - Nothing blocked: "Archive 14 books?" (Yes / No).
   - Some blocked: "2 of 14 books can't be archived:", one line per blocked row (label — reason),
     cut after 10 lines with "…and N more", then "Archive the other 12?" (Yes / No).
   - All blocked: the same list with OK only; nothing happens.
3. **Run.** Loop `act` over *passes*. Each call is the existing single-row action in its own
   transaction (`SqliteSession::transaction` does not nest), so each row succeeds or fails on its
   own. A `QProgressDialog` with `minimumDuration` ≈ 500 ms appears only for a slow run. Cancel
   stops between rows, and rows already done stay done.
4. **Report.** Show a summary only if something failed during the run or the run was cancelled:
   "12 archived, 1 failed: …".
5. The page refreshes once, which clears the ticks. The Archive emits `recordRestored()` once per
   batch, not once per row.

### Per page

| Page / action | check | act | label |
|---|---|---|---|
| Catalogue Delete | `canArchiveBook` | `archiveBook` | title |
| Members Delete | `canArchiveMember` | `archiveMember` | full name |
| Circulation Delete | `canArchiveLoan` | `archiveLoan` | member — title |
| Archive Restore | always ok | `restore*` | per type |
| Archive Purge | `canPurge*` | `purge*` | per type |
| Copies Remove | row flags | `removeRow` (removes the row in memory, from the bottom up) | local number |

After a bulk delete, Catalogue and Members refresh their filters as the single-row path does
(`refreshLanguages` / `refreshCategories`, `refreshAllFilters`).

### Strings

New keys for the prompt lines ("N of M can't…", "…the other N?", "…and N more", the summary) and a
noun and verb for each page. They go in all three tables, so `test_core_StringsParity` guards them.

## Testing

**Core** (`test_vlms_core`, judged by `ctest`):
- For each `can…`: `ok` on a row that would pass, and each refusal key on a row that would refuse.
- For each refusal, a paired test: the action on the same row returns the same key.
- Every test seeds a second record, so a check that ignores its id fails.

**UI** (`test_vlms_ui`):
- `TableRowChecks`: select-all ticks every row on screen, and turns to "some" when one row is
  unticked. A press on the header box does not sort; a press on the header text does. `clear()`
  runs on reload. Ticking does not move the highlighted row. The box sits at the right edge under RTL.
- `runBulkAction`, driven by fake check and act calls, with the message boxes answered through
  `ModalTest.h`: the sorting into *passes* and *blocked* is right, No does nothing, and a
  failure during the run shows in the summary.
- Each page: two ticked rows are both archived. With none ticked, only the highlighted row is archived.
  A blocked ticked row stays.
- Copies tab: ticked rows are removed and the on-loan and reserved rows stay. A double-click on the
  number still opens its editor. A render test reads back that the check indicator is actually
  painted inside `FreeLocalNumberDelegate`'s cell.

**By hand:** run the real app, and screenshot a ticked Catalogue page, the "2 of 14" prompt, and a
bulk Archive purge.
