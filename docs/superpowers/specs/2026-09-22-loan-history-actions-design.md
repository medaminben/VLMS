# Loan history dialogs become workbenches

Date: 2026-09-22
Status: approved for implementation
Follows: `2026-09-22-catalogue-loans-button-design.md`

## What changes

The two loan-history dialogs — `BookLoansDialog` on the Catalogue and
`MemberLoansDialog` on the Members page — stop being read-only records. Each grows a
button row:

    [ Loan ] [ Extend ] [ Return ]                              [ Close ]

and the `Loans` button that opens them stops being conditional.

## Why the Loans button stops being conditional

Today it is enabled only when the book (or member) already has a loan. That is backwards
for the commonest case: a book nobody has borrowed yet is exactly the book someone is
about to borrow. The `countLoans` probe goes, `updateLoansButton` with it, and the button
is enabled whenever a row is selected. An empty history is now a *destination* — it opens
on a short line saying the book has not been circulated yet, with `Loan` live beside it.

The same reasoning governs the no-free-copy case below: a control that refuses to act
without saying why is what this change removes, so nothing here is disabled silently.

## Catalogue: the book is fixed, the member is chosen

`Loan` opens `LoanCheckoutDialog` scoped to the book. The copy drop-down holds only that
book's available copies, opened on the lowest — the number the catalogue cell paints
green. The copy *search* row disappears, because the book is already decided; searching
the whole library from a dialog titled after one book is how a librarian lends the wrong
one.

**Every copy out on loan:** the dialog opens and states it, with OK disabled. The librarian
sees the history that explains it — the rows naming who holds each copy are right behind
the dialog.

## Members: the member is fixed, the book is chosen

The mirror image. The member search row and member drop-down are replaced by the member's
name as static text; the copy search and drop-down stay exactly as they are today, because
here the book is the open question.

## One dialog, two scopes

`LoanCheckoutDialog` takes an optional scope rather than growing a second class:

```cpp
struct LoanScope {
    std::int64_t bookId = 0;    ///< fix the book: only its free copies are offered
    std::int64_t memberId = 0;  ///< fix the member: the member rows become a label
    QString label;              ///< the fixed thing's name, for the row and the title
};

LoanCheckoutDialog(CirculationRepository& repository,
                   const LoanScope& scope = {},
                   QWidget* parent = nullptr);
```

Both zero is today's dialog, which is what `CirculationPage` keeps passing. The two fields
are not expected together, and nothing sets both; if both were set the dialog would simply
fix both rows, which is coherent rather than special-cased.

## Extend and Return act on the selected history row

Both are disabled unless the selected row is a loan still out. A returned row disables
both. `CirculationPage` answers this with a warning box, but inside a dialog whose Status
column already says `Returned`, a greyed button is the quieter answer — the reason is on
screen.

They reuse `LoanExtendDialog` and `LoanReturnDialog` unchanged, and the repository calls
`extendLoan` / `returnLoan` that `CirculationPage` already makes. The loan id is already on
the first column's `Qt::UserRole` in both dialogs.

After any of the three succeeds the dialog re-queries itself, so the new or changed row is
visible without closing. The page behind refreshes when the dialog closes, so the Copies
and Available columns and the Members page's loan count follow.

## Core: available copies can be scoped to a book

```cpp
listAvailableCopies(const std::string& search = {}, std::int64_t bookId = 0) const;
```

adding `AND bc.book_id = :book_id` to the query in `CirculationRepository`. The query
already selects from `book_copies bc`, already excludes copies with an unreturned loan,
and already excludes archived copies and archived books — so "this book's free copies" is
one clause, and the ordering it already applies (`b.title`, then `bc.local_id`) puts the
lowest number first within a single book.

`local_id` is ordered `COLLATE NOCASE`, which is lexicographic: "1000" sorts before "505".
The dialog therefore preselects **the first row the query returns**, and the spec claims
no more than that — matching the catalogue's green lead number exactly would mean a
numeric sort the rest of this query does not use, and picking a different free copy is one
click away.

## Strings

Two new keys per table (`Strings.cpp`, Arabic / French / English, guarded by
`test_core_StringsParity`):

| Key | ar | fr | en |
|---|---|---|---|
| `catalog.loanHistoryEmpty` | لم تُعَر هذه النسخة بعد. | Cet ouvrage n'a pas encore été prêté. | This book has not been circulated yet. |
| `members.loanHistoryEmpty` | لم يستعر هذا العضو أي كتاب بعد. | Cet adhérent n'a encore rien emprunté. | This member has not borrowed anything yet. |

The three buttons reuse `circulation.checkout`, `circulation.extend` and
`circulation.return`, which already exist in all three tables. `loan.noCopies` already
exists for the empty copy drop-down.

## What is deliberately not changed

- `CirculationPage` keeps its own buttons and its warning boxes. It passes an empty scope
  and behaves exactly as before.
- Archiving a loan stays a Circulation-page action; the history dialogs do not delete.
- The Archive page gains nothing.

## Testing

Core — `test_core_LoanBookFilter` gains:

- **`AvailableCopiesCanBeScopedToOneBook`** — two books with free copies; scoping returns
  only the one book's, and a copy out on loan is still excluded.

UI — `test_ui_CatalogLoans` and a new `test_ui_MemberLoansActions`:

- **`TheLoansButtonOpensEvenWithNoHistory`** — a book never borrowed: the button is
  enabled and the dialog says so.
- **`LendingFromAnEmptyHistoryAddsTheRow`** — drive the scoped checkout, accept, and the
  history that was empty now has one row.
- **`TheScopedDialogOffersOnlyThisBooksCopies`** — two books with free copies; the copy
  drop-down holds only this book's, and its data ids all belong to it.
- **`ExtendAndReturnFollowTheSelectedRow`** — a returned row disables both; an open row
  enables both.
- **`TheMemberDialogFixesTheMemberNotTheBook`** — the member rows are absent and the copy
  drop-down is populated.

Each test that asserts a scoping must seed a second book (or member) that would appear if
the scope were dropped — the previous round's lesson: a history test seeded with one book
passes even when the filter is deleted.
