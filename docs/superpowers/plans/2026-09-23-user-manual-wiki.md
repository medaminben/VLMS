# User Manual Wiki Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a complete, three-language (Arabic, English, French) HTML wiki manual for
VLMS, with real screenshots and step-by-step task pages, openable from a new Help
button in the application header.

**Architecture:** Markdown sources in `docs/manual/content/<lang>/` are rendered by a
standard-library Python generator into committed static HTML under `docs/manual/<lang>/`.
Screenshots come from `manual_capture`, a Qt program that links the application's own UI
library, runs the real `MainWindow` offscreen on a scrubbed sandbox copy of the database,
and saves widget grabs with numbered callouts drawn from widget geometry. The app gains a
`nav.help` header button that opens the generated manual in the system browser.

**Tech Stack:** Python 3.13 standard library only (`sqlite3`, `html`, `re`, `unittest`,
`pathlib`); C++20 / Qt 6 Widgets + QtTest; GoogleTest for the in-app change; plain HTML,
CSS (logical properties) and a few lines of vanilla JS.

Spec: `docs/superpowers/specs/2026-09-23-user-manual-wiki-design.md` (read it; the
Screenshots section was revised on 2026-09-23 for the offscreen tool).

## Progress (2026-09-23, uncommitted)

- Task 1: scrubber and its 9 unit tests are in. The live-database run was not done.
- Task 2: generator, template, stylesheet and fixture tests are in. A browser check of the fixture pages showed the Arabic sidebar on the right and the English sidebar on the left.
- Task 3: `manual_capture` builds. It refuses a desktop session and a folder with no sandbox marker (exit 4). The general shots were not captured.
- Tasks 4–10: not started. They need a scrubbed copy of the live database.
- Task 11: Help button, the three string keys, the Windows install rule, and the location tests are in. The running window was not driven.

## Global Constraints

- Audience: librarians. Prose names controls by the label printed on them, in the reader's language. No class names, table names, file paths or SQL in manual prose.
- Arabic is the primary language, written natively, not translated from English. `ar` pages are `dir="rtl"`.
- Every screenshot on a page is in that page's language (`assets/shots/<lang>/<id>.png`).
- Fourteen pages per language, same file names in all three: `index`, `getting-started`, `catalogue`, `members`, `circulation`, `archive`, `metrics`, `task-add-book`, `task-register-member`, `task-lend-book`, `task-return-extend`, `task-remove-book`, `task-reuse-number`, `reference`.
- Generator: Python standard library only, no third-party dependency, no network.
- Front matter: exactly the flat keys `title`, `order`, `summary`, split on the first colon.
- Both Markdown sources and generated HTML are committed; the reader double-clicks `index.html` — no server.
- CSS uses logical properties (`margin-inline-start`, `padding-inline`, `border-inline-start`, `inset-inline-start`); no separate RTL stylesheet. Light and dark via `prefers-color-scheme`.
- No external URL in any generated page (no CDN, no web font service). The Cairo font is copied from `applications/vlms/resources/fonts/Cairo-Variable.ttf`.
- British English in all English prose and comments: catalogue, colour, organise, behaviour. Never rename identifiers (`nav.catalog`, `CatalogPage` stay).
- **Never** commit, upload or paste member data. The live database `database/vlms.db` is only ever opened read-only by `prepare_sandbox.py`. Screenshots come only from the scrubbed sandbox. `resources/members/**` is never copied.
- The capture tool never touches the desktop: it runs with `QT_QPA_PLATFORM=offscreen` only.
- Implementers do not commit. The controller commits each task after its reviewer approves, in task order.
- Each C++ task uses its own build directory (`build-sdd-manual-<task>`); `/build-*/` is already gitignored.
- Judge C++ tests with `ctest` from the build dir (`QT_QPA_PLATFORM=offscreen ctest --output-on-failure`), never by running a test binary directly.

## Shot contract

Every screenshot has an id. The capture tasks (4, 5) must produce exactly these ids, in
all three languages; the content tasks (6–9) reference them as `![caption](shot:<id>)`.
"Callouts" means numbered circles drawn by the tool over the listed widgets, in order.

| id | Page(s) | What the image shows |
|---|---|---|
| `gs-window` | getting-started | Full window on Catalogue. Callouts: ① nav buttons ② language flags ③ theme toggle ④ filter column ⑤ search box ⑥ table ⑦ pager ⑧ details panel ⑨ button pad ⑩ footer |
| `gs-dark` | getting-started | Full window on Catalogue in dark theme |
| `gs-ticks` | getting-started, task-remove-book | Catalogue table cropped, rows 1–3 ticked, header tick partially checked. Callouts: ① a row tick ② header tick |
| `gs-sort` | getting-started | Catalogue table cropped after clicking the Title header once (sort indicator visible) |
| `gs-licence` | getting-started | The licence dialog opened from the footer |
| `cat-overview` | catalogue | Full window on Catalogue with the first row selected. Callouts: ① category filter ② language filter ③ cover filter ④ search ⑤ Local Number column ⑥ Copies/Available columns ⑦ details panel ⑧ `Loans` ⑨ `Add Book` ⑩ `Edit` ⑪ `Delete` |
| `cat-search-number` | catalogue | Catalogue after searching a local number that belongs to a title with several copies; the matched number leads the cell as `N (+n)` |
| `cat-number-dropdown` | catalogue | The Local Number cell's drop-down open on a multi-copy title (popup composited onto the window grab) |
| `cat-copy-colours` | catalogue, reference | Table crop showing at least one green (on shelf) and one red italic (on loan) local number |
| `cat-loans-dialog` | catalogue, task-lend-book | The book `Loans` dialog of a title that has loan history |
| `mem-overview` | members | Full window on Members, first row selected. Callouts: ① status filter ② sex filter ③ inscription-year filter ④ age-group filter ⑤ city filter ⑥ search ⑦ Loans column ⑧ details panel ⑨ `Loans` ⑩ `Add Member` ⑪ `Edit` ⑫ `Delete` |
| `mem-details` | members, reference | Details panel crop of a member showing status and last active day |
| `mem-delete-blocked` | members, task-remove-book | The message shown when deleting a member who still has a loan out, with its jump-to-Circulation button |
| `mem-loans-dialog` | members, task-lend-book | The member `Loans` dialog of a member with loan history |
| `circ-overview` | circulation | Full window on Circulation. Callouts: ① status filter ② search ③ table ④ details panel ⑤ `Checkout` ⑥ `Extend` ⑦ `Return` ⑧ `Delete` |
| `circ-overdue` | circulation, reference | Circulation with the Overdue filter picked |
| `circ-search-name` | circulation | Circulation after searching a (scrubbed) member's full name |
| `arc-books` | archive | Archive, Books type. Callouts: ① type list ② search ③ Copies column ④ `Restore` ⑤ `Permanently remove` |
| `arc-copies` | archive, task-reuse-number | Archive, Copies type. Callouts: ① Loans column ② `Reuse local number` |
| `arc-loans` | archive | Archive, Loans type |
| `arc-members` | archive | Archive, Members type. Callouts: ① Loans column |
| `arc-restore-confirm` | archive | The Restore confirmation box |
| `arc-purge-confirm` | archive, task-remove-book | The Permanently remove confirmation box for a loan |
| `met-overview` | metrics | The Metrics page, top of the dashboard. Callouts: one per section heading visible, then `Refresh` |
| `met-full` | metrics | The whole dashboard viewer grabbed at its full content height (all five sections) |
| `add-book-open` | task-add-book | The `Add Book` dialog as it opens (Book tab) |
| `add-book-filled` | task-add-book | Same dialog with title, author, language and category filled. Callouts: ① Category row's categories button ② From image button |
| `add-book-copies` | task-add-book | The Copies tab with one new copy row |
| `add-book-free-number` | task-add-book, task-reuse-number | New copy row's local-number editor open, showing the next number then free numbers |
| `add-book-categories` | task-add-book | The categories list dialog opened from the Category row |
| `reg-open` | task-register-member | The `Add Member` dialog as it opens |
| `reg-filled` | task-register-member | Same dialog filled (fake name, date of birth, sex, city). Callouts: ① number (read-only) ② status ③ last active day |
| `reg-renew` | task-register-member | `Edit` on a Non active member with the status combo open |
| `reg-renewed` | task-register-member | Same dialog after picking Active: last active day one year ahead |
| `lend-dialog` | task-lend-book | `Checkout` dialog from Circulation, empty |
| `lend-filled` | task-lend-book | Same dialog with a member and a copy picked |
| `lend-from-book` | task-lend-book | `Checkout` from a book's `Loans` dialog (copy list fixed to that title, no copy search) |
| `lend-from-member` | task-lend-book | `Checkout` from a member's `Loans` dialog (member shown as a label) |
| `lend-empty-history` | task-lend-book | A book `Loans` dialog for a never-borrowed title (empty message, Checkout enabled) |
| `ret-dialog` | task-return-extend | `Return` dialog for an open loan |
| `ext-dialog` | task-return-extend | `Extend` dialog for an open loan |
| `ret-from-history` | task-return-extend | A `Loans` dialog with an open loan selected and `Extend`/`Return` enabled |
| `rm-book-confirm` | task-remove-book | Catalogue `Delete` confirmation for one book |
| `rm-book-refused` | task-remove-book | The refusal for a book with a copy out on loan |
| `rm-copy-row` | task-remove-book | Book editor Copies tab with a copy row selected and its remove button visible. Callouts: ① remove button |
| `rm-loan-confirm` | task-remove-book | Circulation `Delete` confirmation for one returned loan |
| `rm-bulk-confirm` | task-remove-book | Catalogue `Delete` confirmation with three rows ticked (the several-rows wording) |
| `reuse-chooser` | task-reuse-number | The book chooser opened by `Reuse local number` |
| `reuse-editor` | task-reuse-number | The book editor opened by the reuse flow, number locked in |

Language-independent images the tool also writes (once, not per language):
`assets/flags/ar.png`, `assets/flags/en.png`, `assets/flags/fr.png` (64 px, from
`languageFlagIcon`).

---

## File structure

```
scripts/manual/
  prepare_sandbox.py          copy + scrub + verify + marker (Task 1)
  test_prepare_sandbox.py     (Task 1)
  capture.sh                  build tool, prepare sandbox, capture 3 languages (Task 3)
  test_build_manual.py        generator + whole-site tests (Task 2)
scripts/build_manual.py       the generator (Task 2)
docs/manual/
  index.html                  language chooser (generated, Task 2)
  template.html               page shell (Task 2)
  assets/manual.css           (Task 2)
  assets/manual.js            (Task 2)
  assets/fonts/Cairo-Variable.ttf   copied (Task 2)
  assets/brand/icon-64.png    copied (Task 2)
  assets/flags/*.png          written by the capture tool (Task 3)
  assets/shots/<lang>/*.png   written by the capture tool (Tasks 4, 5)
  content/<lang>/*.md         sources (Tasks 6–9)
  <lang>/*.html               generated (Task 10)
applications/vlms/manual_capture/
  CMakeLists.txt              target manual_capture, option-gated (Task 3)
  main.cpp                    args, safety guards, sandbox settings, run loop (Task 3)
  Capture.h / Capture.cpp     harness helpers: window, language, theme, grab, callouts, modals (Task 3)
  Shots.h                     ShotFn, registry declarations (Task 3)
  shots_general.cpp           gs-* shots + flags (Task 3)
  shots_screens.cpp           cat-*, mem-*, circ-*, arc-*, met-* (Task 4)
  shots_tasks.cpp             add-book-*, reg-*, lend-*, ret-*, ext-*, rm-*, reuse-* (Task 5)
applications/vlms/src/ui/ManualLocation.h/.cpp   (Task 11)
applications/vlms/test/src/test_manual_location.cpp (Task 11)
```

Dependency order: Tasks 1, 2, 3, 11 are independent and may run in parallel. Tasks 4 and 5
need 1 and 3 and may run in parallel with each other. Tasks 6–9 need 2, 4 and 5 and may run
in parallel with each other. Task 10 needs everything.

---

### Task 1: Sandbox preparer

**Files:**
- Create: `scripts/manual/prepare_sandbox.py`
- Test: `scripts/manual/test_prepare_sandbox.py`

**Interfaces:**
- Produces: CLI `python3 scripts/manual/prepare_sandbox.py --source database/vlms.db --books resources/books --out build-manual-sandbox`. Result: `<out>/database/vlms.db` (scrubbed), `<out>/resources/books/` (copied), `<out>/resources/members/` (empty), `<out>/config/` (empty), `<out>/.vlms-manual-sandbox` (marker, text `vlms-manual-sandbox v1`). Exit 0 on success, 2 when the verification finds a live name, 1 on usage errors.
- Produces: module functions `scrub(conn)`, `live_names(conn) -> set[str]`, `remaining_names(conn, names) -> list[str]`, `FAKE_FIRST`, `FAKE_LAST`.

- [ ] **Step 1: Write the failing tests**

```python
# scripts/manual/test_prepare_sandbox.py
import sqlite3
import tempfile
import unittest
from pathlib import Path

import prepare_sandbox as ps

SCHEMA = """
CREATE TABLE employees (id INTEGER PRIMARY KEY, username TEXT UNIQUE, password_hash TEXT,
  first_name TEXT, last_name TEXT);
CREATE TABLE members (id INTEGER PRIMARY KEY, membership_number TEXT, first_name TEXT,
  last_name TEXT, full_name TEXT, sex TEXT, date_of_birth TEXT, phone TEXT, address TEXT,
  email TEXT, notes TEXT, photo_path TEXT, id_image_path TEXT, city TEXT);
"""


def make_db(path):
    conn = sqlite3.connect(path)
    conn.executescript(SCHEMA)
    conn.execute("INSERT INTO employees VALUES (1,'amin','x','أمين','بن حسين')")
    conn.execute(
        "INSERT INTO members VALUES (1,'12','رنيم','بنت سفيان','رنيم بنت سفيان',"
        "'female','2001-05-17','22 333 444','نهج 5','a@b.tn','ملاحظة','p.jpg','i.jpg','قصور الساف')")
    conn.execute(
        "INSERT INTO members VALUES (2,'13','Karim','Ben Ali',NULL,"
        "'male','1990-02-03',NULL,NULL,NULL,NULL,NULL,NULL,'المهدية')")
    conn.commit()
    return conn


class ScrubTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.conn = make_db(Path(self.tmp.name) / "t.db")

    def tearDown(self):
        self.conn.close()
        self.tmp.cleanup()

    def test_scrub_removes_every_live_name(self):
        names = ps.live_names(self.conn)
        self.assertIn("رنيم بنت سفيان", names)
        ps.scrub(self.conn)
        self.assertEqual(ps.remaining_names(self.conn, names), [])

    def test_scrub_clears_contact_details_and_images(self):
        ps.scrub(self.conn)
        row = self.conn.execute(
            "SELECT phone, address, email, notes, photo_path, id_image_path FROM members WHERE id=1"
        ).fetchone()
        self.assertEqual(row[0], "00 000 000")
        self.assertEqual(row[1:], (None, None, None, None, None))

    def test_scrub_keeps_birth_year_only(self):
        ps.scrub(self.conn)
        dob = self.conn.execute("SELECT date_of_birth FROM members WHERE id=1").fetchone()[0]
        self.assertEqual(dob, "2001-01-01")

    def test_scrub_keeps_city_sex_and_number(self):
        ps.scrub(self.conn)
        row = self.conn.execute(
            "SELECT membership_number, sex, city FROM members WHERE id=1").fetchone()
        self.assertEqual(row, ("12", "female", "قصور الساف"))

    def test_fake_full_name_is_first_plus_last(self):
        ps.scrub(self.conn)
        first, last, full = self.conn.execute(
            "SELECT first_name, last_name, full_name FROM members WHERE id=2").fetchone()
        self.assertEqual(full, f"{first} {last}")
        self.assertIn(first, ps.FAKE_FIRST)

    def test_scrub_renames_employees(self):
        ps.scrub(self.conn)
        row = self.conn.execute(
            "SELECT username, first_name, last_name FROM employees").fetchone()
        self.assertEqual(row, ("librarian", "أمين", "المكتبة"))

    def test_remaining_names_reports_a_leak(self):
        names = ps.live_names(self.conn)
        self.assertIn("رنيم", ps.remaining_names(self.conn, names))


class CliTest(unittest.TestCase):
    def test_prepare_writes_marker_and_empty_members(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            make_db(tmp / "live.db").close()
            (tmp / "books" / "1").mkdir(parents=True)
            (tmp / "books" / "1" / "cover.jpg").write_bytes(b"x")
            out = tmp / "sandbox"
            code = ps.main(["--source", str(tmp / "live.db"), "--books", str(tmp / "books"),
                            "--out", str(out)])
            self.assertEqual(code, 0)
            self.assertEqual((out / ".vlms-manual-sandbox").read_text().strip(),
                             "vlms-manual-sandbox v1")
            self.assertTrue((out / "resources" / "books" / "1" / "cover.jpg").exists())
            self.assertEqual(list((out / "resources" / "members").iterdir()), [])
            self.assertTrue((out / "config").is_dir())
            live = sqlite3.connect(tmp / "live.db")
            self.assertEqual(live.execute("SELECT first_name FROM members WHERE id=1").fetchone()[0],
                             "رنيم")  # the source is never modified

    def test_prepare_refuses_to_write_into_the_source_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "database").mkdir()
            make_db(tmp / "database" / "vlms.db").close()
            code = ps.main(["--source", str(tmp / "database" / "vlms.db"),
                            "--books", str(tmp), "--out", str(tmp)])
            self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd scripts/manual && python3 -m unittest test_prepare_sandbox -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'prepare_sandbox'`

- [ ] **Step 3: Implement**

```python
#!/usr/bin/env python3
"""Build the screenshot sandbox for the user manual.

Copies the live database (opened read-only) into <out>/database/, overwrites every
member's personal data, verifies no live name survives, copies book covers, leaves
member photographs behind, and writes the marker manual_capture requires.
"""
from __future__ import annotations

import argparse
import shutil
import sqlite3
import sys
from pathlib import Path

MARKER = ".vlms-manual-sandbox"
MARKER_TEXT = "vlms-manual-sandbox v1\n"

FAKE_FIRST = [
    "أحمد", "سلمى", "يوسف", "مريم", "علي", "آمنة", "حمزة", "نور", "سامي", "ليلى",
    "كريم", "هالة", "بلال", "رحمة", "زياد", "إيمان", "طارق", "سارة", "وليد", "هند",
]
FAKE_LAST = [
    "بن علي الورداني", "بنت محمد الساحلي", "بن صالح القصوري", "بنت الهادي المنستيري",
    "بن عمر الجربي", "بنت يوسف البحري", "بن حسن الزيتوني", "بنت سالم الشابي",
    "بن فرج النابلي", "بنت الطاهر القيرواني", "بن منير الصفاقسي", "بنت خليل التوزري",
]


def live_names(conn: sqlite3.Connection) -> set[str]:
    names: set[str] = set()
    for first, last, full in conn.execute("SELECT first_name, last_name, full_name FROM members"):
        for value in (first, last, full):
            if value and len(value.strip()) >= 3:
                names.add(value.strip())
    return names


def scrub(conn: sqlite3.Connection) -> None:
    ids = [row[0] for row in conn.execute("SELECT id FROM members ORDER BY id")]
    for member_id in ids:
        first = FAKE_FIRST[member_id % len(FAKE_FIRST)]
        last = FAKE_LAST[(member_id // len(FAKE_FIRST)) % len(FAKE_LAST)]
        conn.execute(
            "UPDATE members SET first_name=?, last_name=?, full_name=? WHERE id=?",
            (first, last, f"{first} {last}", member_id),
        )
    conn.execute(
        "UPDATE members SET phone = CASE WHEN phone IS NULL THEN NULL ELSE '00 000 000' END,"
        " address=NULL, email=NULL, notes=NULL, photo_path=NULL, id_image_path=NULL,"
        " date_of_birth = CASE WHEN date_of_birth IS NULL THEN NULL"
        "                      ELSE substr(date_of_birth, 1, 4) || '-01-01' END"
    )
    conn.execute(
        "UPDATE employees SET username='librarian', password_hash='-',"
        " first_name='أمين', last_name='المكتبة'"
    )
    conn.commit()


def remaining_names(conn: sqlite3.Connection, names: set[str]) -> list[str]:
    fake = set(FAKE_FIRST) | set(FAKE_LAST)
    present = live_names(conn)
    return sorted(n for n in names if n in present and n not in fake)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--books", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args(argv)

    source = args.source.resolve()
    out = args.out.resolve()
    target_db = out / "database" / "vlms.db"
    if target_db == source or source.is_relative_to(out):
        print("refusing: the sandbox would overwrite or contain the source database",
              file=sys.stderr)
        return 1

    if out.exists():
        shutil.rmtree(out)
    (out / "database").mkdir(parents=True)
    (out / "resources" / "members").mkdir(parents=True)
    (out / "config").mkdir()

    live = sqlite3.connect(f"file:{source}?mode=ro", uri=True)
    names = live_names(live)
    copy = sqlite3.connect(target_db)
    live.backup(copy)
    live.close()

    scrub(copy)
    leaked = remaining_names(copy, names)
    copy.close()
    if leaked:
        print(f"refusing: {len(leaked)} live names survived the scrub", file=sys.stderr)
        return 2

    shutil.copytree(args.books, out / "resources" / "books")
    (out / MARKER).write_text(MARKER_TEXT, encoding="utf-8")
    print(f"sandbox ready: {out} ({len(names)} names scrubbed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Notes for the implementer: `copytree` of `resources/books` excludes nothing, but a book
folder can hold a `.gitkeep` — fine. The empty sandbox `config/` is where `manual_capture`
points `QSettings`.

- [ ] **Step 4: Run tests**

Run: `cd scripts/manual && python3 -m unittest test_prepare_sandbox -v`
Expected: 9 tests, OK.

- [ ] **Step 5: Run against the live data once**

Run: `python3 scripts/manual/prepare_sandbox.py --source database/vlms.db --books resources/books --out build-manual-sandbox`
Expected: `sandbox ready: …/build-manual-sandbox (N names scrubbed)` and exit 0. Then
`git status --short` shows nothing new (the folder is ignored by `/build-*/`), and
`sha256sum database/vlms.db` is identical before and after.

- [ ] **Step 6: Hand to the controller for review and commit**

Commit message: `Prepare a scrubbed sandbox for the manual's screenshots.`

---

### Task 2: Site generator, template and styling

**Files:**
- Create: `scripts/build_manual.py`
- Create: `scripts/manual/test_build_manual.py`
- Create: `docs/manual/template.html`
- Create: `docs/manual/assets/manual.css`
- Create: `docs/manual/assets/manual.js`
- Create (copy): `docs/manual/assets/fonts/Cairo-Variable.ttf` from `applications/vlms/resources/fonts/Cairo-Variable.ttf`
- Create (copy): `docs/manual/assets/brand/icon-64.png` from `applications/vlms/resources/images/brand/icon-64.png`
- Create (fixtures, test-only): `scripts/manual/fixtures/content/{ar,en,fr}/index.md`, `.../getting-started.md`

**Interfaces:**
- Produces: `python3 scripts/build_manual.py [--root docs/manual]` renders every `content/<lang>/*.md` into `<lang>/<page>.html` and writes `index.html` (chooser). Exit 0.
- Produces: `python3 scripts/build_manual.py --check` renders to memory and runs the site checks, printing each problem and exiting 1 if any.
- Produces: module API `parse_front_matter(text) -> (dict, body)`, `render_markdown(body, lang) -> str`, `build(root: Path) -> dict[Path, str]`, `check_site(root: Path, pages: dict[Path,str]) -> list[str]`, `load_ui_labels(strings_cpp: Path) -> dict[str, set[str]]`, constants `LANGS = ("ar", "en", "fr")`, `PAGES` (the fourteen names).
- **Markdown subset (the content contract; Tasks 6–9 write only this):**
  - `## Heading` / `### Heading`, optional explicit anchor suffix ` {#anchor}` (ASCII). Without one, headings get `id="h-<n>"`.
  - Paragraphs separated by blank lines. Inline: `**bold**`, `*italic*`, `` `Label` `` = a UI label (rendered `<span class="ui">`; must match a string in that language's table — see check below), `[text](page)` or `[text](page#anchor)` → `page.html`/`page.html#anchor`.
  - `- item` bullets; `1. item` numbered list (one line per item; continuation lines indented two spaces join the item).
  - `![caption](shot:<id>)` on its own line → `<figure class="shot"><a href="../assets/shots/<lang>/<id>.png"><img src="…" alt="caption" loading="lazy"></a><figcaption>caption</figcaption></figure>`.
  - Pipe tables: header row, `|---|` separator row, body rows.
  - Fenced blocks `::: step` … `:::` — consecutive step blocks form one `<ol class="steps">`; each block's inner lines are rendered with the same rules (first paragraph is the action, write it in bold).
  - Fenced blocks `::: note` … `:::` → `<aside class="note">`; `::: warning` … `:::` → `<aside class="warning">`.
- **Site checks** (`check_site`): all 14 pages exist in each of the 3 languages; each page's front matter has `title`, `order`, `summary`; `order` values are unique per language; every `href` to a local page/anchor resolves (anchors must exist in the target); every `<img src>` exists on disk; every `span.ui` text, after stripping a trailing `:` or `…` from both sides, equals a value (likewise stripped) from that language's table in `libraries/Core/src/Strings.cpp`; no page contains `http://` or `https://` except `licence`-free text — i.e. none at all.

- [ ] **Step 1: Write failing tests**

```python
# scripts/manual/test_build_manual.py
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(REPO / "scripts"))
import build_manual as bm  # noqa: E402


class FrontMatterTest(unittest.TestCase):
    def test_three_flat_keys(self):
        meta, body = bm.parse_front_matter("---\ntitle: A: b\norder: 3\nsummary: s\n---\n\nhi\n")
        self.assertEqual(meta, {"title": "A: b", "order": "3", "summary": "s"})
        self.assertEqual(body.strip(), "hi")


class MarkdownTest(unittest.TestCase):
    def r(self, text, lang="en"):
        return bm.render_markdown(text, lang)

    def test_heading_with_anchor(self):
        self.assertIn('<h2 id="loan-states">Loan states</h2>', self.r("## Loan states {#loan-states}"))

    def test_heading_without_anchor_is_numbered(self):
        self.assertIn('<h2 id="h-1">One</h2>', self.r("## One"))

    def test_inline(self):
        html = self.r("Click `Add Book`, **now**, *gently*, see [x](members#filters).")
        self.assertIn('<span class="ui">Add Book</span>', html)
        self.assertIn("<strong>now</strong>", html)
        self.assertIn("<em>gently</em>", html)
        self.assertIn('<a href="members.html#filters">x</a>', html)

    def test_escapes_html(self):
        self.assertIn("a &lt; b", self.r("a < b"))

    def test_shot(self):
        html = self.r("![The window](shot:gs-window)", "ar")
        self.assertIn('src="../assets/shots/ar/gs-window.png"', html)
        self.assertIn("<figcaption>The window</figcaption>", html)

    def test_lists(self):
        html = self.r("- a\n- b\n\n1. one\n2. two\n   more")
        self.assertIn("<ul><li>a</li><li>b</li></ul>", html)
        self.assertIn("<ol><li>one</li><li>two more</li></ol>", html)

    def test_steps_group_into_one_list(self):
        html = self.r("::: step\n**Do A.** Then.\n:::\n\n::: step\n**Do B.**\n:::")
        self.assertEqual(html.count('<ol class="steps">'), 1)
        self.assertEqual(html.count("<li>"), 2)

    def test_note_and_warning(self):
        self.assertIn('<aside class="note"><p>n</p></aside>', self.r("::: note\nn\n:::"))
        self.assertIn('<aside class="warning"><p>w</p></aside>', self.r("::: warning\nw\n:::"))

    def test_table(self):
        html = self.r("| a | b |\n|---|---|\n| 1 | 2 |")
        self.assertIn("<table><thead><tr><th>a</th><th>b</th></tr></thead>", html)
        self.assertIn("<tbody><tr><td>1</td><td>2</td></tr></tbody></table>", html)


class LabelsTest(unittest.TestCase):
    def test_reads_the_three_tables(self):
        labels = bm.load_ui_labels(REPO / "libraries/Core/src/Strings.cpp")
        self.assertIn("Add Book", labels["en"])
        self.assertIn("إضافة كتاب", labels["ar"])
        self.assertIn("Ajouter un ouvrage", labels["fr"])
        self.assertNotIn("Add Book", labels["fr"])


class BuildTest(unittest.TestCase):
    def test_fixture_site_builds_with_rtl_and_sidebar(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bm.copy_skeleton(REPO / "docs/manual", root)
            bm.copy_tree(HERE / "fixtures/content", root / "content")
            pages = bm.build(root)
            ar = pages[root / "ar/index.html"]
            self.assertIn('<html lang="ar" dir="rtl">', ar)
            self.assertIn('href="getting-started.html"', ar)
            self.assertIn('href="../en/index.html"', ar)  # language switcher, same page
            self.assertIn('<html lang="en" dir="ltr">', pages[root / "en/index.html"])
            self.assertIn(root / "index.html", pages)

    def test_check_reports_missing_pages_and_images(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bm.copy_skeleton(REPO / "docs/manual", root)
            bm.copy_tree(HERE / "fixtures/content", root / "content")
            problems = bm.check_site(root, bm.build(root))
            self.assertTrue(any("missing page" in p and "catalogue" in p for p in problems))
            self.assertTrue(any("missing image" in p for p in problems))


class RealSiteTest(unittest.TestCase):
    """The committed manual. Skipped until the content exists (Tasks 6-9)."""

    def test_committed_manual_is_complete(self):
        root = REPO / "docs/manual"
        if not (root / "content/ar/reference.md").exists():
            self.skipTest("manual content not written yet")
        problems = bm.check_site(root, bm.build(root))
        self.assertEqual(problems, [], "\n".join(problems))

    def test_committed_html_is_up_to_date(self):
        root = REPO / "docs/manual"
        if not (root / "ar/reference.html").exists():
            self.skipTest("manual not generated yet")
        for path, html in bm.build(root).items():
            self.assertEqual(path.read_text(encoding="utf-8"), html, f"stale: {path}")


if __name__ == "__main__":
    unittest.main()
```

Fixtures: `scripts/manual/fixtures/content/<lang>/index.md` with front matter
`title: Home`, `order: 1`, `summary: s`, body `## Start\n\nSee [getting started](getting-started).\n\n![x](shot:missing-shot)`;
and `getting-started.md` with `order: 2` and a one-line body. Use the language's own title
(`الرئيسية` / `Home` / `Accueil`).

- [ ] **Step 2: Run to verify failure**

Run: `python3 -m unittest discover -s scripts/manual -p 'test_build_manual.py' -v`
Expected: FAIL, `ModuleNotFoundError: No module named 'build_manual'`.

- [ ] **Step 3: Implement `scripts/build_manual.py`**

```python
#!/usr/bin/env python3
"""Render the user manual: docs/manual/content/<lang>/*.md -> docs/manual/<lang>/*.html.

Standard library only. See docs/superpowers/specs/2026-09-23-user-manual-wiki-design.md.
"""
from __future__ import annotations

import argparse
import html
import re
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LANGS = ("ar", "en", "fr")
RTL = {"ar"}
PAGES = (
    "index", "getting-started", "catalogue", "members", "circulation", "archive", "metrics",
    "task-add-book", "task-register-member", "task-lend-book", "task-return-extend",
    "task-remove-book", "task-reuse-number", "reference",
)
LANG_NAMES = {"ar": "العربية", "en": "English", "fr": "Français"}
UI = {
    "ar": {"contents": "المحتويات", "prev": "السابق", "next": "التالي", "manual": "دليل الاستخدام",
           "language": "اللغة"},
    "en": {"contents": "Contents", "prev": "Previous", "next": "Next", "manual": "User manual",
           "language": "Language"},
    "fr": {"contents": "Sommaire", "prev": "Précédent", "next": "Suivant",
           "manual": "Manuel d'utilisation", "language": "Langue"},
}


# ---------------------------------------------------------------- front matter

def parse_front_matter(text: str) -> tuple[dict[str, str], str]:
    lines = text.splitlines()
    if not lines or lines[0].strip() != "---":
        return {}, text
    meta: dict[str, str] = {}
    for index, line in enumerate(lines[1:], start=1):
        if line.strip() == "---":
            return meta, "\n".join(lines[index + 1:])
        key, _, value = line.partition(":")
        meta[key.strip()] = value.strip()
    return meta, ""


# ---------------------------------------------------------------- inline

_INLINE = re.compile(
    r"`(?P<ui>[^`]+)`"
    r"|\*\*(?P<b>.+?)\*\*"
    r"|\*(?P<i>[^*]+)\*"
    r"|\[(?P<lt>[^\]]+)\]\((?P<lh>[^)\s]+)\)"
)


def _href(target: str) -> str:
    page, _, anchor = target.partition("#")
    out = f"{page}.html" if page else ""
    return out + (f"#{anchor}" if anchor else "")


def inline(text: str) -> str:
    out: list[str] = []
    pos = 0
    for m in _INLINE.finditer(text):
        out.append(html.escape(text[pos:m.start()], quote=False))
        if m.group("ui") is not None:
            out.append(f'<span class="ui">{html.escape(m.group("ui"), quote=False)}</span>')
        elif m.group("b") is not None:
            out.append(f"<strong>{inline(m.group('b'))}</strong>")
        elif m.group("i") is not None:
            out.append(f"<em>{inline(m.group('i'))}</em>")
        else:
            out.append(f'<a href="{html.escape(_href(m.group("lh")))}">{inline(m.group("lt"))}</a>')
        pos = m.end()
    out.append(html.escape(text[pos:], quote=False))
    return "".join(out)


# ---------------------------------------------------------------- blocks

_HEADING = re.compile(r"^(#{2,3})\s+(.*?)(?:\s+\{#([A-Za-z0-9_-]+)\})?\s*$")
_SHOT = re.compile(r"^!\[(.*)\]\(shot:([A-Za-z0-9_-]+)\)\s*$")
_OL = re.compile(r"^\d+\.\s+(.*)$")


class _Renderer:
    def __init__(self, lang: str):
        self.lang = lang
        self.heading_count = 0

    def render(self, text: str) -> str:
        lines = text.splitlines()
        out: list[str] = []
        i = 0
        while i < len(lines):
            line = lines[i]
            stripped = line.strip()
            if not stripped:
                i += 1
                continue
            if stripped.startswith(":::"):
                kind = stripped[3:].strip()
                j = i + 1
                depth = 1
                while j < len(lines):
                    s = lines[j].strip()
                    if s.startswith(":::") and s[3:].strip():
                        depth += 1
                    elif s == ":::":
                        depth -= 1
                        if depth == 0:
                            break
                    j += 1
                inner = self.render("\n".join(lines[i + 1:j]))
                if kind == "step":
                    item = f"<li>{inner}</li>"
                    if out and out[-1].startswith('<ol class="steps">'):
                        out[-1] = out[-1][: -len("</ol>")] + item + "</ol>"
                    else:
                        out.append(f'<ol class="steps">{item}</ol>')
                else:
                    out.append(f'<aside class="{html.escape(kind)}">{inner}</aside>')
                i = j + 1
                continue
            m = _HEADING.match(stripped)
            if m:
                level = len(m.group(1))
                self.heading_count += 1
                anchor = m.group(3) or f"h-{self.heading_count}"
                out.append(f'<h{level} id="{anchor}">{inline(m.group(2))}</h{level}>')
                i += 1
                continue
            m = _SHOT.match(stripped)
            if m:
                caption, shot = m.group(1), m.group(2)
                src = f"../assets/shots/{self.lang}/{shot}.png"
                out.append(
                    f'<figure class="shot"><a href="{src}"><img src="{src}" '
                    f'alt="{html.escape(caption)}" loading="lazy"></a>'
                    f"<figcaption>{inline(caption)}</figcaption></figure>")
                i += 1
                continue
            if stripped.startswith("|"):
                rows = []
                while i < len(lines) and lines[i].strip().startswith("|"):
                    rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                    i += 1
                head, body = rows[0], [r for r in rows[2:]]
                out.append(
                    "<table><thead><tr>" + "".join(f"<th>{inline(c)}</th>" for c in head)
                    + "</tr></thead><tbody>"
                    + "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>"
                              for r in body)
                    + "</tbody></table>")
                continue
            if stripped.startswith("- ") or _OL.match(stripped):
                ordered = not stripped.startswith("- ")
                items: list[str] = []
                while i < len(lines):
                    s = lines[i]
                    st = s.strip()
                    if ordered and _OL.match(st):
                        items.append(_OL.match(st).group(1))
                    elif not ordered and st.startswith("- "):
                        items.append(st[2:])
                    elif st and s.startswith("  ") and items:
                        items[-1] += " " + st
                    else:
                        break
                    i += 1
                tag = "ol" if ordered else "ul"
                out.append(f"<{tag}>" + "".join(f"<li>{inline(t)}</li>" for t in items)
                           + f"</{tag}>")
                continue
            para = [stripped]
            i += 1
            while i < len(lines):
                st = lines[i].strip()
                if (not st or st.startswith((":::", "#", "|", "- ", "![")) or _OL.match(st)):
                    break
                para.append(st)
                i += 1
            out.append(f"<p>{inline(' '.join(para))}</p>")
        return "\n".join(out)


def render_markdown(body: str, lang: str) -> str:
    return _Renderer(lang).render(body)


# ---------------------------------------------------------------- site

def copy_tree(src: Path, dst: Path) -> None:
    shutil.copytree(src, dst, dirs_exist_ok=True)


def copy_skeleton(src_root: Path, dst_root: Path) -> None:
    """Template and assets only; used by the tests to build into a scratch folder."""
    dst_root.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src_root / "template.html", dst_root / "template.html")
    copy_tree(src_root / "assets", dst_root / "assets")


def _pages_for(root: Path, lang: str) -> list[tuple[str, dict[str, str], str]]:
    out = []
    for path in sorted((root / "content" / lang).glob("*.md")):
        meta, body = parse_front_matter(path.read_text(encoding="utf-8"))
        out.append((path.stem, meta, body))
    out.sort(key=lambda p: int(p[1].get("order", "999")))
    return out


def _fill(template: str, values: dict[str, str]) -> str:
    return re.sub(r"\{\{(\w+)\}\}", lambda m: values[m.group(1)], template)


def build(root: Path) -> dict[Path, str]:
    template = (root / "template.html").read_text(encoding="utf-8")
    result: dict[Path, str] = {}
    for lang in LANGS:
        pages = _pages_for(root, lang)
        for index, (name, meta, body) in enumerate(pages):
            nav = "".join(
                f'<li><a href="{n}.html"{" aria-current=\"page\"" if n == name else ""}>'
                f"{html.escape(m.get('title', n))}</a></li>" for n, m, _ in pages)
            prev_link = next_link = ""
            if index > 0:
                p = pages[index - 1]
                prev_link = (f'<a class="prev" href="{p[0]}.html"><span>{UI[lang]["prev"]}</span>'
                             f"{html.escape(p[1].get('title', p[0]))}</a>")
            if index + 1 < len(pages):
                n = pages[index + 1]
                next_link = (f'<a class="next" href="{n[0]}.html"><span>{UI[lang]["next"]}</span>'
                             f"{html.escape(n[1].get('title', n[0]))}</a>")
            switcher = "".join(
                f'<a href="../{other}/{name}.html" lang="{other}"'
                f'{" aria-current=\"true\"" if other == lang else ""}>'
                f'<img src="../assets/flags/{other}.png" alt="">{LANG_NAMES[other]}</a>'
                for other in LANGS)
            result[root / lang / f"{name}.html"] = _fill(template, {
                "lang": lang,
                "dir": "rtl" if lang in RTL else "ltr",
                "title": html.escape(meta.get("title", name)),
                "summary": html.escape(meta.get("summary", "")),
                "manual": UI[lang]["manual"],
                "contents": UI[lang]["contents"],
                "language": UI[lang]["language"],
                "nav": nav,
                "switcher": switcher,
                "body": render_markdown(body, lang),
                "prev": prev_link,
                "next": next_link,
            })
    result[root / "index.html"] = _chooser()
    return result


def _chooser() -> str:
    cards = "".join(
        f'<a class="choice" href="{lang}/index.html" lang="{lang}" dir="{"rtl" if lang in RTL else "ltr"}">'
        f'<img src="assets/flags/{lang}.png" alt=""><span>{UI[lang]["manual"]}</span>'
        f"<small>{LANG_NAMES[lang]}</small></a>" for lang in LANGS)
    return (
        '<!doctype html>\n<html lang="ar" dir="rtl">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
        "<title>VLMS</title>\n"
        '<link rel="icon" href="assets/brand/icon-64.png">\n'
        '<link rel="stylesheet" href="assets/manual.css">\n</head>\n'
        '<body class="chooser">\n<main>\n'
        '<img class="logo" src="assets/brand/icon-64.png" alt="">\n<h1>VLMS</h1>\n'
        f'<nav class="choices">{cards}</nav>\n</main>\n</body>\n</html>\n')


# ---------------------------------------------------------------- checks

_TABLE_FN = {"ar": "arabicStrings", "fr": "frenchStrings", "en": "englishStrings"}


def load_ui_labels(strings_cpp: Path) -> dict[str, set[str]]:
    text = strings_cpp.read_text(encoding="utf-8")
    starts = {lang: text.index(f"StringTable {fn}()") for lang, fn in _TABLE_FN.items()}
    order = sorted(starts.items(), key=lambda kv: kv[1])
    labels: dict[str, set[str]] = {}
    for idx, (lang, start) in enumerate(order):
        end = order[idx + 1][1] if idx + 1 < len(order) else len(text)
        chunk = text[start:end]
        values = set()
        for m in re.finditer(r'\{"[^"]+",\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\}', chunk):
            parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
            values.add(bytes("".join(parts), "utf-8").decode("unicode_escape")
                       .encode("latin-1").decode("utf-8") if "\\" in "".join(parts)
                       else "".join(parts))
        labels[lang] = values
    return labels


def _norm(label: str) -> str:
    return label.strip().rstrip(":…").rstrip().rstrip(":").strip()


def check_site(root: Path, pages: dict[Path, str]) -> list[str]:
    problems: list[str] = []
    labels = load_ui_labels(REPO / "libraries/Core/src/Strings.cpp")
    norm_labels = {lang: {_norm(v) for v in vals} for lang, vals in labels.items()}
    for lang in LANGS:
        seen_orders: dict[str, str] = {}
        for name in PAGES:
            src = root / "content" / lang / f"{name}.md"
            if not src.exists():
                problems.append(f"missing page: {lang}/{name}")
                continue
            meta, _ = parse_front_matter(src.read_text(encoding="utf-8"))
            for key in ("title", "order", "summary"):
                if not meta.get(key):
                    problems.append(f"{lang}/{name}: front matter lacks {key}")
            order = meta.get("order", "")
            if order in seen_orders:
                problems.append(f"{lang}/{name}: order {order} also used by {seen_orders[order]}")
            seen_orders[order] = name
    for path, text in pages.items():
        rel = path.relative_to(root)
        if "http://" in text or "https://" in text:
            problems.append(f"{rel}: external URL")
        for m in re.finditer(r'(?:src|href)="([^"]+)"', text):
            target = m.group(1)
            file_part, _, anchor = target.partition("#")
            dest = (path.parent / file_part).resolve() if file_part else path
            if dest not in pages and not dest.exists():
                kind = "missing image" if target.endswith(".png") else "broken link"
                problems.append(f"{rel}: {kind} {target}")
                continue
            if anchor:
                dest_text = pages.get(dest) or dest.read_text(encoding="utf-8")
                if f'id="{anchor}"' not in dest_text:
                    problems.append(f"{rel}: missing anchor {target}")
        lang = rel.parts[0] if len(rel.parts) > 1 else None
        if lang in LANGS:
            for m in re.finditer(r'<span class="ui">([^<]+)</span>', text):
                label = html.unescape(m.group(1))
                if _norm(label) not in norm_labels[lang]:
                    problems.append(f"{rel}: not a {lang} UI label: {label}")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO / "docs" / "manual")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    pages = build(args.root)
    if args.check:
        problems = check_site(args.root, pages)
        for p in problems:
            print(p)
        return 1 if problems else 0
    for path, text in pages.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    print(f"wrote {len(pages)} pages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

Implementer notes:
- `load_ui_labels` must handle the table entries whose value is split over several adjacent
  string literals on following lines (`{"ocr.readFromImageTip",\n "…"\n "…"}`); the regex
  above allows whitespace/newlines between literals. Values containing `{name}` placeholders
  are loaded as-is and simply never match a label — that is fine. Verify the three-table
  split against the real file (Arabic starts ~line 16, French ~553, English ~1106). If
  `\\` escapes in values make the decoding branch awkward, replace it with a simple
  `.replace('\\"', '"').replace("\\n", "\n")`.
- The Python 3.12+ f-string nested quotes in `build()` are valid on 3.13 (the installed
  version); keep them or hoist into variables, reviewer's choice.

- [ ] **Step 4: Write `docs/manual/template.html`**

```html
<!doctype html>
<html lang="{{lang}}" dir="{{dir}}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{{title}} — VLMS</title>
<meta name="description" content="{{summary}}">
<link rel="icon" href="../assets/brand/icon-64.png">
<link rel="stylesheet" href="../assets/manual.css">
</head>
<body>
<header class="top">
  <a class="brand" href="index.html"><img src="../assets/brand/icon-64.png" alt=""><span>VLMS</span><small>{{manual}}</small></a>
  <nav class="langs" aria-label="{{language}}">{{switcher}}</nav>
</header>
<div class="layout">
  <nav class="sidebar" aria-label="{{contents}}">
    <h2>{{contents}}</h2>
    <ol>{{nav}}</ol>
  </nav>
  <main>
    <h1>{{title}}</h1>
    <p class="summary">{{summary}}</p>
    {{body}}
    <nav class="pager">{{prev}}{{next}}</nav>
  </main>
</div>
<div class="lightbox" hidden><img alt=""></div>
<script src="../assets/manual.js"></script>
</body>
</html>
```

- [ ] **Step 5: Write `docs/manual/assets/manual.css`**

Requirements (write the full stylesheet; values below are the design contract):
- `@font-face { font-family: Cairo; src: url(fonts/Cairo-Variable.ttf); font-weight: 200 1000; }`; body font `Cairo, system-ui, sans-serif`, 17px, line-height 1.7.
- Tokens on `:root`: `--bg #f7f8fa`, `--surface #ffffff`, `--text #1f2937`, `--muted #6b7280`, `--border #e5e7eb`, `--accent #0f766e`, `--accent-soft #ccfbf1`, `--note #eff6ff`/`--note-border #3b82f6`, `--warn #fff7ed`/`--warn-border #f97316`, `--ui-bg #eef2f7`. Dark (`@media (prefers-color-scheme: dark)`): `--bg #0f1419`, `--surface #151b23`, `--text #e5e7eb`, `--muted #9ca3af`, `--border #2a3441`, `--accent #5eead4`, `--accent-soft #134e4a`, `--note #172554`/`#60a5fa`, `--warn #431407`/`#fb923c`, `--ui-bg #1f2a37`.
- Layout: sticky `header.top` (brand start, `.langs` end, flags 20px round); `.layout` grid `16rem 1fr`, max-width 78rem, centred; below 820px the sidebar stacks above `main`.
- Sidebar: `border-inline-end`, current page (`[aria-current=page]`) with `border-inline-start: 3px solid var(--accent)` and `background: var(--accent-soft)`.
- `main` max-width 52rem; `h1` 2rem; `.summary` muted, 1.1rem.
- `span.ui`: inline-block, `padding-inline: .45em`, `border: 1px solid var(--border)`, `border-radius: 6px`, `background: var(--ui-bg)`, `font-weight: 600`, `white-space: nowrap`.
- `figure.shot`: margin-block 1.25rem, `img` max-width 100%, border, radius 8px, subtle shadow, `cursor: zoom-in`; `figcaption` muted, .9rem, centred.
- `ol.steps`: `counter-reset: step`, `list-style: none`, `padding-inline-start: 0`; each `li` has `counter-increment: step`, `position: relative`, `padding-inline-start: 3rem`, `margin-block-end: 1.75rem`; `li::before` shows the counter in a 2rem accent circle at `inset-inline-start: 0`.
- `aside.note` / `aside.warning`: `border-inline-start: 4px solid`, padding 1rem, radius 6px, the tokens above.
- Tables: full width, collapsed borders, `th` background `--ui-bg`, `text-align: start`.
- `.pager`: flex, space-between; `.prev`/`.next` bordered cards with a muted small label `span`; `.next` aligned to the inline end.
- `.lightbox`: fixed inset 0, `background: rgb(0 0 0 / .85)`, grid centred, image max 95vw×95vh, `cursor: zoom-out`.
- `body.chooser`: full-viewport centred column; `.choices` row of three `.choice` cards (flag 64px, manual title, small language name), wrapping on narrow screens.
- `@media print`: hide header, sidebar, pager; images max-height 60vh.
- **No physical `left`/`right`/`margin-left`/`padding-right` anywhere** (the reviewer greps for `left|right` outside `text-align` comments).

- [ ] **Step 6: Write `docs/manual/assets/manual.js`**

```js
// Opens a screenshot full-size on click; Escape or a click closes it.
(function () {
  var box = document.querySelector('.lightbox');
  if (!box) return;
  var big = box.querySelector('img');
  document.querySelectorAll('figure.shot a').forEach(function (link) {
    link.addEventListener('click', function (event) {
      event.preventDefault();
      big.src = link.getAttribute('href');
      big.alt = link.querySelector('img').alt;
      box.hidden = false;
    });
  });
  function close() { box.hidden = true; big.removeAttribute('src'); }
  box.addEventListener('click', close);
  document.addEventListener('keydown', function (e) { if (e.key === 'Escape') close(); });
})();
```

Current-page marking is done by the generator (`aria-current`), so the script needs nothing else.

- [ ] **Step 7: Copy the font and icon; run tests**

```bash
mkdir -p docs/manual/assets/fonts docs/manual/assets/brand
cp applications/vlms/resources/fonts/Cairo-Variable.ttf docs/manual/assets/fonts/
cp applications/vlms/resources/images/brand/icon-64.png docs/manual/assets/brand/
python3 -m unittest discover -s scripts/manual -p 'test_build_manual.py' -v
```
Expected: all tests pass; the two `RealSiteTest` tests are skipped.

- [ ] **Step 8: Visual check of the fixture site**

Build the fixtures into a scratch folder
(`python3 -c "import sys;sys.path.insert(0,'scripts');import build_manual as b,pathlib as p;r=p.Path('/tmp/…');…"` or a tiny helper in the scratchpad), open `ar/index.html` and
`en/index.html` in a browser, and confirm: RTL sidebar on the right in Arabic, left in
English; dark mode follows the system. Report what you saw.

- [ ] **Step 9: Hand to the controller for review and commit**

Commit message: `Generate the user manual from Markdown in three languages.`

---

### Task 3: Capture tool — harness, safety and general shots

**Files:**
- Create: `applications/vlms/manual_capture/CMakeLists.txt`
- Create: `applications/vlms/manual_capture/main.cpp`
- Create: `applications/vlms/manual_capture/Capture.h`, `Capture.cpp`
- Create: `applications/vlms/manual_capture/Shots.h`
- Create: `applications/vlms/manual_capture/shots_general.cpp`
- Create: `applications/vlms/manual_capture/shots_screens.cpp` (empty registrar only: `void registerScreenShots(ShotRegistry&) {}`)
- Create: `applications/vlms/manual_capture/shots_tasks.cpp` (empty registrar only)
- Create: `scripts/manual/capture.sh`
- Modify: `applications/vlms/CMakeLists.txt` (append the option block below)

**Interfaces:**
- Produces (in `Shots.h`, used verbatim by Tasks 4 and 5):

```cpp
#pragma once

#include <QString>

#include <functional>
#include <map>

namespace ManualCapture {

class Capture;

/// One screenshot. Writes <out>/shots/<lang>/<id>.png through Capture::save.
/// Must leave the window where it found it: Catalogue page, no dialog open,
/// light theme, no ticks, empty search.
using ShotFn = std::function<void(Capture&)>;

/// Keyed by shot id, so --only <id> and a stable run order come for free.
using ShotRegistry = std::map<QString, ShotFn>;

void registerGeneralShots(ShotRegistry& registry);   // Task 3
void registerScreenShots(ShotRegistry& registry);    // Task 4
void registerTaskShots(ShotRegistry& registry);      // Task 5

}  // namespace ManualCapture
```

- Produces (in `Capture.h`) — the helpers every shot uses:

```cpp
#pragma once

#include <QList>
#include <QPixmap>
#include <QRect>
#include <QString>
#include <QWidget>

#include <functional>

class MainWindow;
class QAbstractButton;
class QTableWidget;

namespace ManualCapture {

class Capture {
public:
    Capture(MainWindow& window, QString outRoot, QString lang);

    [[nodiscard]] MainWindow& window() const;
    [[nodiscard]] const QString& lang() const;

    /// Clicks the header nav button whose text is T(navKey), e.g. "nav.members",
    /// then processes events until the page settles.
    void goTo(const char* navKey);
    /// The visible page's widget (the QStackedWidget's current widget).
    [[nodiscard]] QWidget* page() const;

    /// First visible QAbstractButton under root whose text is T(key) (after
    /// stripping '&'). Throws std::runtime_error naming the key when absent.
    [[nodiscard]] QAbstractButton* button(const char* key, QWidget* root = nullptr) const;
    /// First QTableWidget under root (default: page()).
    [[nodiscard]] QTableWidget* table(QWidget* root = nullptr) const;
    /// First child of type W under root whose objectName is name.
    template <typename W>
    [[nodiscard]] W* named(const QString& name, QWidget* root = nullptr) const;

    void click(QWidget* widget);           // QTest::mouseClick at its centre + settle()
    void typeInto(QWidget* edit, const QString& text);  // clear, QTest::keyClicks, settle()
    void settle(int ms = 150);             // processEvents + qWait so paging/queries finish

    /// Opens a modal by running trigger (which calls exec()), then runs inside on the
    /// modal widget while it is up. inside must close the modal (reject/accept/answer).
    /// Nested modals: call openModal again from inside.
    void openModal(const std::function<void()>& trigger,
                   const std::function<void(QWidget* modal)>& inside);

    /// Grab of widget (default: the whole window) with any visible popup
    /// (QComboBox view, QMenu) composited at its on-screen position.
    [[nodiscard]] QPixmap grab(QWidget* widget = nullptr) const;
    /// Grab of the window cropped to the union of widgets' window rects, padded by 12px.
    [[nodiscard]] QPixmap grabRegion(const QList<QWidget*>& widgets) const;

    /// Numbered circles ①..⑳ at each target's top inline-start corner (right corner in
    /// RTL), offset 6px inward; targets are in the coordinate space of `base`, which is
    /// the widget that was grabbed. Returns the annotated pixmap.
    [[nodiscard]] QPixmap callouts(QPixmap image, QWidget* base,
                                   const QList<QWidget*>& targets) const;
    /// Same, with rects already in image coordinates (for table cells, header sections).
    [[nodiscard]] QPixmap calloutsAt(QPixmap image, const QList<QRect>& rects) const;

    /// Writes <out>/shots/<lang>/<id>.png; logs "shot <lang>/<id> WxH".
    void save(const QString& id, const QPixmap& image) const;

    /// Restores the neutral state described on ShotFn.
    void reset();

private:
    MainWindow& m_window;
    QString m_outRoot;
    QString m_lang;
};

}  // namespace ManualCapture
```

- Produces: CLI `manual_capture --sandbox <dir> --out docs/manual/assets --lang ar|en|fr [--only <id>[,<id>...]] [--list]`. Exit 0 when every requested shot saved; 3 when a shot threw (the message names the shot and the reason; the remaining shots still run); 4 on a refused sandbox.
- Produces: `scripts/manual/capture.sh` (see Step 6).

- [ ] **Step 1: CMake option and target**

Append to `applications/vlms/CMakeLists.txt` (after `create_application(...)`, before
the `if(WIN32)` install block):

```cmake
# The user manual's screenshot tool. Off by default: it is a maintainer tool, run by
# scripts/manual/capture.sh, never shipped.
option(VLMS_MANUAL_CAPTURE "Build the manual screenshot tool" OFF)
if(VLMS_MANUAL_CAPTURE)
    add_subdirectory(manual_capture)
endif()
```

`applications/vlms/manual_capture/CMakeLists.txt`:

```cmake
qt_add_executable(manual_capture
    main.cpp
    Capture.cpp
    Capture.h
    Shots.h
    shots_general.cpp
    shots_screens.cpp
    shots_tasks.cpp
    ../resources/fonts.qrc
    ../resources/images.qrc
)
set_target_properties(manual_capture PROPERTIES AUTOMOC ON AUTORCC ON)
target_include_directories(manual_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(manual_capture PRIVATE
    vlms_ui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Test
)
```

If `qt_add_executable` or the resource paths clash with how `create_application` wires
resources (see `cmake/BuildUtils.cmake`), mirror `create_application`'s approach instead —
the requirement is that `:/fonts/Cairo-Variable.ttf` and `:/brand/…` resolve, exactly as in
the shipped app. `Qt::Test` must be found; if the top-level `find_package` does not include
`Test` when tests are off, add `find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)`
in this CMakeLists.

- [ ] **Step 2: Safety guards in `main.cpp` (write these first; they are the reason this task exists)**

```cpp
#include "Application.h"
#include "Capture.h"
#include "Shots.h"
#include "ui/FlagIcons.h"
#include "ui/MainWindow.h"

#include <VLMS/Core/Paths.h>

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QSettings>
#include <QTextStream>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace {

constexpr const char* kMarker = ".vlms-manual-sandbox";

/// Everything here runs before QApplication exists, so it uses plain Qt core only.
int refuse(const QString& why)
{
    std::fprintf(stderr, "manual_capture: refusing: %s\n", qPrintable(why));
    return 4;
}

QString argValue(int argc, char** argv, const char* name)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], name) == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return {};
}

}  // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariable("QT_QPA_PLATFORM") != QStringLiteral("offscreen")) {
        return refuse(QStringLiteral("QT_QPA_PLATFORM must be offscreen; this tool never drives a desktop"));
    }
    const QString sandboxArg = argValue(argc, argv, "--sandbox");
    if (sandboxArg.isEmpty()) {
        return refuse(QStringLiteral("--sandbox <dir> is required"));
    }
    const QString sandbox = QFileInfo(sandboxArg).canonicalFilePath();
    if (sandbox.isEmpty() || !QFile::exists(sandbox + QLatin1Char('/') + QLatin1String(kMarker))) {
        return refuse(QStringLiteral("%1 has no %2 marker; run scripts/manual/prepare_sandbox.py")
                          .arg(sandboxArg, QLatin1String(kMarker)));
    }
    const QString sandboxDb = QFileInfo(sandbox + QStringLiteral("/database/vlms.db")).canonicalFilePath();
#ifdef VLMS_PROJECT_ROOT
    const QString liveDb = QFileInfo(QStringLiteral(VLMS_PROJECT_ROOT "/database/vlms.db")).canonicalFilePath();
    if (!liveDb.isEmpty() && sandboxDb == liveDb) {
        return refuse(QStringLiteral("the sandbox database is the live database"));
    }
#endif
    // Before Application: its constructor keeps a non-empty root and opens <root>/database.
    VLMS::Paths::setProjectRoot(sandbox.toStdString());
    // Settings (language, theme) go into the sandbox, never the user's own profile.
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, sandbox + QStringLiteral("/config"));
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, sandbox + QStringLiteral("/config"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    Application app(argc, argv);
    if (!app.isDatabaseReady()) {
        return refuse(QStringLiteral("the sandbox database did not open"));
    }
    if (VLMS::qs(VLMS::Paths::databaseDirectory()) != sandbox + QStringLiteral("/database")) {
        return refuse(QStringLiteral("the project root moved away from the sandbox"));
    }
    // ... parse --out, --lang, --only, --list with QCommandLineParser (below)
}
```

`VLMS_PROJECT_ROOT` is a private define of Core, so it is not visible here. Add it to
the target with `target_compile_definitions(manual_capture PRIVATE VLMS_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")`
purely so the guard can compare against the live path (the tool itself never uses it as its
root). `qs` lives in `QtBridge.h` — include it.

- [ ] **Step 3: Run loop in `main.cpp`**

After the guards:

1. Parse `--out` (required), `--lang` (required, one of `ar en fr`), `--only` (optional
   comma list), `--list` (prints the registry's ids, one per line, exits 0).
2. `QFont f = app.font(); f.setStyleStrategy(QFont::StyleStrategy(f.styleStrategy() | QFont::NoSubpixelAntialias)); app.setFont(f);`
3. `app.setUiLocale(lang)`; `app.setDarkTheme(false)`.
4. `MainWindow window; window.resize(1440, 900); window.show(); QTest::qWaitForWindowExposed(&window); settle(800)`.
5. Build the registry with the three `register…` calls; if `--only` names an id not in it, print
   `unknown shot <id>` and return 1.
6. For each selected id: `try { fn(capture); } catch (const std::exception& e) { failures << id + ": " + e.what(); }`, then `capture.reset()` regardless.
7. Write the flags once per run (cheap, idempotent): for each of `ar en fr`,
   `VLMS::languageFlagIcon(code, 64).pixmap(64, 64).save(out + "/flags/" + code + ".png")`.
8. Print failures to stderr; return 3 if any, else 0.

- [ ] **Step 4: Implement `Capture.cpp`**

Behaviour each helper must have (write the code; these are the acceptance rules):
- `goTo`: finds the `QPushButton#navLink` whose `text() == T(navKey)` under `window()`,
  clicks it, `settle(400)`.
- `page()`: `window().findChild<QStackedWidget*>()->currentWidget()`.
- `button`: search `findChildren<QAbstractButton*>()` under root (default `QApplication::activeModalWidget()` if one is up, else `page()`), match `text().remove('&') == T(key)` and `isVisible()`; throw `std::runtime_error("no button " + key)` otherwise.
- `click`: `QTest::mouseClick(widget, Qt::LeftButton, {}, widget->rect().center())`, then `settle()`.
  For a click that opens a modal use `openModal` instead — a direct click would block.
- `openModal`: `QTimer::singleShot(0, …)` polls `QApplication::activeModalWidget()` up to 3 s
  (10 ms steps, `QTest::qWait`), calls `settle(250)`, then `inside(modal)`; then runs
  `trigger()`. If no modal appears, throw after `trigger()` returns (record the failure
  in a flag the timer sets; do not throw inside the timer).
- `grab`: `widget->grab()`; then for every top-level visible widget that is a popup
  (`windowFlags() & Qt::Popup`) and whose top-left maps inside `widget`, draw its `grab()`
  at `widget->mapFromGlobal(popup->mapToGlobal(QPoint(0,0)))`. Offscreen popups report
  global positions consistently with their parent window, which is what makes this work.
- `grabRegion`: union of `w->mapTo(&window(), QPoint())`-based rects, padded 12 px, clipped
  to the window, `grab(&window()).copy(rect)`.
- `callouts`/`calloutsAt`: circle diameter 30 px, fill `#0f766e`, white 2 px ring,
  white bold number (Cairo, 15 px); position at the target rect's top-left + (6,6) in LTR,
  top-right − (36,−6) in RTL (`QApplication::layoutDirection()`); never outside the image
  (clamp). Use the pixmap's `devicePixelRatio`.
- `save`: `QDir().mkpath(out + "/shots/" + lang)`; `image.save(path, "PNG")`; print
  `shot <lang>/<id> <w>x<h>` to stdout. Throw if `save` returns false.
- `reset`: close every visible top-level other than the main window (`reject()` dialogs,
  `close()` others); `app.setDarkTheme(false)`; `goTo("nav.catalog")`; clear the page's
  search edit (`ListPageFrame::searchEdit()` is reachable as the first `QLineEdit` in the
  page's search area — find the edit whose `placeholderText() == T("catalog.searchPlaceholder")`);
  untick every row (click the header tick until unchecked, or set every first-column item's
  check state to `Qt::Unchecked`), `settle()`.

- [ ] **Step 5: General shots in `shots_general.cpp`**

Implement and register `gs-window`, `gs-dark`, `gs-ticks`, `gs-sort`, `gs-licence` exactly as
the Shot contract describes. Guidance:
- `gs-window`: callout targets are widgets: the first nav button (use its parent header
  area — callout ① on the `m_catalogNav` button is fine), the `LanguageSelector`
  (find by type `VLMS::LanguageSelector`), `QPushButton#themeToggle`, and from the
  page's `ListPageFrame` (find by type) `filterColumn()`, `searchEdit()`, `table()`,
  `pager()`, `detailsPanel()`, `buttonPad()`, and the footer `QWidget#appFooter`.
  Select the first table row first (`table()->selectRow(0)`) so the details panel is filled.
- `gs-dark`: `qobject_cast<Application*>(qApp)->setDarkTheme(true)`, settle, grab, save.
- `gs-ticks`: set rows 0–2 first-column items to `Qt::Checked` by clicking the check
  indicator (use `QTest::mouseClick` on the viewport at the cell's check-box rect, or
  `item->setCheckState` — the reviewer accepts either if the header shows the partial
  state). Crop with `grabRegion({table})` limited to the header + first 8 rows
  (`image.copy(0, 0, w, headerHeight + 8*rowHeight + 12)`). Callouts via `calloutsAt` on
  row 0's check rect and the header section 0 rect.
- `gs-sort`: click the Title header section once
  (`QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, QPoint(header->sectionViewportPosition(titleCol) + 20, header->height()/2))`), settle, crop as above. Find `titleCol` by
  header text `T("catalog.col.title")`.
- `gs-licence`: `openModal([&]{ click(footerLabel); }, [&](QWidget* m){ save("gs-licence", grab(m)); static_cast<QDialog*>(m)->reject(); })` where `footerLabel` is the
  `VLMS::ClickableLabel` under `#appFooter`.

- [ ] **Step 6: `scripts/manual/capture.sh`**

```bash
#!/usr/bin/env bash
# Rebuild the manual's screenshots in all three languages, offscreen, from a scrubbed
# sandbox. Never opens the live database for writing and never touches the desktop.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
build="$repo/build-manual"
sandbox="$repo/build-manual-sandbox"

cmake -S "$repo" -B "$build" -DCMAKE_BUILD_TYPE=Release \
      -DVLMS_MANUAL_CAPTURE=ON -DVLMS_BUILD_TESTS=OFF >/dev/null
cmake --build "$build" --target manual_capture -j"$(nproc)"

python3 "$repo/scripts/manual/prepare_sandbox.py" \
    --source "$repo/database/vlms.db" --books "$repo/resources/books" --out "$sandbox"

bin="$(find "$build" -type f -name manual_capture -perm -u+x | head -n1)"
status=0
for lang in ar en fr; do
    # A fresh copy per language: shots write to the sandbox (a checkout, a renewal),
    # and each language must start from the same data.
    python3 "$repo/scripts/manual/prepare_sandbox.py" \
        --source "$repo/database/vlms.db" --books "$repo/resources/books" --out "$sandbox" >/dev/null
    QT_QPA_PLATFORM=offscreen VLMS_SCHEMA_PATH="$repo/database/schema.sql" \
        "$bin" --sandbox "$sandbox" --out "$repo/docs/manual/assets" --lang "$lang" "$@" || status=$?
done
exit "$status"
```

Check the real name of the tests switch in the top-level `CMakeLists.txt` (`VLMS_BUILD_TESTS`
or `BUILD_TESTING`) and use it. `"$@"` forwards `--only …`.

- [ ] **Step 7: Verify the guards**

Run each and expect exit 4 with the quoted reason:
- `build-manual/…/manual_capture --sandbox /tmp --out /tmp/x --lang en` without
  `QT_QPA_PLATFORM` → "must be offscreen".
- with `QT_QPA_PLATFORM=offscreen` and `--sandbox /tmp` → "no .vlms-manual-sandbox marker".
- `touch database/.vlms-manual-sandbox` is **not** to be done; instead create a scratch folder
  with a marker and a symlink `database -> <repo>/database` and expect "the sandbox database is
  the live database". Remove the scratch folder afterwards.

Then `sha256sum database/vlms.db` before and after the whole step: identical.

- [ ] **Step 8: Capture the general shots**

Run: `scripts/manual/capture.sh --only gs-window,gs-dark,gs-ticks,gs-sort,gs-licence`
Expected: exit 0, 15 `shot …` lines, 15 PNGs under `docs/manual/assets/shots/{ar,en,fr}/`,
plus `docs/manual/assets/flags/{ar,en,fr}.png`. Open `ar/gs-window.png` and `en/gs-window.png`
with the Read tool and confirm: right language, RTL mirror in Arabic, callouts ①–⑩ on the
right regions, no real member name (members are not on this page, but check the details
panel shows a book). Report what you saw.

- [ ] **Step 9: Hand to the controller for review and commit**

Commit message: `Capture the manual's screenshots offscreen from a scrubbed sandbox.`
The PNGs are committed with the task.

---

### Task 4: Screen-page shots

**Files:**
- Modify: `applications/vlms/manual_capture/shots_screens.cpp` (only this file)

**Interfaces:**
- Consumes: `Capture` API and `ShotRegistry` from Task 3; the sandbox from Task 1.
- Produces: shots `cat-overview`, `cat-search-number`, `cat-number-dropdown`,
  `cat-copy-colours`, `cat-loans-dialog`, `mem-overview`, `mem-details`,
  `mem-delete-blocked`, `mem-loans-dialog`, `circ-overview`, `circ-overdue`,
  `circ-search-name`, `arc-books`, `arc-copies`, `arc-loans`, `arc-members`,
  `arc-restore-confirm`, `arc-purge-confirm`, `met-overview`, `met-full`, in all three
  languages.

- [ ] **Step 1: Pick the data each shot needs, from the sandbox, with SQL, before writing C++**

Each shot must find its example rows *by query at run time*, not by hard-coded ids, so a
re-run on tomorrow's data still works. Write these as small helpers at the top of
`shots_screens.cpp` using the repositories reachable through
`qobject_cast<Application*>(qApp)` (`catalog()`, `members()`, `circulation()`), e.g.:
- a live title with ≥ 3 copies and at least one copy on loan (for `cat-search-number`,
  `cat-number-dropdown`, `cat-copy-colours`): list books sorted by copies descending via
  `CatalogRepository::listBooks` with a `BookQuery`, take the first whose
  `localIdsOnLoan` is non-empty and whose `localIds.size() >= 3`.
- a member with an unreturned loan (for `mem-delete-blocked`, `mem-loans-dialog`).
- an archived book with zero copies, an archived copy, an archived loan, an archived member
  (Archive shots; the live data has thousands of archived loans and members — see
  CLAUDE.md 2026-09-23; if no archived book exists, archive one live copy-less-able book
  **in the sandbox** through the Catalogue `Delete` flow first and say so in the report).
Read `libraries/Core/include/VLMS/Core/*.h` for the exact query structs.

- [ ] **Step 2: Implement the shots**

Follow the Shot contract table for content and callouts. Rules:
- Navigate with `goTo`, select with the table (`selectRow` + `settle`) or by searching.
- Filters: the filter widgets are the page's `ListPageFrame::filterColumn()` children;
  pick list entries by their text (`T("circulation.status.overdue")` etc.).
- Archive type: the type list's entries are `T("archive.type.books")`, `…copies`,
  `…loans`, `…members`.
- Confirmation boxes (`arc-restore-confirm`, `arc-purge-confirm`, `mem-delete-blocked`):
  `openModal`, save the grab, then **answer No / Cancel / Close** — these shots never
  change data. For `mem-delete-blocked` answer with the plain close/cancel button, not
  the jump.
- `cat-number-dropdown`: click the Local Number cell of the chosen row to create its
  editor combo, call `showPopup()` on it, `settle(300)`, then `grab()` (which composites the
  popup); crop with `grabRegion({table})`. Close the popup with `hidePopup()` afterwards.
- `met-full`: `MetricsPage`'s viewer is a scroll area; grab its inner widget
  (`QScrollArea::widget()`) after `adjustSize()`, not the viewport.
- `circ-search-name`: take the member of the first open loan, type their (scrubbed)
  full name into the search box.

- [ ] **Step 3: Capture and inspect**

Run: `scripts/manual/capture.sh --only <the 20 ids, comma-separated>`
Expected: exit 0, 60 `shot` lines. Open every `en/*.png` and at least `ar/cat-overview.png`,
`ar/arc-copies.png`, `ar/mem-overview.png` with the Read tool. Confirm for each: the right
screen, the right language, callouts on the intended widgets, nothing clipped, and every
member name visible is one of the fake names in `prepare_sandbox.FAKE_FIRST`/`FAKE_LAST`.
List every image in the report with one line on what it shows.

- [ ] **Step 4: Hand to the controller for review and commit**

Commit message: `Capture the screen pages for the manual.`

---

### Task 5: Task-page shots

**Files:**
- Modify: `applications/vlms/manual_capture/shots_tasks.cpp` (only this file)

**Interfaces:**
- Consumes: as Task 4.
- Produces: shots `add-book-open`, `add-book-filled`, `add-book-copies`,
  `add-book-free-number`, `add-book-categories`, `reg-open`, `reg-filled`, `reg-renew`,
  `reg-renewed`, `lend-dialog`, `lend-filled`, `lend-from-book`, `lend-from-member`,
  `lend-empty-history`, `ret-dialog`, `ext-dialog`, `ret-from-history`, `rm-book-confirm`,
  `rm-book-refused`, `rm-copy-row`, `rm-loan-confirm`, `rm-bulk-confirm`, `reuse-chooser`,
  `reuse-editor`, in all three languages.

- [ ] **Step 1: Example data by query (as Task 4 Step 1)**

Needed: a never-borrowed live title (`lend-empty-history`), a live title with a copy on
loan (`rm-book-refused`), an open loan (`ret-*`, `ext-dialog`, `ret-from-history`), a
returned live loan (`rm-loan-confirm`), a Non active member (`reg-renew`), an archived copy
whose number is released (`reuse-*`), a borrowable (active) member and an available copy
(`lend-filled`).

- [ ] **Step 2: Implement**

Rules:
- **Every dialog is closed with Cancel / reject / No.** No shot saves a book, a member or a
  loan. `reg-renewed` only changes the combo inside the open dialog and grabs the updated
  last-active-day label; then Cancel.
- Fill fields by `typeInto` on the dialog's line edits found by their row label
  (`QFormLayout::labelForField` / the label whose text is `T("book.field.title")` etc.) and
  combos by `setCurrentIndex(findText(...))`. Use fake example values that read naturally in
  each language — the same book title in all three (e.g. `الأيام` by `طه حسين`), and a
  member from `FAKE_FIRST`/`FAKE_LAST`.
- `add-book-free-number`: in the Copies tab, add a copy row with `T("book.copy.add")`, open
  the local-number cell's editor (`FreeLocalNumberDelegate` builds an editable combo), call
  `showPopup()`, grab with the popup composited.
- `add-book-categories`: `openModal` on the Category row's button inside the open Add Book
  dialog (nested modal), grab it, reject it, then reject the book dialog.
- `lend-from-book` / `lend-from-member`: open the `Loans` dialog, then nested `openModal` on
  its `T("circulation.checkout")` button.
- `rm-bulk-confirm`: tick rows 0–2 on Catalogue (pick three titles with no copy on loan —
  search first if needed), `openModal` on `T("catalog.delete")`, grab, answer No.
- `rm-copy-row`: open `Edit` on a multi-copy title with all copies on the shelf, Copies tab,
  select a row, callout on the `T("book.copy.remove")` button, grab, Cancel.
- `reuse-chooser` / `reuse-editor`: Archive → Copies → select the archived copy →
  `openModal` on `T("archive.reuse")`; grab the chooser; to reach the editor, pick the first
  book in the chooser and accept it inside a nested `openModal` that grabs the editor and
  rejects it.

- [ ] **Step 3: Capture and inspect** (as Task 4 Step 3; 72 `shot` lines expected)

Additionally prove nothing was written: after the run, compare
`sqlite3`-free counts via Python — `SELECT count(*) FROM books`, `book_copies`, `loans`,
`members` and `SUM(archived_at IS NULL)` in each — against a fresh `prepare_sandbox.py`
output. They must be equal. Report the numbers.

- [ ] **Step 4: Hand to the controller for review and commit**

Commit message: `Capture the task pages for the manual.`

---

### Content tasks (6–9): shared rules

These four tasks write Markdown only, in the subset defined in Task 2, for all three
languages. Every implementer reads first:
- the spec (whole), the Shot contract above, and the PNGs of their shots in all three
  languages (Read tool) — describe what the image actually shows;
- `libraries/Core/src/Strings.cpp`: every control name in backticks must be copied **exactly**
  from that language's table (Arabic from `arabicStrings()`, French from `frenchStrings()`,
  English from `englishStrings()`). The site check rejects anything else;
- the Session log in `CLAUDE.md` for behaviour (loan states, statuses, archive vs purge,
  free numbers, ticks);
- the code when behaviour is unclear — the manual describes what the app does, not what
  a spec hoped.

Writing rules:
- Arabic first, written as a librarian would say it (Modern Standard Arabic, Tunisian
  library context, no calques from English). Then French and English carry the same
  content and the same shots in the same order — not word-for-word translations, but no
  section present in one and missing in another.
- Front matter `order` values: index 1, getting-started 2, catalogue 3, members 4,
  circulation 5, archive 6, metrics 7, task-add-book 8, task-register-member 9,
  task-lend-book 10, task-return-extend 11, task-remove-book 12, task-reuse-number 13,
  reference 14 — identical in all languages.
- Screen pages open with the overview shot and a numbered list whose numbers match the
  callouts exactly.
- Task pages are a sequence of `::: step` blocks: first line the action in bold
  (`**Click `Add Book`.**`), then what the application does in response, then the step's
  shot where there is one. End with a short "What if…" section (`###`) for the refusals the
  app can give on that path.
- Cross-link generously: `[the loan states](reference#loan-states)`. Anchors used across
  pages are fixed here and must exist: `reference#loan-states`, `reference#member-status`,
  `reference#live-archived`, `reference#local-number`, `reference#fields-book`,
  `reference#fields-member`, `getting-started#ticks`, `getting-started#list-shape`,
  `archive#purge-order`.
- No file paths, class names, SQL, or database vocabulary ("row", "column" is fine as a
  table column the reader sees; "record" is fine; "NULL", "schema" are not).
- British English in `en`.

Each content task ends with:
- Run: `python3 scripts/build_manual.py --check 2>&1 | grep -E '/(<your pages>)\b' || true`
  — no problem may name one of your pages. (Other tasks' pages may still be missing.)
- Hand to the controller for review; the controller commits the Markdown only (HTML is
  generated in Task 10).

### Task 6: Content — index, getting-started, reference

**Files:** Create `docs/manual/content/{ar,en,fr}/{index,getting-started,reference}.md`

**Interfaces:** Produces anchors `getting-started#list-shape`, `getting-started#ticks`,
`reference#loan-states`, `reference#member-status`, `reference#live-archived`,
`reference#local-number`, `reference#fields-book`, `reference#fields-member`.

Required sections:
- `index`: what VLMS is (one paragraph); the five screens as a list linking to their
  pages; "Where to start" linking getting-started and the three most common tasks
  (lend, return, register); how the manual is organised (screens vs tasks vs reference).
- `getting-started`: launching; `gs-window` with the ten callouts; the header (five nav
  buttons, the three language flags, the theme toggle with `gs-dark`); the footer and the
  licence (`gs-licence`); `## … {#list-shape}` the shape every list page shares (filter
  column, search, table, pager — ALL by default, 20/50/100, First/Last, the bar hides under
  20 rows — details panel, button pad); sorting by a header click (`gs-sort`);
  `## … {#ticks}` ticking rows (`gs-ticks`): the tick in each row's first cell, the header
  tick, a button acts on every ticked row, or on the highlighted row when nothing is
  ticked; Delete/Restore/Permanently remove stay enabled while any row is ticked.
- `reference`: `{#fields-book}` book fields and copy fields (every label on the Book and
  Copies tabs, one line each); `{#fields-member}` member fields; `{#member-status}` the two
  statuses and the last active day (active on a day exactly when the last active day is
  that day or later; registration and renewal set it one year ahead less a day; setting
  Non active ends it yesterday; only an active member can borrow) with `mem-details`;
  `{#loan-states}` Open / Overdue / Returned, exclusive, with `circ-overdue`;
  `{#live-archived}` live vs archived vs permanently removed; `{#local-number}` local vs
  central number, `N (+n)`, green on the shelf, red italic out on loan (the italic is for
  greyscale print and colour-blind readers), with `cat-copy-colours`.

### Task 7: Content — catalogue, members, circulation

**Files:** Create `docs/manual/content/{ar,en,fr}/{catalogue,members,circulation}.md`

Required sections:
- `catalogue`: `cat-overview` + numbered list of the eleven callouts; filters; search
  (title, author, ISBN, local number — exact match wins, otherwise a prefix; `cat-search-number`);
  the Local Number column and its drop-down (`cat-number-dropdown`: picking a number
  marks it in the cell; it does not survive a refresh); colours (`cat-copy-colours`, link
  `reference#local-number`); the `Loans` button and dialog (`cat-loans-dialog`: every
  loan of every copy, archived ones included; it opens even for a never-borrowed title and
  offers Checkout); links to task-add-book, task-remove-book, task-lend-book.
- `members`: `mem-overview` + twelve callouts; the five filters; search; the Loans column
  (counts every loan, archived ones too); details with status and last active day
  (`mem-details`, link `reference#member-status`); photo and ID image; `Loans`
  (`mem-loans-dialog`); Delete refused while a loan is out, and the jump to Circulation
  (`mem-delete-blocked`); links to task-register-member, task-remove-book.
- `circulation`: `circ-overview` + eight callouts; the status filter (All default, Open,
  Overdue, Returned; `circ-overdue`, link `reference#loan-states`); search by title, member
  number or full name (`circ-search-name`); Checkout / Extend / Return / Delete, each
  linking to its task page.

### Task 8: Content — archive, metrics, task-remove-book, task-reuse-number

**Files:** Create `docs/manual/content/{ar,en,fr}/{archive,metrics,task-remove-book,task-reuse-number}.md`

**Interfaces:** Produces anchor `archive#purge-order`.

Required sections:
- `archive`: what the Archive is (deleted = archived, recoverable); the four types
  (`arc-books`, `arc-copies`, `arc-loans`, `arc-members` with their callouts); Restore
  (`arc-restore-confirm`); `## … {#purge-order}` Permanently remove and its order — a loan
  any time; a copy only when no loan names it (Loans column 0); a book only when it has no
  copies left (Copies column 0); a member only when no loan names them — with
  `arc-purge-confirm` and a `::: warning` that it cannot be undone; Reuse local number
  (link task-reuse-number); ticking several rows (link `getting-started#ticks`).
- `metrics`: `met-overview` + callouts; the five sections from `met-full`, each tile
  explained (read `MetricsPage.cpp` and `MetricsRepository` for exactly what each counts;
  Open excludes Overdue); Refresh.
- `task-remove-book`: steps for deleting a book (`rm-book-confirm`; refusal when a copy is
  out: `rm-book-refused`), a copy (`rm-copy-row`), a member (`mem-delete-blocked` for the
  refusal), a loan (`rm-loan-confirm`), several at once (`gs-ticks`, `rm-bulk-confirm`),
  then permanently removing from the Archive in order (link `archive#purge-order`,
  `arc-purge-confirm`). Open with a `::: note` that Delete never destroys anything.
- `task-reuse-number`: two ways, as two `##` sections of steps — (1) adding a copy: the cell
  opens on the next number, the free numbers wait behind it and are only used when picked
  (`add-book-free-number`); (2) from the Archive: `arc-copies`, `reuse-chooser`,
  `reuse-editor`.

### Task 9: Content — task-add-book, task-register-member, task-lend-book, task-return-extend

**Files:** Create `docs/manual/content/{ar,en,fr}/{task-add-book,task-register-member,task-lend-book,task-return-extend}.md`

Required sections:
- `task-add-book`: steps: open (`add-book-open`), fill the Book tab (`add-book-filled`,
  required fields: title and language; the categories list from the Category row:
  `add-book-categories`; the cover), the From image OCR assist (what it fills, that it
  needs a clear photo, that it may be unavailable), Copies tab (`add-book-copies`, number
  of copies, local numbers — `add-book-free-number`, link task-reuse-number), save. "What
  if…": missing title/language, duplicate local number, reserved number.
- `task-register-member`: steps: open (`reg-open`), fill (`reg-filled`: what is required —
  names, date of birth; what the app derives — membership number, age group from the birth
  date, last active day one year ahead less a day), save. Then `##` renewing
  (`reg-renew`, `reg-renewed`) and ending early (pick Non active: the last active day becomes
  yesterday). "What if…": birth date in the future, missing names, invalid email.
- `task-lend-book`: three `##` entry points as step sequences — Circulation (`lend-dialog`,
  `lend-filled`), a book's `Loans` dialog (`lend-from-book`, `lend-empty-history`), a
  member's `Loans` dialog (`lend-from-member`). The due date default and that only an
  active member is offered (link `reference#member-status`). "What if…": no copy
  available, the member is not active.
- `task-return-extend`: return and extend from Circulation (`ret-dialog`, `ext-dialog`) and
  from either `Loans` dialog (`ret-from-history`: the buttons follow the selected loan and
  are enabled only while it is out). "What if…": return date after today, new due date not
  after the current one.

---

### Task 10: Generate, verify and show

**Files:**
- Create (generated): `docs/manual/index.html`, `docs/manual/{ar,en,fr}/*.html`

**Interfaces:** Consumes everything above.

- [ ] **Step 1: Regenerate every screenshot from one clean run**

Run: `scripts/manual/capture.sh`
Expected: exit 0; `ls docs/manual/assets/shots/ar | wc -l` = 49 (= en = fr).

- [ ] **Step 2: Build and check**

```bash
python3 scripts/build_manual.py
python3 scripts/build_manual.py --check
python3 -m unittest discover -s scripts/manual -v
```
Expected: `wrote 43 pages`; `--check` prints nothing and exits 0; all unit tests pass
with `RealSiteTest` now running (not skipped).

- [ ] **Step 3: Privacy gate on the images**

Grep the generated HTML and Markdown for every live full name is not possible without
reading the live DB into a chat; instead run this locally and paste only the count:

```bash
python3 - <<'EOF'
import sqlite3, pathlib
live = sqlite3.connect('file:database/vlms.db?mode=ro', uri=True)
names = {n for (n,) in live.execute("SELECT full_name FROM members WHERE full_name IS NOT NULL") if len(n) > 6}
text = "".join(p.read_text(encoding="utf-8") for p in pathlib.Path("docs/manual").rglob("*.md")) + \
       "".join(p.read_text(encoding="utf-8") for p in pathlib.Path("docs/manual").rglob("*.html"))
print(sum(1 for n in names if n in text))
EOF
```
Expected: `0`. (Images are covered by the sandbox gate; text is covered here.)

- [ ] **Step 4: Open it in a real browser and show it**

Open `docs/manual/index.html`, then `ar/getting-started.html`, `en/task-lend-book.html`,
`fr/archive.html` in the system browser (`xdg-open`). Take screenshots (per the
screenshot-recipe memory, a browser window is fine to capture with the user's normal
tools, or use a headless Chromium/Firefox `--screenshot` if installed) and show them to the
user. Check: Arabic is right-to-left with the sidebar on the right; images load;
the lightbox opens; dark mode follows the system; the language switcher lands on the same
page.

- [ ] **Step 5: Controller commits the generated HTML**

Commit message: `Generate the user manual pages.`

---

### Task 11: Help button in the application

**Files:**
- Create: `applications/vlms/src/ui/ManualLocation.h`, `ManualLocation.cpp`
- Create: `applications/vlms/test/src/test_manual_location.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (add the two sources to `vlms_ui`; add the WIN32 install rule)
- Modify: `applications/vlms/test/CMakeLists.txt` (add the test source)
- Modify: `applications/vlms/src/ui/MainWindow.h/.cpp` (the button)
- Modify: `libraries/Core/src/Strings.cpp` (three keys × three tables)
- Modify: `cmake/WindowsPackaging.cmake` header comment (installed layout gains `manual/`)

**Interfaces:**
- Produces:

```cpp
// ManualLocation.h
#pragma once
#include <QString>

namespace VLMS {

/// The manual page to open for `locale`: the first of
/// <projectRoot>/docs/manual/<locale>/index.html, <appDir>/manual/<locale>/index.html,
/// the same two for "ar", then <projectRoot>/docs/manual/index.html and
/// <appDir>/manual/index.html. Empty when none exists.
[[nodiscard]] QString manualIndexPath(const QString& projectRoot,
                                      const QString& appDir,
                                      const QString& locale);

}  // namespace VLMS
```

- String keys: `nav.help` (ar `الدليل`, fr `Aide`, en `Help`), `help.tooltip` (ar `فتح دليل الاستخدام`,
  fr `Ouvrir le manuel d'utilisation`, en `Open the user manual`), `help.notFound`
  (ar `تعذّر العثور على دليل الاستخدام.`, fr `Le manuel d'utilisation est introuvable.`,
  en `The user manual could not be found.`).

- [ ] **Step 1: Failing tests**

```cpp
// test_manual_location.cpp
#include "ui/ManualLocation.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {
void touch(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
}
}  // namespace

TEST(test_ui_ManualLocation, PrefersTheSourceTreeInTheCurrentLanguage)
{
    QTemporaryDir root, app;
    touch(root.path() + "/docs/manual/fr/index.html");
    touch(app.path() + "/manual/fr/index.html");
    EXPECT_EQ(VLMS::manualIndexPath(root.path(), app.path(), "fr"),
              root.path() + "/docs/manual/fr/index.html");
}

TEST(test_ui_ManualLocation, FindsTheInstalledCopyBesideTheExecutable)
{
    QTemporaryDir root, app;
    touch(app.path() + "/manual/en/index.html");
    EXPECT_EQ(VLMS::manualIndexPath(root.path(), app.path(), "en"),
              app.path() + "/manual/en/index.html");
}

TEST(test_ui_ManualLocation, FallsBackToArabicThenTheChooser)
{
    QTemporaryDir root, app;
    touch(app.path() + "/manual/ar/index.html");
    EXPECT_EQ(VLMS::manualIndexPath(root.path(), app.path(), "fr"),
              app.path() + "/manual/ar/index.html");
    QTemporaryDir root2, app2;
    touch(app2.path() + "/manual/index.html");
    EXPECT_EQ(VLMS::manualIndexPath(root2.path(), app2.path(), "fr"),
              app2.path() + "/manual/index.html");
}

TEST(test_ui_ManualLocation, EmptyWhenThereIsNoManual)
{
    QTemporaryDir root, app;
    EXPECT_TRUE(VLMS::manualIndexPath(root.path(), app.path(), "ar").isEmpty());
}
```

Add `src/test_manual_location.cpp` to `test_vlms_ui`'s `SRC`; add
`src/ui/ManualLocation.cpp` / `.h` to `vlms_ui`.

- [ ] **Step 2: Verify failure** — build `build-sdd-manual-help`, `ctest -R ManualLocation` → link error / FAIL.

- [ ] **Step 3: Implement**

```cpp
// ManualLocation.cpp
#include "ui/ManualLocation.h"

#include <QFileInfo>
#include <QStringList>

namespace VLMS {

QString manualIndexPath(const QString& projectRoot, const QString& appDir, const QString& locale)
{
    QStringList candidates;
    for (const QString& lang : {locale, QStringLiteral("ar")}) {
        candidates << projectRoot + QStringLiteral("/docs/manual/") + lang + QStringLiteral("/index.html")
                   << appDir + QStringLiteral("/manual/") + lang + QStringLiteral("/index.html");
    }
    candidates << projectRoot + QStringLiteral("/docs/manual/index.html")
               << appDir + QStringLiteral("/manual/index.html");
    for (const QString& path : candidates) {
        if (!projectRoot.isEmpty() || !path.startsWith(QStringLiteral("/docs/"))) {
            if (QFileInfo::exists(path)) {
                return path;
            }
        }
    }
    return {};
}

}  // namespace VLMS
```

- [ ] **Step 4: Strings** — add the three keys to all three tables in `Strings.cpp`, next to
  `nav.metrics` / the `licence.*` block. Run `ctest -R StringsParity` → PASS.

- [ ] **Step 5: The button in `MainWindow`**

- Member `QPushButton* m_helpNav = nullptr;`, created with `makeNavButton({})` right after
  `m_metricsNav`, added to the same header loop (after Metrics), tooltip `T("help.tooltip")`
  set in `retranslateUi` together with `m_helpNav->setText(T("nav.help"))`.
- It is never "active" (it opens no page): do not touch `updateNavigation`.
- Click handler:

```cpp
connect(m_helpNav, &QPushButton::clicked, this, [this]() {
    const QString path = VLMS::manualIndexPath(
        VLMS::qs(VLMS::Paths::projectRoot()),
        QCoreApplication::applicationDirPath(),
        VLMS::qs(VLMS::Locale::code()));
    if (path.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        VLMS::showWarning(this, T("nav.help"), T("help.notFound"));
    }
});
```

Check `UiHelpers.h` for the exact name of the warning helper (the codebase has
`showCritical`; use its warning sibling, or `showCritical` if that is the only one) —
always through the helpers, never a raw `QMessageBox`, so the button labels stay localised.

- [ ] **Step 6: Install rule** — in `applications/vlms/CMakeLists.txt`, inside the
  `if(WIN32)` block:

```cmake
    # The user manual, opened by the header's Help button. Only the generated pages and
    # their assets: the Markdown sources and the template stay in the repository.
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/docs/manual/
            DESTINATION ${VLMS_WIN_APP_DIR}/manual
            PATTERN "content" EXCLUDE
            PATTERN "template.html" EXCLUDE)
```

and add `#   manual/                 (user manual, opened by Help)` to the layout comment in
`cmake/WindowsPackaging.cmake`.

- [ ] **Step 7: Full suite** — `QT_QPA_PLATFORM=offscreen ctest --output-on-failure` in the
  task build dir → 100% pass (217 + 4 new).

- [ ] **Step 8: Drive the real app** — MainWindow has no test. Per the screenshot-recipe
  memory, build a sandbox app (`-DVLMS_DEV_PATHS=OFF`, the sandbox DB from Task 1
  copied to `<bindir>/database/`), run it offscreen is *not* enough to prove `openUrl`;
  instead confirm with the controller whether to run on the desktop. Minimum acceptable
  proof without the desktop: an offscreen run of the app that grabs the header showing the
  Help button in all three languages, plus the unit tests above.

- [ ] **Step 9: Hand to the controller** — commit message: `Open the user manual from the header.`

---

## Self-review (done while writing)

- Spec coverage: structure (Task 2), fourteen pages × three languages (6–9, checked in 2/10),
  numbered callouts (3–5), step-by-step task pages (content rules), generator + committed
  HTML (2, 10), logical-properties CSS (2), screenshots with sandbox safety (1, 3; spec
  revised for offscreen), Help button + keys + installer (11), verification (2, 10),
  browser check (10). Out-of-scope items are not planned.
- Names used across tasks: `ShotRegistry`, `ShotFn`, `registerGeneralShots` /
  `registerScreenShots` / `registerTaskShots`, `Capture::{goTo,page,button,table,named,click,typeInto,settle,openModal,grab,grabRegion,callouts,calloutsAt,save,reset}`,
  `manualIndexPath`, `build_manual.{parse_front_matter,render_markdown,build,check_site,load_ui_labels,copy_skeleton,copy_tree}`,
  `prepare_sandbox.{scrub,live_names,remaining_names,FAKE_FIRST,FAKE_LAST,main}` — consistent.
- Shot count: 5 general + 20 screen + 24 task = 49 per language.
