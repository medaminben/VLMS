# Member status: one-year expiry, two statuses

Date: 2026-09-23

## Problem

A member has one of four statuses — `subscribed`, `active`, `non_active`, `unsubscribed` —
but only `active` changes anything the app allows: `CirculationRepository::memberCanBorrow`
accepts `active` alone, and `listBorrowableMembers` offers no one else. The other three
differ only on the Metrics tiles and the Members filter. Worse, the status is stored and
nothing ever moves it: new members start `subscribed` (so cannot borrow until someone
changes the combo), and nobody expires. The live data has been hand-corrected twice in two
days (2026-09-22, 2026-09-23) and is now all `active` / `non_active`.

The library's rule: a subscription lasts one year. A member may either register afresh
each year (new record, new membership number — how the paper list works) or keep their
record and renew it. Both must be possible.

## Rule

- A member is **active** on day `d` exactly when `active_until >= d`; otherwise **not active**.
- **Borrowing is refused if the member is not active.** Returning is unaffected.
- `active_until` is the member's last active day (ISO date, `YYYY-MM-DD`).

| Event | `active_until` becomes |
|---|---|
| New registration | registration date + 1 year − 1 day |
| Librarian sets a not-active member to **Active** (renewal) | today + 1 year − 1 day |
| Librarian sets an active member to **Not active** | yesterday |
| Save with the status unchanged | unchanged |

"Today" is `Clock`'s local date, never SQLite's `'now'`, so tests and the time zone hold.
Date arithmetic uses SQLite's `date(x, '+1 year', '-1 day')` (29 Feb + 1 year = 1 Mar,
minus a day = 28 Feb). Example: registered 2025-09-24 → active through 2026-09-23;
registered 2025-09-23 → not active on 2026-09-23.

Setting an already-active member to Active is a no-op; there is no early renewal.

## Data model (schema v7)

- `members.status` is **removed**; `members.active_until TEXT CHECK (date(active_until) IS active_until)`
  replaces it, appended last (an `ALTER TABLE ADD COLUMN` can neither place it elsewhere nor
  make it `NOT NULL` without a constant default). The repository always writes it; a NULL
  reads as not active.
- Status is never stored. Core computes it with one SQL expression,
  `CASE WHEN m.active_until >= :today THEN 'active' ELSE 'non_active' END`, supplied by a
  single helper (e.g. `MemberSql::statusExpression()` plus binding `:today`). Every reader
  uses it.
- `MemberStatus` keeps `kActive` and `kNonActive`; `kSubscribed` / `kUnsubscribed` go.
  `statusCodes()` returns the two.
- `MemberRecord` keeps `status` (now derived on read) and gains `activeUntil`.
  `MemberInput::status` stays as the librarian's choice, default `active`; the repository
  turns it into `active_until` per the table above.
- `member_status_history` stays and is written again, on create and on every status change
  from the editor, with a note such as `Renewed until 2027-09-22` /
  `Ended early` / `Registered, active until 2027-09-22`. Old rows are kept as they are.

### Migration v6 → v7

`ALTER TABLE members ADD COLUMN active_until …`, fill it, then `DROP INDEX
idx_members_status` and `ALTER TABLE members DROP COLUMN status` (SQLite ≥ 3.35). The CHECK on
`status` is the column's own, so `DROP COLUMN` accepts it; this was probed on a scratch copy
of the live DB — all 9,676 status-history rows survived with foreign keys on. The legacy
date-constraint rebuild in `Database.cpp` creates `active_until` itself and no longer creates
`status`, so a legacy database and a fresh one end with identical CHECK lines. Done in two
commits (add, then drop once nothing reads `status`), both under v7.

- `active_until = date(registered_at, '+1 year', '-1 day')` for **every** member, live and
  archived. The old `status` is not consulted.
- Effect on the live DB as of 2026-09-23: identical to today's stored status except **8
  members** stored `active` who registered 2025-09-02 … 2025-09-23; they become not active.
  Agreed: their year has ended; a librarian renews any who should stay.
- `database/schema.sql` and the fresh-schema DDL in `Database.cpp` change to match; test
  fixture `schema_v6` snapshot is added for the migration test.
- The live DB is backed up by hand (`vlms.db.bak-YYYYMMDD-HHMMSS`) before the first run
  of the new build against it.

## Readers that change

- `CirculationRepository::memberCanBorrow` — derived status; error key unchanged
  (`error.loan.memberInactive`).
- `CirculationRepository::listBorrowableMembers` — `active_until >= :today`.
- Loan rows' `memberStatus` (`CirculationRepository.cpp`, column 4) — derived.
- `MemberSql` filter (`m.status IN …`) and the `MemberSort::kStatus` sort — use the
  expression; the sort orders by `active_until` then id.
- `MetricsRepository` — Active / Not active counts via the expression;
  `membersSubscribed` / `membersUnsubscribed` fields and tiles removed.
- `ArchivePage`, `MembersPage` table + details — unchanged calls to
  `Strings::memberStatusLabel`, since `status` is still filled.

## UI

- **Member editor:** the status combo offers **Active** and **Not active** only. Beside it a
  read-only **Active until** value (the stored date; for a new member, the date it will get).
  Default for a new member: Active.
- **Members page:** status filter lists the two statuses; details panel adds **Active until**.
- **Metrics:** Subscribed and Unsubscribed tiles removed.
- **Strings:** remove `member.status.subscribed`, `member.status.unsubscribed`,
  `metrics.membersSubscribed`, `metrics.membersUnsubscribed`; add a `member.field.activeUntil`
  key in all three tables; update the help text at `Strings.cpp:1135` that lists the four
  statuses. `test_core_StringsParity` guards the three tables.

## Import script

`scripts/import_members_from_xlsx.py` writes `active_until` (registration + 1 year − 1 day)
instead of `status`, and its history rows follow.

## Out of scope

- Early renewal of a still-active member, renewal periods other than one year, a
  "subscription" entity separate from the member.
- The user-manual wiki spec (`2026-09-23-user-manual-wiki-design.md`) mentions "the four
  member statuses"; it must say two when that work is done.

## Tests

Core (`test_vlms_core`, judged under `ctest`):
- Expiry boundary with a fixed `Clock`: active on `active_until`, not active the day after.
- Create sets registration + 1 year − 1 day; status default is active.
- Not active → Active sets today + 1 year − 1 day and writes history; Active → Not active
  sets yesterday; unchanged status leaves the date alone.
- `memberCanBorrow` refuses the day after expiry; `listBorrowableMembers` drops the member.
- Members filter and sort by status use the derived value.
- Metrics counts two statuses.
- Migration v6 → v7: `status` column gone, `active_until` filled from `registered_at`, a
  stored-`active` member registered over a year ago comes out not active.
- `statusCodes()` is exactly `{active, non_active}`.

UI (`test_vlms_ui`):
- Editor combo has two entries and shows Active until; Members filter has two entries;
  Metrics has no Subscribed/Unsubscribed tile.
- Existing tests that seed `subscribed` / `unsubscribed` members are updated.
