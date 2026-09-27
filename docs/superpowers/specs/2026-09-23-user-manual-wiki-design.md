# User manual wiki — design

*2026-09-23*

## The problem

VLMS is handed to the library as an installed Windows application. The librarians
never see the source, never read this repository, and have no document that tells them
what the five screens do. Everything the application knows how to do — the local-number
drop-down, the two member statuses and the last active day, the three loan states, the difference between a
deleted record and an archived one — is currently knowledge held by whoever was in the
room when it was built.

What is wanted is a manual the library can read on its own: HTML pages, one topic per
page, wiki-shaped, in the three languages the application itself speaks, with a picture
of the actual screen beside every explanation.

## Audience

The librarians who operate the application daily. Not maintainers.

Consequences that bind the whole document:

- Prose names every control by the label printed on it, in the reader's own language.
  No class names, no table names, no file paths, no SQL.
- Arabic is the primary language, not a translation of the English. It is the language
  the library works in and the application's default.
- Every screenshot shows the interface in the same language as the page around it. An
  Arabic reader never sees an English button.

## Scope

Three complete parallel page sets — `ar` (RTL), `en`, `fr` — of fourteen pages each,
forty-two pages in total, covering the application as it behaves on `Beta` at version
0.2.0. That includes the permanent removal funnel, the one-year member status, and row
ticks with bulk actions, all merged since this spec was first drafted (see *Revision*).

## Structure

```
docs/manual/
  index.html                  language chooser: three flags, the app's own icons
  template.html               one shared shell for every page
  assets/
    manual.css                one stylesheet, light and dark, logical properties
    manual.js                 sidebar current-page marking, image lightbox
    shots/<lang>/<id>.png     screenshots, one folder per language
  content/<lang>/*.md         the source text
  <lang>/*.html               the generated pages the reader opens
```

Fourteen pages per language:

| File | Covers |
|---|---|
| `index` | What the application is; how the manual is organised; where to start |
| `getting-started` | Launching; the header — five nav buttons, three language flags, theme toggle; the footer and the licence; the shape every list page shares: filter column, search box, table, pager, details panel, button pad; sorting by a header click; ticking rows — the tick in each row's first cell, the select-all tick in the header, and that a button acts on every ticked row, or on the highlighted row when nothing is ticked |
| `catalogue` | Every control on the Catalogue screen: `Loans`, `Add Book`, `Edit`, `Delete`; the category, language and cover filters; search by title, author, ISBN or local number; the Local Number column — `N (+n)`, the drop-down of a title's numbers, green on the shelf and red italic out on loan |
| `members` | Every control on the Members screen: `Loans`, `Add Member`, `Edit`, `Delete`; the status, sex, inscription year, age group and city filters; the Loans column; the last active day in the details; the photo and ID image; Delete refused while a loan is out, with the jump to Circulation |
| `circulation` | Every control on the Circulation screen: `Checkout`, `Extend`, `Return`, `Delete`; the status filter (All, Open, Overdue, Returned); search by title, member number or a member's full name |
| `archive` | Every control on the Archive screen, all four record types: `Restore`, `Permanently remove`, `Reuse local number`; the Copies and Loans columns and why they decide whether a record can be purged |
| `metrics` | The five sections and what each tile counts |
| `task-add-book` | Adding a title, its copies, its cover, the OCR assist; the categories list reached from the book dialog's Category row |
| `task-register-member` | Registering a member; what the application derives (number, age group, last active day) and what it asks for; renewing a membership for another year, and ending one early by setting Non active |
| `task-lend-book` | Lending, from all three entry points: the Circulation screen, and the `Loans` dialog of a book or of a member — which opens even when nothing has been borrowed yet |
| `task-return-extend` | Returning and extending, from the same three entry points |
| `task-remove-book` | Deleting a book, a copy, a member or a loan; what delete means (archive, not destroy); deleting several ticked rows at once; permanent removal from the Archive, and the order it requires — loans, then copies, then the title |
| `task-reuse-number` | Reusing a local number: taking one released by an archived copy through the Archive's `Reuse local number`, and picking a free number offered behind the next number when adding a copy row |
| `reference` | Field-by-field meanings; the two member statuses and the last active day; the three loan states; live vs archived; what the local number's colours and italic mean |

Each of the five screen pages opens with a **numbered callout image** — the full window
with ①②③ drawn over its regions — followed by a numbered list explaining each one, so a
reader can point at what is in front of them.

Task pages are step-by-step: one screenshot per step, the step's action in bold, and what
the application does in response.

## Content model

`scripts/build_manual.py`, Python standard library only, no third-party dependency, no
network.

It reads `docs/manual/content/<lang>/<page>.md` — front matter followed by Markdown —
and renders each through
`docs/manual/template.html` into `docs/manual/<lang>/<page>.html`.

Front matter is three flat `key: value` lines between `---` fences — `title`, `order`,
`summary` — parsed by splitting on the first colon. No nesting, no lists, no YAML
library.

The template supplies: `<html lang dir>`, the brand header, the sidebar built from every
page's `order` and `title` in that language, prev/next links, and the language switcher
that jumps to the same page in another language.

Both the Markdown source **and** the generated HTML are committed. The reader
double-clicks `index.html` — no server, no Python, no build step. Regenerating after a
UI change is one command.

**DECISION — generator rather than forty-two hand-written files.** The cost that matters
is the second edit, not the first. One template change must not be a forty-two file edit,
and three hand-maintained language sets drift apart within one release.

**DECISION — CSS logical properties (`margin-inline-start`, `padding-inline`,
`border-inline-start`) rather than a separate RTL stylesheet.** One rule set serves all
three languages. Arabic is the primary audience and must not be the direction that breaks.

## Screenshots

Taken from the real application code on a copy of the real database, so the manual shows
the window the librarian actually sees — real widgets, real data volumes, real pagination —
rather than a synthetic near-empty database.

**DECISION (2026-09-23, revised with the user) — an offscreen capture tool, not a
synthetic pointer on the desktop.** The first draft drove the running `vlms` process
over XTest. `manual_capture` replaces that: a small Qt program that links `vlms_ui`,
builds the real `Application` and `MainWindow` on a sandbox folder, drives them with
`QTest` under `QT_QPA_PLATFORM=offscreen`, and saves `QWidget::grab()` images. The user's
desktop and pointer are never touched, a run needs no supervision, and every capture is
repeatable with one command. Images show the application window as it draws itself,
without a Linux title bar — the library runs Windows, so a KWin frame would have been
wrong anyway. The numbered callouts are drawn by the same tool from the widgets' own
geometry, so there is no coordinates file to keep in step with the layout.

Committed and replayable:

| Artefact | Purpose |
|---|---|
| `scripts/manual/prepare_sandbox.py` | Copies the live database into the sandbox, scrubs every member's personal data, verifies the scrub, and writes the sandbox marker |
| `applications/vlms/manual_capture/` | The capture tool; one C++ function per shot id, grouped by manual page |
| `scripts/manual/capture.sh` | Builds the tool, prepares the sandbox, and captures all three languages |

A capture that fails can be re-run on its own: `manual_capture --only <id>`.

### Safety rules, non-negotiable

1. The tool refuses to start unless `--sandbox <dir>` names a folder holding the
   `.vlms-manual-sandbox` marker that `prepare_sandbox.py` writes, and whose database is
   not the repository's `database/vlms.db`. It sets the project root to that folder
   before `Application` is constructed, so the live database is never opened.
2. Settings (language, theme) are redirected into the sandbox before `Application` is
   built, so switching languages for a capture never changes the user's own settings.
3. The sandbox database is a copy of the live one with every member's names, phone,
   address, email and notes overwritten. Real volumes, no personal data in any image.
   `prepare_sandbox.py` refuses to write the marker if any live full name, first name or
   last name is still present.
4. `resources/books` is copied (covers are not personal data). `resources/members` is
   left empty — it holds member photographs and identity scans.

## Opening the manual from the application

Superseded by `2026-09-23-manual-button-design.md`. The Help word after Metrics is
removed. A round "?" beside the theme toggle opens the manual in its own browser
window, and a second click brings that window forward.

Install rules still add `docs/manual/` to the Windows installer payload so the
button works on the library's machine, where there is no repository. The string
keys `nav.help`, `help.tooltip`, and `help.notFound` stay; `help.noBrowser` is
added by the newer spec.

## Revision

*2026-09-23, later the same day.* The first draft planned a "coming in the next version"
banner and placeholder screenshots for the permanent removal funnel, because its Archive
buttons did not exist yet. The funnel has since merged into `Beta`, so the banner and the
placeholders are dropped: every page, `archive` and `task-remove-book` included, is written
and captured from the running application like the rest.

Also merged after the first draft, and now named in the page table above:

- **Row ticks and bulk actions**
  (`docs/superpowers/specs/2026-09-23-row-ticks-bulk-actions-design.md`) — a tick in the
  first cell of every list table and in the book editor's Copies tab, a select-all tick in
  the header, and Delete, Restore and Permanently remove acting on every ticked row. The screen pages
  need a capture with several rows ticked; `task-remove-book` needs a step for it.
- **The one-year member status**
  (`docs/superpowers/specs/2026-09-23-member-status-expiry-design.md`) — two statuses read
  from the last active day, and renewal through the editor's status combo. Covered in
  `task-register-member` rather than a fifteenth page, so the page set stays at fourteen.
- **Free local numbers** offered behind the next number on a new copy row — the more common
  way a librarian reuses a number, so `task-reuse-number` covers it beside the Archive's
  `Reuse local number`.
- **Searching loans by a member's full name**, on Circulation and in the Archive.

## Verification

- `scripts/manual/test_build_manual.py`, run with `python3 -m unittest`: every page
  exists in every language; the three languages have the same page set; every internal
  link resolves; every `<img>` has a file on disk. It stays outside `ctest`, which builds
  and runs the C++ suites only.
- The sandbox gate above: `prepare_sandbox.py` verifies the scrub before writing the
  marker, and `manual_capture` refuses a folder without it.
- The finished manual is opened in a real browser, in all three languages, and shown
  before the work is called done.

## Out of scope

- Installation and Windows deployment — `docs/windows-installer.md` already covers it,
  and the librarians receive an installed application.
- Database administration, backup and restore.
- Anything about building, testing or modifying the source.

## Open issues

- **OPEN ISSUE** — whether the library wants a printable single-page version (PDF) as
  well. Not designed here; the page structure does not prevent it later.
