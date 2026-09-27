# Automatic versioning and release history — design

*2026-09-24*

A merge into `main` releases itself: the diff since the last release decides whether it is a patch, a minor or a major; the version, tag, GitHub release, installer and release history follow with no one involved.

## Decisions

| Question | Decision |
|---|---|
| What decides the level | The diff since the latest `vX.Y.Z` tag, by the signal table below. The highest level found wins; several features since the last release are still one minor. |
| Who applies it | Nobody. `main` releases automatically; `Beta` only previews. |
| Where the version lives | The latest git tag. `CMakeLists.txt` no longer carries a number. |
| Installer | The level, read from the version digits, drives the existing-installation recommendation and the silent default. A "What's new" box lists the entries since the installed version. |
| Release history | Generated, never edited. Kept in annotated tag messages; rendered as GitHub release notes, `CHANGELOG.md`, and the installer's box. English only. |

## Signals

Compared as `git diff <latest tag>..HEAD`. Any one signal is enough for its level.

| Level | Signal |
|---|---|
| Major | The `PRAGMA user_version` line in `database/schema.sql` changes. An `add_library` target is added or removed in any `CMakeLists.txt`. A `find_package` or `FetchContent_Declare` is added. A file under `libraries/*/include/` is deleted or renamed. |
| Minor | A source file (`.cpp`, `.h`) is added or deleted under `applications/` or `libraries/` outside any `test/` directory. A string key is added to or removed from `libraries/Core/src/Strings.cpp`. A file is added under `docs/superpowers/specs/`. |
| Patch | Any other change to a path outside the ignored set. |
| None | Every changed path is in the ignored set: `docs/` (except new specs, above), `CLAUDE.md`, `.github/`, `.vscode/`, `.claude/`, any `test/` directory, `scripts/release/`, and `*.md` at the root. |

A new spec counts as minor even though it lives under `docs/`; that is the one exception to the ignored set.

Each signal that fires is recorded as a reason, e.g. `new file applications/vlms/src/ui/members/BirthDateEdit.cpp` or `new string key member.dateOfBirthRequired`.

## Next version

From the latest tag `vA.B.C`: patch gives `A.B.(C+1)`, minor gives `A.(B+1).0`, major gives `(A+1).0.0`. None gives no release.

## Components

### `scripts/release/release.py`

Plain Python 3, standard library only, driving `git` through `subprocess`.

- `plan [--head <rev>] [--json]` — prints the level, the reasons, the latest tag, and the next version. Exit 0 in every case, including none. `--json` for the workflows.
- `notes <version>` — prints the entry for that version: the tag's message if the tag exists, otherwise the entry the plan would write.
- `tag [--head <rev>] [--version X.Y.Z]` — runs `plan`; if the level is not none, creates the annotated tag `vX.Y.Z` on the head with the entry as its message (`git tag -a --cleanup=verbatim`, since the default cleanup strips the `## ` heading as a comment) and prints the version. If the head already carries a `v*` tag, prints that version and creates nothing (re-runs reuse the tag). `--version` is for the backfill: the version is given, the level comes from the digits against the previous tag, and the reason line reads `released before automatic versioning`.
- `changelog` — renders every `v*` tag's message, newest first, into `CHANGELOG.md` (path given by `--out`).
- `whats-new --out <file>` — the same entries in a line format the installer's Pascal can parse (below).

Failure cases that stop with a non-zero exit and release nothing: the latest tag shares no history with the head (`git merge-base` finds nothing); a `v*` tag does not parse as `vX.Y.Z`; there is no tag at all.

The latest tag is the highest `vX.Y.Z` in the repository, compared numerically, not by date. It need not be an ancestor of the head: releases are tagged on `main`'s merge commits, which `Beta` never contains, and the comparison is a tree diff (`git diff <tag> <head>`) plus `git log <tag>..<head>`, neither of which needs ancestry.

A lightweight tag (only `v0.1.0`) has no message; its entry is rendered as `## 0.1.0 — <commit date> — initial` / `Why initial: the first release.`

### Entry format

```
## 0.4.0 — 2026-09-25 — minor
Why minor: new file applications/vlms/src/ui/members/BirthDateEdit.cpp; new string key member.dateOfBirthRequired
- Pick the date of birth from three compact day, month and year boxes.
- Show the birth-date boxes in the manual.
```

The bullet lines are the subjects of commits since the last tag, oldest first, merge commits excluded, and commits whose paths are all in the ignored set excluded. The date is the tagged commit's committer date in UTC. The reason line lists at most eight reasons, then `and N more`.

### Version in CMake

The top-level `CMakeLists.txt` runs, before `project()`:

`git describe --tags --match "v[0-9]*.[0-9]*.[0-9]*" --abbrev=0`

and strips the `v`. `project(VLMS VERSION <that> LANGUAGES CXX)`. With no git or no tag, it falls back to `0.0.0` and `VLMS_VERSION` becomes `0.0.0-dev`. When `HEAD` is past the tag, `VLMS_VERSION` is `<tag>+<commits>.<short sha>` so a dev build never passes for a release; `project()` still gets the bare numbers. The installer build runs on a tagged commit, so it gets the bare version.

The logic lives in `cmake/GitVersion.cmake`.

### Workflows

- **`ci.yml`** (every push and pull request): `actions/checkout` gets `fetch-depth: 0` so tags exist. A new step runs `scripts/release/release.py plan` and writes the result to `$GITHUB_STEP_SUMMARY` under "Next release". It never fails the run.
- **`release.yml`** (new; push to `main`, and `workflow_dispatch`): concurrency group `release`, not cancelled in progress. Steps: checkout with full history; `release.py tag`; if a version came back, push the tag, create or update the GitHub release `vX.Y.Z` with `release.py notes` as its body and `CHANGELOG.md` attached; then call the installer workflow with that tag and upload the `.exe` to the release. `permissions: contents: write`.
- **`windows-installer.yml`**: loses its `push: tags` trigger; gains `workflow_call` with a `tag` input and checks that tag out with full history. `workflow_dispatch` stays for manual builds. The installer data asset is looked up on the fixed release `v0.1.0` instead of `releases/latest`, in both the workflow's cache-key step and `scripts/fetch_installer_data.ps1`.

### Installer

`scripts/build_windows_installer.ps1` reads the version from `vlms_version.txt`, which the top-level `CMakeLists.txt` writes into the build directory at configure time, instead of parsing `project(... VERSION ...)` from the file, runs `release.py whats-new` and `release.py changelog`, and hands both files to Inno Setup. `CHANGELOG.md` is installed into `{app}`; the what's-new file goes to `{tmp}` only.

`installer/vlms.iss`:

- **Level** — from the installed version and `{#MyAppVersion}`: differing major digit is major, differing minor digit is minor, otherwise patch; equal versions with this build's recorded schema are a reinstall. Treated as major regardless: the recorded `SchemaVersion` differs from `{#SchemaVersion}` or is missing, the installed version is unreadable, or the installed version is newer.
- **Page** — first line `Update from <installed> to <this>: <level>.` followed, for a patch or minor, by `The catalogue's structure is unchanged.` Keep is recommended and preselected for patch, minor and reinstall; Delete for major. The backup checkbox is unchanged.
- **What's new** — its own wizard page right after the existing-installation page (the page is too full for a memo), listing the entries whose version is greater than the installed one, newest first. No entries: no page.
- **Silent** — `/PREVIOUS` defaults to `keep` for patch, minor and reinstall and to `remove` for major. An explicit `/PREVIOUS=keep|remove` wins.

What's-new line format, one record per line, written as UTF-8 with a byte-order mark so `LoadStringsFromFile` reads it as UTF-8, and parsed with `Pos` and `Copy`:

```
V|0.4.0|2026-09-25|minor
W|new file applications/.../BirthDateEdit.cpp; new string key member.dateOfBirthRequired
-|Pick the date of birth from three compact day, month and year boxes.
```

`docs/windows-installer.md` is updated to match.

## Backfill

Annotated tags, messages generated by `release.py` with the base being the previous tag:

| Tag | Commit |
|---|---|
| `v0.2.0` | `4478a02` |
| `v0.3.0` | `f8ca450` |
| `v0.3.1` | `6d98da1` |

`v0.1.0` exists and is left alone. GitHub releases are created for the three new tags with their notes, marked not latest except `v0.3.1`; the `v0.3.1` release gets the installer built by run 35940977281. The backfill is pushed before `release.yml` exists on `main`. A tag push still runs the workflows as they were at the tagged commit, which still carry the old `push: tags` trigger in `windows-installer.yml`; the installer runs those three pushes start are force-cancelled (`gh api -X POST .../actions/runs/<id>/force-cancel`). Tags pushed later by `release.yml` use `GITHUB_TOKEN`, which starts no workflows.

## Testing

- `scripts/release/test_release.py` (`python3 -m unittest discover -s scripts/release`): each test builds a throwaway repo in a temporary directory with the needed files, commits, and tags, then asserts `plan`'s level and reasons. Covered: each major, minor and patch signal; the none case; a new spec under `docs/` being minor; test-only changes being none; highest level wins; next-version arithmetic from each level; a re-run on a tagged `HEAD` reusing the tag; a tag that shares no history failing; a tag on a commit that is not an ancestor (as on `Beta`) still planning; entry and what's-new formats; ignored-only commits left out of the bullets.
- `cmake/GitVersion.cmake`: a CTest script test configures a tiny project in three temporary repos (tag on `HEAD`, commits past the tag, no git) and checks the resulting versions.
- CI runs the Python tests in the existing default job.
- The Pascal is exercised by the installer build in `release.yml`; the build script fails if the what's-new file has a line that is not `V|`, `W|` or `-|`.

## Out of scope

- Translating the release history into Arabic or French, or showing it inside the application.
- Signing tags or installers.
- Merging `main` back into `Beta` (nothing is committed on `main` by the release, so there is nothing to merge).
