# Clickable table column sort

Approved 2026-09-06. Header captions sort the table they belong to. First
click on a column is ascending; the next click on the same column is
descending; further clicks toggle. A click on another column starts that
column at ascending.

## Scope

Every table in the app:

| Surface | Table | Backend |
|---|---|---|
| Catalog page | books | SQL via `BookQuery` |
| Members page | members | SQL via `MemberQuery` |
| Circulation page | loans | SQL via `LoanQuery` |
| Metrics page | activity, top categories | in-widget |
| Member loans dialog | loans for one member | in-widget |
| Category manager | categories | in-widget |
| Book editor | copies | in-widget |

List pages are paginated (50 rows). Sort applies to the **full filtered
result**, not only the visible page. Dialogs, Metrics, and copies already
hold every row, so they sort in the widget.

Out of scope: persisting sort across sessions; sorting filter lists;
changing default order until the user clicks a header.

## Behaviour

- Header shows Qt’s standard sort arrow on the active column.
- Text columns: case-insensitive (`COLLATE NOCASE` in SQL; `Qt::CaseInsensitive` in-widget).
- Numbers, counts, and dates: numeric or date order (`2` before `10`).
- Membership numbers: `CAST(membership_number AS INTEGER)` then the full
  string, so `123b` sits next to `123`.
- Before any click, keep today’s defaults: members by last name then first
  name; catalog by title; loans by open, then overdue, then due date, then
  id. In-widget tables keep their current load order until a header is
  clicked.
- Sort is session-only.
- Search and filters do not change. Only order changes.
- Paginated list, **no row selected**: jump to page 1 of the new order.
- Paginated list, **a row selected**: find the page that now contains that
  same record and open it; keep the row selected so focus is not lost.
- In-widget tables: restore selection by the id stored on the row.

## Architecture

Two backends, one header-click rule.

**SQL (Catalog, Members, Circulation).**  
`BookQuery`, `MemberQuery`, and `LoanQuery` gain:

- `sortColumn` — stable English key (`number`, `name`, …), never the
  translated caption
- `sortAscending` — `true` on first click of a column

Each SQL helper exposes `orderClause(query)` from a **whitelist**. Empty or
unknown `sortColumn` → today’s default `ORDER BY`. Hostile values are
ignored (same as unknown). Never concatenate header text into SQL.

**Rank (list pages only).**  
`rankOf(id, query)` counts filtered rows that sort *before* that id under
the same `orderClause`. Page is `rank / pageSize + 1`. On failure, open
page 1 and re-select the row if it is visible there.

**In-widget.**  
Dialogs, Metrics, and book copies use `QTableWidget` / `QHeaderView` sort
on the rows already loaded. Selection is restored by `Qt::UserRole` id
(or the existing id role on copies). Header sort does not write a new
copy order back to the database.

**Shared UI helper.**  
One helper wires `QHeaderView::sectionClicked`: first click on a column →
ascending; same column again → toggle; other column → ascending; update
the sort indicator. List pages then refresh through the query. Other
tables sort in-widget.

## Column keys

Stable keys, independent of locale. Captions stay on `Strings::t`.

**Members:** `number`, `name`, `phone`, `city`, `status`, `loans`  
**Catalog:** `title`, `author`, `category`, `isbn`, `language`, `copies`, `available`  
**Circulation:** `member`, `number`, `title`, `borrowed`, `due`, `status`  
**Metrics activity:** `period`, `checkouts`, `returns`, `newMembers`  
**Metrics categories:** `category`, `books`  
**Member loans dialog:** `title`, `copy`, `borrowed`, `due`, `returned`, `status`  
**Category manager:** `code`, `label`, `books`  
**Book copies:** `localId`, `globalId`, `source`, `centralId`, `classification`,
`subject`, `indexCode`, `location`, `inventoryStatus`, `compensation`,
`notes`, `status`

`name` on members orders `last_name` then `first_name`. Circulation
`status` uses the same open / overdue / returned cases already used in
the default loan order.

## Error handling

- Unknown `sortColumn` → default order. No error dialog.
- `list*` failure → existing repo-error path. Sort state is left as the
  user set it so a retry uses the same order.
- `rankOf` failure → page 1; re-select if the id is on that page.

## Tests

**Core**

- `listMembers` / `listBooks` / `listLoans`: one text column and one
  numeric (or date) column, both directions.
- `rankOf` returns the index that maps to the page containing the selected
  id after a sort change.
- Injection: `sortColumn` of `1; DROP TABLE` (or similar) yields the
  default order and does not break the statement.

**UI**

- Header click reorders the visible rows.
- No selection → pager is on page 1.
- Selection kept; pager follows that row to its new page.

## Non-goals

- Remembering sort after restart.
- Multi-column user sort (only the implicit tie-break in `orderClause`).
- Reordering book copies in the database from a header click.
