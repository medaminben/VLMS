---
title: Register a member
order: 9
summary: Register a member, renew them, or end the membership early.
---

## Register

::: step
**Click `Add Member`.**

The dialog gives a membership number. You cannot type one.

![Add Member, as it opens.](shot:reg-open)
:::

::: step
**Fill the name, the date of birth, and the rest.**

`First name`, `Last name`, and `Date of birth` are required. The date is picked from three lists: day, month, and year. The application derives the age group from it, and sets `Active until` to one year ahead, less one day. The status starts at `Active`.

![A new member, with the number, the status, and the last active day marked.](shot:reg-filled)

1. `Membership number` — read only.
2. `Status`
3. `Active until`
:::

::: step
**Click `OK`.**

`Cancel` discards the dialog.
:::

## Renew

Open the member with `Edit`. The status shows `Not active` when the last active day has passed.

::: step
**Open `Status` and choose `Active`.**

![The status list on a member who is not active.](shot:reg-renew)

`Active until` moves to one year ahead, less one day. It updates in the dialog before you save, so you can see the day you are about to store.

![After Active is chosen, the last active day has moved forward.](shot:reg-renewed)
:::

::: step
**Click `OK`.**
:::

To end a membership early, choose `Not active` instead. `Active until` becomes yesterday, and the member can no longer borrow. See [Member status](reference#member-status).

### What if…

- The first name or the last name is empty.
- The date of birth is missing, is not a real date, or is after today.
- The email is filled in and is not a valid address.
