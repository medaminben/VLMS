---
title: Getting started
order: 2
summary: The window, the list pages, sorting, and ticks.
---

Open VLMS. The window comes up on the Catalogue, in the language and the light or dark theme you left it in.

VLMS opens once per computer. Opening it again while it is open shows *VLMS is already open on this computer. Use the window that is already open.* and starts nothing; go back to the window you have.

![The Catalogue, with the parts of the window numbered.](shot:gs-window)

1. `Catalogue` — the screen you are on. `Members`, `Circulation`, `Archive`, and `Metrics` sit beside it.
2. The three flags. The filled one is the language of the screen. Choosing another flag switches the whole application at once.
3. The theme button, and the ? beside it. The theme button turns the dark theme on and off. The ? opens this manual.
4. The filters for this screen.
5. Search.
6. The list.
7. The pager.
8. The details of the highlighted row.
9. The buttons for this screen.
10. The footer. The library's name is a link.

![The same window in the dark theme.](shot:gs-dark)

## The footer and the licence

The line in the footer opens the licence. VLMS is free software under the MIT Licence: the library, like anyone, may use, copy, change and pass it on, as long as the licence goes with it. The catalogue, the member records and the photographs belong to the library. There is no warranty. The licence also names the software VLMS is built with, such as Qt and Tesseract, and ends with the full MIT text.

![The licence.](shot:gs-licence)

Close it with `Close`.

## The shape of every list {#list-shape}

Catalogue, Members, Circulation, and Archive are the same shape.

- The filter column is on the side. A choice there narrows the list. `All` means no narrowing on that filter.
- Search matches as you type. Clearing the box brings the full list back.
- The table is the list. Click a row to highlight it. The details panel then shows that row.
- The pager sits under the table. It opens on `ALL`, so the whole filtered list is loaded. You can choose 20, 50, or 100 instead. `First` and `Last` jump to the ends, and the page box jumps to a page. When the filtered list has fewer than 20 rows, the pager hides.
- The details panel is the highlighted row, including its picture when it has one.
- The button pad is the work for this screen: add, edit, lend, delete, and so on. A button follows the highlighted row. It stays grey when that row cannot take the action. The context menu and the keyboard shortcuts are the same actions, in the same order; see [Menus and shortcuts](reference#menus-shortcuts).

## Sorting {#sort}

Click a column heading to sort by it. Click it again to reverse the order. The heading shows which way the list is going.

![The Title heading after a click, with the list reversed.](shot:gs-sort)

The tick at the start of the first heading is not part of the sort. Click the middle of the heading, not the tick. See [Ticks](#ticks).

## Ticks {#ticks}

A tick in the first cell of a row marks that row for a button. The tick in the heading marks every row on the page, or clears them.

![Three ticked rows, and the heading tick.](shot:gs-ticks)

1. The tick of the highlighted row.
2. The heading tick.

`Delete`, `Restore`, and `Permanently remove` act on every ticked row. With nothing ticked, they act on the highlighted row alone. Those buttons stay available while any row is ticked, even when the highlighted row itself could not take the action. Each ticked row is still checked on its own: a row that cannot be removed is skipped and the others go through.

A tick stays as long as its row is in the list. Searching, filtering, sorting, or a change made elsewhere does not clear it. A row that leaves the list loses its tick, also when it moves to another page; after a `Delete`, the rows that went are no longer ticked and the rest still are. Choosing another kind of record on the Archive clears every tick.

`Tick all` (Ctrl+A) ticks every row in the list. When every row is already ticked, it clears them instead.
