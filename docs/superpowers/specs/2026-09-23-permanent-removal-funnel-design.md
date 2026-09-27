# Permanent removal funnel — design

*2026-09-23*

## The problem

Delete means two different things today, and which one you get depends on a checkbox.

The Members page asks "delete this member?" with a *Move to archive* box ticked by default; clearing
it calls `purgeMember` and the record is gone. The Catalogue and Circulation pages ask a plain
yes/no and always archive — there is no permanent removal for a book, a copy or a loan anywhere in
the application. So one page can destroy a record from the live list, two cannot, and `purgeMember`
does not even require the member to have been archived first.

What is wanted instead is one shape for all four record types: **delete archives, and permanent
removal exists only in the Archive**. A record has to be put down before it can be destroyed, and
at every step short of the last one the decision can be taken back.

## The funnel

| | Live page: Delete | Archive: Permanently remove |
|---|---|---|
| **Member** | yes/no → archived. Refused while any loan of theirs is unreturned. | Allowed when no loan row names them. Takes `resources/members/<id>` with it. |
| **Book** | yes/no → archived, copies with it. Refused while any copy is out. | Allowed when the title has no copies left at all. Takes `resources/books/<id>` with it. |
| **Copy** | removing the row in the book editor archives it. Refused while it is out. | Allowed when no loan row names it. The book stays; the local number rejoins the free list. |
| **Loan** | yes/no → archived. The button is disabled while the loan is open. | Always allowed — a loan has nothing beneath it. |

The rule that makes this a funnel rather than two doors: **every purge refuses a record that is not
archived.**

A loan can be destroyed outright because it is the bottom of the stack; everything else is gated on
what still points at it. That makes the funnel walkable to the end rather than a dead stop: a member
who has borrowed since 2022 goes Circulation → Delete each loan → Archive/Loans → Permanently remove
each → Members → Delete → Archive/Members → Permanently remove. It is deliberately laborious. The
alternative — a cascade that takes the loans with the member — would let one confirmation erase years
of circulation history, which is the thing this design exists to prevent.

### Why a book needs its copies gone first

A title is permanently removable only once it has **no copies at all**, so the librarian removes
each archived copy individually and then the title. The gate that actually protects data is "no loan
names this copy", and removing twelve copies in one confirmation would check it just as strictly —
but the friction is the point here, and each copy gets looked at on its own.

The schema already leans this way: `book_copies.book_id` is `ON DELETE CASCADE` while
`loans.book_copy_id` has no cascade at all (`database/schema.sql:141`), with `PRAGMA foreign_keys =
ON`. Deleting a book would therefore drag its copies down and SQLite would refuse the moment one of
them is named by a loan. Requiring zero copies means we never rely on that raw constraint error
reaching the librarian in a language they read.

## What each page changes

**Members** loses the checkbox. `deleteMember` keeps its open-loans pre-check and its *Go to loans*
button, then asks a plain yes/no and archives. `purgeMember` is no longer reachable from the live
page, and `members.moveToArchive` and `members.deleteBlockedByHistory` retire with it.

**Catalogue** gains the pre-check Members already has. Today it confirms first and only then
discovers `error.book.hasActiveLoans` coming back from `archiveBook`
(`applications/vlms/src/ui/catalog/CatalogPage.cpp:770`). Being asked "delete this book?" only
to be told afterwards that it was never possible is the long way round to the same no — the comment
in `MembersPage::deleteMember` already says exactly that, so the three pages should agree.

**Circulation** is unchanged. Its Delete is already yes/no → archive, and the button is already
disabled while the loan is open.

**Archive** gains a third button, `Permanently remove`, beside Restore and Reuse. It is destructive,
so it carries its own confirmation naming what is about to go, and it is **disabled when its gate is
shut** rather than complaining on click.

A disabled button only works if the row says why, so each list must show the number that blocks it:

- **Books** already does. The list has a Copies column, and `BookRecord::totalCopies` under
  `ArchiveScope::Archived` counts exactly the copies that block the title. It reads 0 when the title
  is free to go.
- **Members** and **Copies** each gain a **Loans** column — the count of loan rows naming that member
  or copy. 0 means removable, and the librarian can see how much of the funnel is left to walk.
  It counts **every** loan row, archived ones included: an archived loan still blocks the purge, so a
  column that skipped them would read 0 beside a button that refuses.
- **Loans** needs no column; nothing gates it.

## Core

**No schema change.** No v7, no migration. Every gate is a question the existing tables already
answer.

The Archive page does not ask Core whether a row may go — it reads the column it is already showing.
`Permanently remove` is enabled when the Loans column reads 0 (members, copies), when Copies reads 0
(books), or unconditionally (loans). Core then enforces the same rules again on the way in with
localised errors. The UI decides what to offer; the repository decides what is allowed.

Four purges, each refusing a record that is not archived:

- `MemberRepository::purgeMember` — exists (`libraries/Core/src/MemberRepository.cpp:681`), tightened
  to require `archived_at IS NOT NULL`. Its `MemberRemovalBlock::LoanHistory` block stays, and it
  already removes `resources/members/<id>`.
- `CatalogRepository::purgeBook` — refuses while the title has **any** copy, archived or live. Not
  the scoped count the list shows: the check must not depend on which list asked. Removes
  `resources/books/<id>`. In practice the two counts cannot disagree — restoring a copy un-archives
  its book (`BookCopyStore::restoreCopy`), so an archived title never holds a live copy — but the
  purge should not be the place that assumes it.
- `CatalogRepository::purgeCopy` — refuses while any loan row names it. The book stays.
- `CirculationRepository::purgeLoan` — archived is the only condition.

Two new record fields carry the new columns: `MemberRecord::loanCount` and
`BookCopyRecord::loanCount`, each a scalar subquery over `loans`, which is indexed on both
`member_id` and `book_copy_id`.

New string keys, all three tables in `libraries/Core/src/Strings.cpp`, guarded by
`test_core_StringsParity`: the button label, the two column headings, a confirmation per type, and
the errors `error.member.notArchived`, `error.book.notArchived`, `error.book.hasCopies`,
`error.copy.notArchived`, `error.copy.hasHistory`, `error.loan.notArchived`.

## The free local numbers

Two stocks, `arabic` and `foreign`, each with its own accession counter; `AR-`/`FR-` is a prefix on
`global_copy_id`, derived from the local number and moved with it. `nextCopyNumber` is
`MAX(CAST(local_id AS INTEGER)) + 1` per source (`libraries/Core/src/BookCopyStore.cpp:47`), so a
permanently removed number would otherwise be lost for good — and the gaps are not hypothetical. The
live database already holds:

```
arabic:  13,797 copies, numbers 1–19,773  → 5,976 numbers missing in range
foreign:  6,483 copies, numbers 1–8,682   → 2,199 numbers missing in range
```

**A number is free when no `book_copies` row holds it — live or archived.** Computed, never stored:
there is no pool table to drift out of sync, a permanently removed number rejoins the list by
itself, and the 8,175 historic gaps are available from the first run. It cannot tell a number freed
on purpose from one the 2022 import never used, and it does not try to.

`CatalogRepository::listFreeLocalNumbers(source, limit)` reads the stock's held numbers in one pass
— `SELECT CAST(local_id AS INTEGER) … WHERE source = :source ORDER BY 1` — and walks the sorted list
in C++, emitting each gap until `limit`. Deliberately not a recursive CTE with a `NOT EXISTS`:
`CAST(local_id AS INTEGER)` is not indexed, so that shape scans the copies table once per candidate,
~19,773 times for the arabic stock. One sorted read of 13,797 integers takes a few milliseconds.

### Who picks

The gaps are too many and their provenance too uncertain to hand out automatically. A gap may exist
because a book was lost or withdrawn while the paper register still carries that number against it,
and reissuing it would put two different books under one number in the library's own records. So the
number is never chosen for the librarian:

- In the book editor, the local-number cell of a **newly added** copy row gets a delegate returning
  an editable `QComboBox`: the next incremented number first and selected, then the free numbers for
  that stock ascending, capped at the lowest 100. A drop-down with 5,976 entries is not a choice.
- Typing a number by hand still works and is validated exactly as today.
- Existing rows are untouched, and so is the Reuse flow's locked row (`addReservedRow`).
- `createBook`'s initial copies keep incrementing, unasked. Book creation mints several numbers at
  once with nobody looking at them, which is precisely where a gap must not be taken silently.

Nothing changes unless the librarian opens the list.

### The Archive's Reuse button is a different thing, and stays

`Reuse` moves a number off an archived copy that **still exists and is still restorable**, inside the
book save's transaction (`BookWrite::releaseFromCopyId`). The free list holds numbers whose copy is
gone for good. Offering an archived copy's number in the editor drop-down would silently strip a
record someone can still restore, so the two routes stay separate.

## Tests

Core tests are judged by `ctest`, never by running `./build/bin/test_vlms_core` directly — the
schema path, data directory and TZ come from per-test `ENVIRONMENT` in `cmake/TestUtils.cmake`, and
16 tests fail without them.

**Core (`test_vlms_core`)**

- Each of the four purges refuses an unarchived record, and succeeds on a clean archived one.
- `purgeMember` refuses with loan history, and takes `resources/members/<id>` with it.
- `purgeBook` refuses while a copy exists, and takes the cover folder. Seeding a copy-less book means
  archiving its only copy with `saveCopies(bookId, {})` — `createBook` rejects `initialCopyCount < 1`
  with `error.book.minCopies`.
- `purgeCopy` refuses while a loan names it, succeeds otherwise, and the book survives.
- The funnel end to end: archive a loan, purge it, and the member it blocked becomes purgeable.
- `listFreeLocalNumbers` — gaps found, kept per stock, empty result when the stock is contiguous, cap
  honoured.

**UI (`test_vlms_ui`)**

- The Archive button's enabled state per type, and the two new Loans columns.
- The Members page no longer showing a checkbox, and its Delete archiving.
- The Catalogue refusing an open-loan title before it confirms rather than after.
- The copies table offering the free numbers on a new row, with the incremented number still the
  default and still what a save uses when nobody opens the list.

**A trap to design around.** A purge test that seeds a single member or book **cannot fail** — the
same trap the loan-history tests hit, where every loan in the database belonged to the one seeded
book, so a missing filter still passed. Each purge test seeds a second record that must survive, and
each new gate is confirmed to fail without its check.
