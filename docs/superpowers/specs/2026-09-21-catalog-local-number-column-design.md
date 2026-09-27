# Catalog Local Number Column — Design

**Date:** 2026-09-21
**Status:** Approved
**Page:** Catalog (`applications/vlms/src/ui/catalog/`)

## Problem

The catalog table leads with `Title | Author | Category | ISBN | Language | Copies |
Available`. Two of those columns — ISBN and Language — are not what a librarian reaches for
at the desk, and the one number they *do* reach for, the copy's local accession number, is
not on the list at all. Today it takes opening the book editor to see it.

A book can hold several copies, each with its own local number, so the column cannot be a
plain text cell. It has to show one number and offer the rest.

## Goal

The catalog table reads `Title | Author | Category | Local Number | Copies | Available`,
where the Local Number cell is a dropdown over that book's copy numbers.

## The data this runs against

Measured against the live database on 2026-09-21:

| | rows |
|---|---|
| live books with a title | 18,468 |
| books with 0 copies | 1,749 |
| books with exactly 1 copy | 14,359 |
| books with more than 1 copy | 2,360 (largest: 18 copies) |
| live copies with a blank `local_id` | 0 |
| live copies with a non-numeric `local_id` | 0 |

Two consequences drive the design:

1. **Every live copy has a numeric local number.** The only empty cell is a book with no
   copies. No blank-number fallback is needed for copies that exist.
2. **The pager opens on ALL** (session log, 2026-09-21), so the page paints all 18,468 rows
   on load, and repaints them on every search keystroke. Anything costing a widget per row
   is out.

`local_id` is unique per *source*, not globally — 2,803 numbers repeat across the Arabic and
foreign sources. The column displays numbers; it never treats them as a global key.

## Decisions

| Question | Decision |
|---|---|
| What does picking a number do? | Nothing. Browse only — the dropdown exists so the numbers are visible without opening the editor. |
| ISBN and Language columns | Removed from the table. Both remain in the details panel, the language filter, and free-text search. |
| Closed-cell content | Lowest number, with the extra count beside it: `1042 (+2)`. One copy shows the bare number. No copies shows an em dash. |
| Sortable? | Yes, by lowest number, keeping the "every column sorts" convention from 2026-09-06. |
| How the dropdown is built | A `QStyledItemDelegate` paints the closed cell; a real `QComboBox` is created only on click and destroyed when its popup closes. |

### Why not a `QComboBox` per row

`BookCopiesTable` already uses `setCellWidget` with a combo, so it is the familiar pattern —
but that table holds a handful of copies inside a dialog. Here it would mean building 2,360
combo widgets on the default ALL page and rebuilding them on every search keystroke. That
would undo the ~2s cold load the pager change just bought. The delegate keeps the per-row
cost at one `QTableWidgetItem`, exactly what it is today.

## Architecture

### Core (Qt-free)

`BookRecord` gains one field:

```cpp
std::vector<std::string> localIds;  // ascending numerically; empty when the book has no copies
```

`listBooks` already `LEFT JOIN`s `book_copies` and groups by `b.id`, so the numbers come back
in the same query with no second round-trip. One aggregate is appended to `kBookSelect`:

```sql
COALESCE(GROUP_CONCAT(DISTINCT bc.local_id), '') AS local_ids
```

It is appended **last**, at index 19, so every existing column index in `readBookRow` is
untouched. `DISTINCT` guards against the `active_loan` join multiplying a copy row. SQLite
does not order `GROUP_CONCAT`, so `readBookRow` splits on `,` and sorts numerically in C++ —
at most 18 elements per book. `getBook` shares `kBookSelect` and gets the field too.

New sort key `BookSort::kLocalNumber = "localNumber"`, with a branch in
`BookSql::orderExpressions`:

```cpp
"CASE WHEN COUNT(bc.id) = 0 THEN 1 ELSE 0 END ASC, "
+ withDirection("MIN(CAST(bc.local_id AS INTEGER))", asc) + ", " + withDirection("b.id", asc)
```

The leading `CASE` pins the 1,749 copy-less books last in **both** directions. `MIN` is the
same number the closed cell shows, so the column reads in order top to bottom.

`BookSort::kIsbn` and `BookSort::kLanguage` are deleted along with their two
`orderExpressions` branches. Nothing outside the catalog's own column keys referenced them.

### Strings

Add `catalog.col.localNumber` to all three tables, reusing the wording already established by
`archive.col.localId` and `book.copy.localId`:

| locale | value |
|---|---|
| ar | `الرقم المحلي` |
| fr | `N° local` |
| en | `Local no.` |

Remove `catalog.col.isbn` and `catalog.col.language` from all three tables. The parity test
guards that they move together.

The `1042 (+2)` label is built inline with `QStringLiteral("%1 (+%2)")`, matching the
untranslated `%1 (%2)` convention the filter lists already use. No new key.

### UI

**`LocalNumberDelegate.{h,cpp}`**, new, in `applications/vlms/src/ui/catalog/`. One
responsibility: render a combo-looking cell and pop a real combo on click. It knows nothing
about the catalog beyond the item role it reads, so it is independently testable.

- Reads the row's numbers from `Qt::UserRole + 1` as a `QStringList`.
- `paint()` draws the closed-combo chrome by hand — a rounded rect against
  `QPalette::Button`/`Mid` and the theme's 4px radius, plus a hand-drawn arrow — rather than
  through `QStyle::CC_ComboBox`. `CC_ComboBox` is drawn against `background.widget` (the table
  view, not a real `QComboBox`), so the theme's `QComboBox { ... }` stylesheet rules never
  match it and the active style falls back to unstyled default chrome. Painting by hand against
  the same palette roles `CE_ItemViewItem` already used for the cell keeps it reading as part of
  the table in both themes, on every style; direction-aware layout (`option.direction`) still
  gives it RTL mirroring. With zero numbers it paints an em dash with no frame and no arrow.
- `editorEvent()` on `MouseButtonRelease` inside the cell, and only with two or more numbers,
  creates one `QComboBox` over the cell rect, fills it, calls `showPopup()`, and
  `deleteLater()`s it when the popup closes. Nothing is committed — browse only.

**`CatalogPage`** drops to six columns, widths `{320, 200, 130, 110, 70, 70}`:

`Title | Author | Category | Local Number | Copies | Available`

`config.tableColumnCount` goes 7 → 6, `kIsbnColumnWidth` is deleted, and the sort keys become
title / author / category / **localNumber** / copies / available. `refreshBooks` sets the new
cell with the numbers in `Qt::UserRole + 1` and installs the delegate on column 3.

## Testing

`TestSeed` gains `rawSetCopyLocalId(db, copyId, value)`, following the existing
`rawSetPublicationDate`, so a test can pin a copy's number.

New `applications/vlms/test/src/test_catalog_local_number.cpp`
(suite `test_ui_CatalogLocalNumber`), registered in the UI test `CMakeLists.txt`:

| Test | Guards |
|---|---|
| `ColumnsStartWithTitleAuthorCategoryLocalNumber` | header order, and that ISBN/Language are gone |
| `ABookWithNoCopiesShowsADash` | the 1,749-row case |
| `ASingleCopyShowsItsNumberAlone` | the 14,359-row case — no `(+n)` suffix |
| `SeveralCopiesShowTheLowestNumberAndTheCount` | `1042 (+2)`, sorted numerically not lexically |
| `SortingByLocalNumberOrdersByTheLowestNumber` | header click on column 3 |
| `BooksWithoutCopiesSortLastInBothDirections` | the `CASE` pin |

Core side, in `test_catalog_repository.cpp`: `listBooks` returns `localIds` ascending, and a
book with no copies returns an empty vector.

Existing catalog tests are updated for the new column indices.

## Out of scope

- The Archive page's own catalog table. It does not use the removed sort keys and is not
  part of this change.
- Making the local number editable from the list. The book editor remains the only place a
  number is assigned or changed.
- Any change to how local numbers are minted, released, or reused.

## Done means

Tests green, and the real application launched and screenshotted showing the catalog page
with the new column — per the standing rule that green tests alone do not close a task.
