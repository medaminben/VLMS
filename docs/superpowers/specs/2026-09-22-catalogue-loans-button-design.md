# Catalogue: Categories… moves into the book dialog, Loans takes its place

Date: 2026-09-22
Status: approved for implementation

## What changes

Two edits to the Catalogue page, one consequence in Core.

1. **`Categories…` leaves the Catalogue button pad** and reappears inside the book dialog,
   on the Category row, immediately after the category drop-down. The business logic is
   untouched: it still opens `CategoryManagerDialog` over the same `CatalogRepository`.
2. **A `Loans` button takes the freed slot**, between the search field and `Add Book` —
   the same position and the same behaviour the Members page already gives its own `Loans`
   button. It opens the loan history of the selected book.

Nothing else on the page moves.

## Why the button moves

Managing the category list is something a librarian does *while cataloguing a book* — the
moment they discover the category they need does not exist yet. Reaching it meant closing
the dialog, clicking `Categories…`, and starting the book again. On the Category row the
list is one click from the drop-down that needs it, and the Catalogue's button pad is left
for actions that operate on the selected row, which is what every other button there does.

## The Catalogue page

### Button order

    [ search field ]        [ Loans ] [ Add Book ] [ Edit ] [ Delete ]

`Loans` is a secondary button, added to the frame before `m_addButton`, matching
`MembersPage::buildUi` exactly.

### When it is enabled

Disabled when no row is selected. With a row selected it is enabled only if that book has
at least one loan on record — `countLoans` with `bookId` set, `ArchiveScope::Any`, and
`limit = 1`. This is `MembersPage::updateLoansButton` with `memberId` swapped for `bookId`,
and it is called from the same place: the end of the selection-changed handler, and
wherever the handler clears the details panel the button is disabled alongside it.

A book that has never been borrowed therefore offers a dead button rather than an empty
dialog, which is the behaviour the Members page already established.

### Where the repository comes from

`CatalogPage` currently holds only a `CatalogRepository&`. It gains a second reference:

```cpp
CatalogPage(CatalogRepository& repository,
            CirculationRepository& circulation,
            QWidget* parent = nullptr);
```

`MainWindow` already has `app->circulation()` — it passes the same instance to
`MembersPage` one line above. Three test files construct `CatalogPage(*m_catalog)` and
gain a `CirculationRepository` in their fixtures, which `test_ui_MemberSort` already shows
how to build.

## The book loan dialog

`BookLoansDialog`, new, in `applications/vlms/src/ui/catalog/`. It is
`MemberLoansDialog` with one column swapped, and it is a separate class rather than a
parameter on the existing one: the two differ in their query, their window title and one
column, and a single class taking "either a member id or a book id" would carry a mode
flag through every method for no gain.

    Member | Copy | Borrowed | Due | Returned | Status

The title column is gone — it is the same on every row, so it belongs in the window title,
which reads `catalog.loanHistoryTitle` with `{name}` replaced by the book's title. Member
names come from `LoanRecord::memberName`, which `listLoans` already selects; no new join
and no second query.

Everything else is inherited from the member dialog verbatim: 820×420, `SelectRows`,
`NoEditTriggers`, widget-level sort via `enableWidgetTableSort`, a localised `Close` box,
`ArchiveScope::Any` because an archived loan still happened, `limit = 1000`, an em dash
for an unreturned loan, and the three-state status label (returned / overdue / open).

## Core: the query gains a book

`LoanQuery` gains `std::int64_t bookId = 0`, beside the `memberId` it mirrors. In
`LoanSql::filterClause`:

```cpp
if (query.bookId > 0) {
    sql += " AND bc.book_id = :book_id ";
}
```

and the matching `bind` in `bindFilters`. Every loan query — `listLoans`, `countLoans`,
`rankOfLoan` — already does `INNER JOIN book_copies bc ON bc.id = l.book_copy_id`, so the
clause needs no new join and applies uniformly. Filtering on `bc.book_id` rather than a
list of copy ids means the history covers **every copy the book has ever had**, including
copies since archived, which is what a history should show.

`memberId` and `bookId` compose: both set would mean "this member's loans of this book".
Nothing asks for that today, and nothing prevents it.

## Strings

Three tables in `Strings.cpp`, guarded by `test_core_StringsParity`.

| Key | ar | fr | en |
|---|---|---|---|
| `catalog.loans` | الإعارات | Prêts | Loans |
| `catalog.loanHistoryTitle` | سجل الإعارات — {name} | Historique des prêts — {name} | Loan history — {name} |

`catalog.categories` keeps its key and its text; only its use site moves, from
`CatalogPage::retranslateUi` to `BookEditorDialog::retranslateUi`. The Members keys are
not reused: `members.loans` naming a Catalogue button would be a lie the next reader has
to unpick.

## Right-to-left

The Category row is a `QFormLayout` row holding a horizontal layout of drop-down then
button; under RTL the row mirrors as a whole, so the button lands at the left edge and
still reads as trailing the drop-down. Nothing is positioned absolutely, and no test
asserts a pixel position for it.

## Testing

`test_ui_CatalogLoans`, new, in `applications/vlms/test/src/`:

- **`TheLoansButtonIsDisabledUntilABookWithLoansIsSelected`** — a seeded book with no
  loans leaves the button disabled; selecting a book with one loan enables it.
- **`TheHistoryListsEveryLoanOfEveryCopy`** — two copies, three loans, one of them
  returned; the dialog shows three rows.
- **`AnArchivedCopysLoanStillAppears`** — the loan of a copy since archived is still in
  the history, which is what `ArchiveScope::Any` plus `bc.book_id` buys.
- **`TheHistoryNamesTheMemberNotTheTitle`** — the first column holds the borrower's name.

`test_core_LoanBookFilter`, new, in `libraries/Core/test/`: `listLoans` and `countLoans`
with `bookId` set return only that book's loans, and `bookId = 0` changes nothing.

Existing suites that must keep passing: `test_ui_CatalogSort`,
`test_ui_CatalogLocalNumber`, `test_ui_ArchiveCatalog` (all three construct `CatalogPage`
and need the new argument), and `test_core_StringsParity`.

`BookEditorDialog` already has no test that counts its buttons, so the moved
`Categories…` is verified by driving the real app rather than by a new pixel test — the
button either opens `CategoryManagerDialog` or it does not, and that is one connect.

## Assumptions

Recorded because implementation began before they were separately confirmed:

- The moved `Categories…` appears in **both** the Add and the Edit book dialog. They are
  one class, `BookEditorDialog`, and splitting them to hide the button in one would be a
  larger change than the request implies.
- Closing `CategoryManagerDialog` from inside the book dialog **repopulates the category
  drop-down** and preserves the current selection — `populateCategories()` already reads
  the current data and restores it, so this is a call, not new logic.
- The Catalogue page is live-scope; the **Archive** page's book list gets neither change.
