---
title: Remove
order: 12
summary: Archive a title, a copy, a member, or a loan, then remove it for good.
---

::: note
`Delete` never destroys a record. It archives it. The live list loses it, and the [Archive](archive) can restore it. Permanent removal is a separate step, only in the Archive.
:::

## A book

Highlight the title. Leave the ticks clear if you mean this title alone. Click `Delete`.

![Delete asks before it archives one title.](shot:rm-book-confirm)

`No` leaves the title on the Catalogue. `Yes` archives the title and its copies. Their local numbers stay with them, so they can be reused. See [Reuse a local number](task-reuse-number).

If a copy is still out, Delete is refused. Return that loan first.

![Delete refused, because a copy is on loan.](shot:rm-book-refused)

## A copy

`Edit` the title, open the Copies tab, highlight the line, and click `Remove copy`. A copy that is still out cannot be removed. `OK` saves the book without that copy. The copy is archived, not destroyed.

![The Copies tab, with Remove copy.](shot:rm-copy-row)

1. `Remove copy`

## A member

`Delete` on Members archives the member, unless a loan of theirs is still out. Then the message offers `Go to Loans`.

![Delete refused while a book is still out.](shot:mem-delete-blocked)

## A loan

Only a returned loan can be archived from Circulation. Highlight it and click `Delete`.

![Delete asks before it archives one returned loan.](shot:rm-loan-confirm)

An open or overdue loan has to be returned first. See [Return or extend](task-return-extend).

## Several at once

Tick the rows, including with the header tick. `Delete` then names how many, and acts on each ticked row. A row that cannot be archived is skipped. See [Ticks](getting-started#ticks).

![The row tick and the header tick.](shot:gs-ticks)

![Delete with three titles ticked.](shot:rm-bulk-confirm)

## Permanently

When the archived record should not come back, open the Archive and follow the [order](archive#purge-order). A loan can go at any time. A copy waits until no loan names it. A book waits until it has no copies. A member waits until no loan names them.

![Permanently remove, for a loan. This cannot be undone.](shot:arc-purge-confirm)
