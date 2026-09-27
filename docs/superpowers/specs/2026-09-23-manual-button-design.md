# Manual button — design

*2026-09-23*

Replaces the "Opening the manual from the application" section of
`2026-09-23-user-manual-wiki-design.md`. The Help word after Metrics goes away. The
manual opens from a round button beside the theme toggle, in its own browser window,
and a second click brings that window forward.

## The problem

The header already has a Help word in the page row. It asks the system browser to
open the manual file, and a second click opens another tab. What is wanted is a
round "?" in the same cluster as the flags and the sun, and a second click that
returns to the manual already on screen.

A program cannot pick a tab in someone else's browser. Chrome, Edge, and Firefox
each treat an open request as a new tab, and a page is not allowed to come forward
when the click happened in VLMS. The way around that is a browser window this
application starts itself: a later start with the same address joins that window
and the browser brings it forward. Tried on this machine (KDE, Wayland, Chromium):
the second start logged "Opening in existing browser session" and did not start a
second copy.

## Decisions

| Question | Decision |
|---|---|
| Where the button sits | To the right of the theme toggle. The cluster reads flags, theme, "?". |
| What leaves | The Help word in the page row. The circle is the only way to open the manual. |
| Which browser | Linux: the first of `chromium`, `chromium-browser`, `google-chrome`, `google-chrome-stable`. Windows: Microsoft Edge. Firefox is never used. |
| What a second click does | The same command again. The browser joins the window it already has for that address and leaves the page you are reading. |
| Two languages | Arabic and French are two windows, because the browser remembers the index address. A second click brings forward the window for the language in use. |
| Missing manual | The existing `help.notFound` warning. |
| Missing browser, or the process refuses to start | `help.noBrowser`: ar `تعذّر العثور على متصفح لفتح دليل الاستخدام.`, fr `Aucun navigateur n'a été trouvé pour ouvrir le manuel d'utilisation.`, en `No browser was found to open the user manual.` |

## Revision

*2026-09-23.* The eye check showed a second click opens the index in another window while the page already being read stays underneath. The user dropped the “bring that page forward” requirement. A click opens the manual. Focusing an already-open page is out of scope.

*2026-09-23.* The `--app` window was restoring a maximized placement taller than the work area (bottom 1060 against a work area that ended at 768). `manualWindowRect` now supplies `--window-position` and `--window-size`. Chromium treats that size as the content box and forces the window back to normal, so the frame is why the rect sits 32px inside the usable screen.

## The button

Built in `MainWindow`, directly after the theme toggle. `QPushButton` named
`manualButton`. The `themeToggle` rules in `Theme.cpp` gain this object name in
the same selectors (34px circle, 1px border, radius 17px), so the two cannot drift.

The mark is a painted "?" at 18px, drawn by a function next to `themeModeIcon`
in `Theme.cpp`, in that icon's muted ink, and redrawn when the theme changes.
The button carries no text. The tooltip is the existing `help.tooltip`. The
accessible name is the existing `nav.help`.

## Opening it

`manualIndexPath` stays as it is: the index for the current language, then Arabic,
then the language chooser, looked up under the repository and beside the executable.

Two functions sit beside it in `ManualLocation.{h,cpp}`.

`manualBrowserCandidates` returns the ordered list for a platform. It takes a
Windows flag and the two Program Files directories, so a test can read the order
back without a Windows machine. `MainWindow` keeps the entries that exist:
`QFileInfo` for an absolute path, `QStandardPaths::findExecutable` for a bare name.

`manualBrowserLaunch` takes that filtered list, the index path, and the profile
directory, and returns the program and its arguments. The program is the first
entry. An empty list returns an empty program.

The arguments are:

- `--user-data-dir=<profile>`
- `--app=<file URL of the index>`
- `--no-first-run`
- `--disable-extensions`
- `--window-position=<x>,<y>` and `--window-size=<w>,<h>`, from
  `manualWindowRect` of the screen the application is on. A large screen
  stays at 1200×800. A shorter screen shrinks so the window is 32px inside
  `availableGeometry` on every side. Those two flags also clear a restored
  maximized window, which is what made the manual taller than a 720px screen.

The profile is `manual-browser` under `QStandardPaths::AppDataLocation`
(`VLMS` / `VLMS`). A private profile is what keeps this window off a
Chromium or Edge the librarian already has open.

`MainWindow` resolves the index first. An empty path shows `help.notFound` and
does not look for a browser. Otherwise it searches the candidate list, builds
the command, and starts it with `QProcess::startDetached`. A failed start shows
`help.noBrowser`.

On Windows the candidates, in order, are `%ProgramFiles%\Microsoft\Edge\Application\msedge.exe`,
`%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe`, and `msedge` on
`PATH`. On Linux they are the four names above, via `QStandardPaths::findExecutable`.

The Windows installer already ships `manual/` beside the executable. The comment
in `cmake/WindowsPackaging.cmake` is updated so it names this button.

## The manual's own words

These sentences still call Help a word in the page row, and they change in all
three languages before the HTML is rebuilt:

- `docs/manual/content/{ar,en,fr}/index.md`
- `docs/manual/content/{ar,en,fr}/getting-started.md`

Every capture in which the page-row buttons or the flag cluster is visible is
taken again, so the circle is in the picture and the Help word is gone.
`python3 scripts/build_manual.py` rewrites the HTML afterwards.

## Verification

- `test_manual_location` covers the candidate order on each platform, the
  four profile arguments, the window position and size on a large screen and
  on a 720px-tall one, an empty browser list returning an empty program, and
  the file URL keeping a space in the path intact.
- `test_core_StringsParity` keeps `help.noBrowser` in all three tables. A direct
  assertion checks the three sentences above, which parity cannot see.
- The eye check on this machine showed a second click opens the index in
  another window while the page already being read stays underneath. Focusing
  an already-open page is out of scope, so that check does not leave the work
  unfinished.

## Out of scope

- Raising a tab in the librarian's ordinary browser.
- Firefox, or any browser other than the candidates listed above.
- A printable PDF of the manual.
