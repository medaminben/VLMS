---
title: Archive
order: 6
summary: What Delete put aside, and what can be restored or removed for good.
---

Delete archives. The title, the copy, the member, or the loan leaves the live list and appears here. It can be restored. It is not destroyed until `Permanently remove`, and only when the [order](#purge-order) below allows it.

The type list on the left chooses what you are looking at: `Members`, `Books`, `Copies`, or `Loans`. Search works the same way as on the live screens.

The filters under the type list change with the type, and changing the type puts them back on `All`.

- `Members` and `Loans` have the member filters from [Members](members). For a loan, they filter by the member who borrowed it, and the year is the year the loan was made.
- `Books` and `Copies` have the language, category, and cover filters from the [Catalogue](catalogue). A copy is filtered by the title it belongs to.

## Books

![Archived books. The Copies column is how many archived copies the title still has.](shot:arc-books)

1. The type list.
2. The Catalogue's filters: language, category, and cover.
3. Search.
4. `Archived copies` — copies still attached to the title. A title can be permanently removed only when this is zero.
5. The cover, or the placeholder when there is none.
6. `Restore`
7. `Permanently remove`

## Copies

![Archived copies. Reuse local number is available when the copy still holds a number.](shot:arc-copies)

1. The Catalogue's filters, applied to the title each copy belongs to.
2. The cover of that title.
3. `Loans` — every loan of this copy, including archived loans. Permanent removal needs a zero here.
4. `Reuse local number` — gives that number to a new or existing title. See [Reuse a local number](task-reuse-number).

## Loans

![Archived loans.](shot:arc-loans)

1. The member filters, for the member who borrowed.
2. The cover of the borrowed title.
3. The member's photograph, or `No photo`.

Under the pictures are the member, the membership number, the copy, the day it came back, the day it was archived, and the loan's notes.

A loan can be restored, or permanently removed at any time. Removing it does not put the copy back on the shelf: the copy was already returned, or it was archived with its title.

## Members

![Archived members. The Loans column counts every loan, archived ones included.](shot:arc-members)

1. The member filters.
2. The member's photograph, or `No photo`.
3. `Loans`
4. `Loans` — the member's loan history.

A member can be permanently removed only when this column is zero.

## Loan history

`Loans` sits after the search on the `Members`, `Books`, and `Copies` lists. It opens every loan of the highlighted member, title, or copy, archived loans included: the member, the membership number, the copy, the dates, the status, and when the loan was archived. The history is only for reading; nothing can be lent or returned from it. The button is grey when the row has never been lent.

## Restore

Highlight a row, or tick several, and choose `Restore`. The confirmation names what will return to the live lists. `No` leaves the Archive unchanged.

![The Restore confirmation.](shot:arc-restore-confirm)

Ticks work here as on every list. See [Ticks](getting-started#ticks).

## Permanently remove {#purge-order}

`Permanently remove` asks once, then deletes the record. It cannot be undone.

![Permanently remove, for one loan.](shot:arc-purge-confirm)

::: warning
Permanent removal cannot be restored. Archive first, and remove for good only when you are sure.
:::

The application allows it in this order:

1. A loan, at any time.
2. A copy, only when `Loans` is zero. An archived loan still counts.
3. A book, only when `Archived copies` is zero. Remove or restore its copies first.
4. A member, only when `Loans` is zero.

`No` on the confirmation leaves the record in the Archive.
