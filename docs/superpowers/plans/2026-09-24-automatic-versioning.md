# Automatic Versioning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A merge into `main` releases itself: the diff since the last `vX.Y.Z` tag decides patch, minor or major; the tag, GitHub release, installer and release history follow with no one involved.

**Architecture:** One standard-library Python tool, `scripts/release/release.py`, classifies the diff and reads and writes annotated tags, whose messages are the release history. CMake takes the version from the latest tag (`cmake/GitVersion.cmake`). A new `release.yml` cuts the tag on `main` and calls the existing installer workflow. The Inno Setup script derives the update level from the version digits and shows the entries since the installed version.

**Tech Stack:** Python 3 (stdlib, `unittest`), git, CMake 3.21 script mode, GitHub Actions, PowerShell, Inno Setup 6 Pascal Script.

**Spec:** `docs/superpowers/specs/2026-09-24-automatic-versioning-design.md`

## Global Constraints

- Levels, lowest to highest: `none`, `patch`, `minor`, `major`. Highest level found wins.
- Next version from `vA.B.C`: patch `A.B.(C+1)`, minor `A.(B+1).0`, major `(A+1).0.0`; none gives no release.
- Ignored set: `docs/` (a file *added* under `docs/superpowers/specs/` is still minor), `CLAUDE.md`, `.github/`, `.vscode/`, `.claude/`, any path with a `test` directory component, `scripts/release/`, `*.md` at the root.
- Tags are annotated, written with `git tag -a --cleanup=verbatim` (default cleanup strips `## ` lines as comments).
- Entry format, exactly:
  ```
  ## 0.4.0 — 2026-09-25 — minor
  Why minor: new file applications/vlms/src/ui/members/BirthDateEdit.cpp; new string key member.dateOfBirthRequired
  - Pick the date of birth from three compact day, month and year boxes.
  ```
  The separators in the heading are ` — ` (space, U+2014, space). Date = tagged commit's committer date, UTC. At most eight reasons, then `; and N more`.
- What's-new records: `V|<version>|<date>|<level>`, `W|<why>`, `-|<subject>`; UTF-8 with BOM.
- Lightweight tag `v0.1.0` renders as `## 0.1.0 — <date> — initial` / `Why initial: the first release.`
- Installer data zip is fetched from release `v0.1.0`, never `releases/latest`.
- Prose in British English; identifiers and Qt/CMake API names unchanged.
- Every commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Work on branch `Beta`. Never push to `main`.

## File Structure

| File | Responsibility |
|---|---|
| `scripts/release/release.py` (create) | Classify the diff, compute versions, render entries, create tags, render changelog and what's-new. CLI. |
| `scripts/release/test_release.py` (create) | Unit tests on throwaway git repos. |
| `cmake/GitVersion.cmake` (create) | `vlms_git_version()` — version from the latest tag. |
| `cmake/test/GitVersionTest.cmake` (create) | CTest script test for the above. |
| `CMakeLists.txt` (modify) | Use `vlms_git_version`, write `vlms_version.txt`, register the CMake test. |
| `applications/vlms/CMakeLists.txt:79-82` (modify) | `VLMS_VERSION` from the full git version. |
| `.github/workflows/ci.yml` (modify) | Full history, release-tool tests, "Next release" summary. |
| `installer/vlms.iss` (modify) | Update level, page text, what's-new memo, silent default. |
| `scripts/build_windows_installer.ps1` (modify) | Version from `vlms_version.txt`, generate what's-new and changelog. |
| `docs/windows-installer.md` (modify) | Describe the level-driven recommendation and the release flow. |
| `.github/workflows/windows-installer.yml` (modify) | `workflow_call` with `tag`, full history, data pinned to `v0.1.0`, no tag trigger. |
| `scripts/fetch_installer_data.ps1` (modify) | Data pinned to `v0.1.0`. |
| `.github/workflows/release.yml` (create) | Tag, release, installer, upload. |

---

### Task 1: The release plan (signals, tags, next version)

**Files:**
- Create: `scripts/release/release.py`
- Create: `scripts/release/test_release.py`

**Interfaces:**
- Produces (used by Task 2 and the workflows):
  - `class ReleaseError(Exception)`
  - `git(repo: Path, *args: str) -> str`, `git_ok(repo: Path, *args: str) -> bool`
  - `TAG_RE`, `LEVELS`
  - `parse_version(text: str) -> tuple[int, int, int]` (accepts `v1.2.3` or `1.2.3`)
  - `format_version(version: tuple[int, int, int]) -> str`
  - `version_tags(repo) -> list[tuple[tuple[int,int,int], str]]` sorted ascending
  - `latest_tag(repo, head: str, below: tuple|None = None) -> tuple[tuple[int,int,int], str]`
  - `is_ignored(path: str) -> bool`
  - `classify(repo, base: str, head: str) -> dict[str, list[str]]` with keys `major`, `minor`, `patch`
  - `next_version(version, level: str) -> tuple[int,int,int]`
  - `@dataclass Plan(level: str, reasons: list[str], base: str, current: str, next: str | None, head: str)`
  - `make_plan(repo, head: str = "HEAD") -> Plan`

- [ ] **Step 1: Write the failing tests**

Create `scripts/release/test_release.py`:

```python
"""Tests for release.py. Each test builds a throwaway git repository."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import release  # noqa: E402

BASE_FILES = {
    "database/schema.sql": "CREATE TABLE t (id INTEGER);\nPRAGMA user_version = 7;\n",
    "CMakeLists.txt": "project(VLMS)\nfind_package(SQLite3 REQUIRED)\nadd_subdirectory(libraries)\n",
    "libraries/Core/CMakeLists.txt": "add_library(vlms_core src/Strings.cpp)\n",
    "libraries/Core/include/VLMS/Core/Strings.h": "#pragma once\n",
    "libraries/Core/src/Strings.cpp": (
        "StringTable arabicStrings()\n{\n    return {\n"
        '        {"app.title", "VLMS"},\n'
        '        {"member.field.dateOfBirthHint", "YYYY-MM-DD"},\n'
        "    };\n}\n"
    ),
    "applications/vlms/src/ui/MainWindow.cpp": "// window\n",
    "docs/manual/index.md": "# Manual\n",
}


class Repo:
    def __init__(self, root: Path):
        self.root = root
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.name", "Test")
        self.git("config", "user.email", "test@example.org")
        self.git("config", "commit.gpgsign", "false")
        self.git("config", "tag.gpgsign", "false")

    def git(self, *args: str, env: dict | None = None) -> str:
        full_env = dict(os.environ, **(env or {}))
        return subprocess.run(
            ["git", "-C", str(self.root), *args],
            check=True, capture_output=True, text=True, env=full_env,
        ).stdout

    def write(self, path: str, text: str) -> None:
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")

    def remove(self, path: str) -> None:
        self.git("rm", "-q", path)

    def commit(self, message: str, date: str | None = None) -> str:
        self.git("add", "-A")
        env = {"GIT_COMMITTER_DATE": date, "GIT_AUTHOR_DATE": date} if date else None
        self.git("commit", "-q", "--allow-empty", "-m", message, env=env)
        return self.git("rev-parse", "HEAD").strip()

    def tag(self, name: str, message: str | None = None, rev: str = "HEAD") -> None:
        if message is None:
            self.git("tag", name, rev)
        else:
            self.git("tag", "-a", "--cleanup=verbatim", "-m", message, name, rev)


class ReleaseTestCase(unittest.TestCase):
    """Starts every test with v0.1.0 (lightweight) and v0.3.1 (annotated)."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.repo = Repo(Path(self._tmp.name))
        for path, text in BASE_FILES.items():
            self.repo.write(path, text)
        self.repo.commit("Initial", date="2026-07-20T12:00:00+00:00")
        self.repo.tag("v0.1.0")
        self.repo.write("applications/vlms/src/ui/MainWindow.cpp", "// window 2\n")
        self.repo.commit("Second", date="2026-09-24T12:00:00+00:00")
        self.repo.tag("v0.3.1", "## 0.3.1 — 2026-09-24 — patch\nWhy patch: changed x\n- Second\n")

    def tearDown(self):
        self._tmp.cleanup()

    def plan(self) -> "release.Plan":
        return release.make_plan(self.repo.root, "HEAD")


class PlanTests(ReleaseTestCase):
    def test_only_ignored_paths_is_none(self):
        self.repo.write("docs/manual/index.md", "# Manual 2\n")
        self.repo.write("CLAUDE.md", "log\n")
        self.repo.write(".github/workflows/ci.yml", "name: CI\n")
        self.repo.write("applications/vlms/test/src/test_x.cpp", "// t\n")
        self.repo.write("scripts/release/notes.txt", "x\n")
        self.repo.write("README.md", "# VLMS\n")
        self.repo.commit("Docs")
        plan = self.plan()
        self.assertEqual(plan.level, "none")
        self.assertIsNone(plan.next)
        self.assertEqual(plan.base, "v0.3.1")
        self.assertEqual(plan.current, "0.3.1")

    def test_editing_existing_source_is_patch(self):
        self.repo.write("applications/vlms/src/ui/MainWindow.cpp", "// fixed\n")
        self.repo.commit("Fix")
        plan = self.plan()
        self.assertEqual(plan.level, "patch")
        self.assertEqual(plan.next, "0.3.2")
        self.assertIn("changed applications/vlms/src/ui/MainWindow.cpp", plan.reasons)

    def test_schema_edit_without_version_change_is_patch(self):
        self.repo.write("database/schema.sql", "CREATE TABLE t (id INTEGER, x TEXT);\nPRAGMA user_version = 7;\n")
        self.repo.commit("Schema tweak")
        self.assertEqual(self.plan().level, "patch")

    def test_new_source_file_is_minor(self):
        self.repo.write("applications/vlms/src/ui/members/BirthDateEdit.cpp", "// new\n")
        self.repo.commit("Feature")
        plan = self.plan()
        self.assertEqual(plan.level, "minor")
        self.assertEqual(plan.next, "0.4.0")
        self.assertIn("new file applications/vlms/src/ui/members/BirthDateEdit.cpp", plan.reasons)

    def test_removed_source_file_is_minor(self):
        self.repo.remove("applications/vlms/src/ui/MainWindow.cpp")
        self.repo.commit("Remove")
        plan = self.plan()
        self.assertEqual(plan.level, "minor")
        self.assertIn("removed file applications/vlms/src/ui/MainWindow.cpp", plan.reasons)

    def test_new_test_source_is_none(self):
        self.repo.write("libraries/Core/test/src/test_new.cpp", "// t\n")
        self.repo.commit("Test")
        self.assertEqual(self.plan().level, "none")

    def test_new_string_key_is_minor(self):
        text = BASE_FILES["libraries/Core/src/Strings.cpp"].replace(
            '        {"app.title", "VLMS"},\n',
            '        {"app.title", "VLMS"},\n        {"member.dateOfBirthRequired", "Fill it in."},\n',
        )
        self.repo.write("libraries/Core/src/Strings.cpp", text)
        self.repo.commit("Key")
        plan = self.plan()
        self.assertEqual(plan.level, "minor")
        self.assertIn("new string key member.dateOfBirthRequired", plan.reasons)

    def test_removed_string_key_is_minor(self):
        text = BASE_FILES["libraries/Core/src/Strings.cpp"].replace(
            '        {"member.field.dateOfBirthHint", "YYYY-MM-DD"},\n', ""
        )
        self.repo.write("libraries/Core/src/Strings.cpp", text)
        self.repo.commit("Drop key")
        plan = self.plan()
        self.assertEqual(plan.level, "minor")
        self.assertIn("removed string key member.field.dateOfBirthHint", plan.reasons)

    def test_new_spec_is_minor_and_edited_spec_is_none(self):
        self.repo.write("docs/superpowers/specs/2026-09-25-x-design.md", "# X\n")
        self.repo.commit("Spec")
        plan = self.plan()
        self.assertEqual(plan.level, "minor")
        self.assertIn("new spec docs/superpowers/specs/2026-09-25-x-design.md", plan.reasons)
        self.repo.tag("v0.4.0", "## 0.4.0 — 2026-09-25 — minor\nWhy minor: x\n")
        self.repo.write("docs/superpowers/specs/2026-09-25-x-design.md", "# X, edited\n")
        self.repo.commit("Edit spec")
        self.assertEqual(self.plan().level, "none")

    def test_schema_version_change_is_major(self):
        self.repo.write("database/schema.sql", "CREATE TABLE t (id INTEGER);\nPRAGMA user_version = 8;\n")
        self.repo.commit("Schema 8")
        plan = self.plan()
        self.assertEqual(plan.level, "major")
        self.assertEqual(plan.next, "1.0.0")
        self.assertIn("schema user_version 7 -> 8", plan.reasons)

    def test_new_library_is_major(self):
        self.repo.write("libraries/Reports/CMakeLists.txt", "add_library(vlms_reports src/a.cpp)\n")
        self.repo.commit("Library")
        plan = self.plan()
        self.assertEqual(plan.level, "major")
        self.assertIn("new library vlms_reports", plan.reasons)

    def test_removed_library_is_major(self):
        self.repo.write("libraries/Core/CMakeLists.txt", "# gone\n")
        self.repo.commit("No library")
        plan = self.plan()
        self.assertEqual(plan.level, "major")
        self.assertIn("removed library vlms_core", plan.reasons)

    def test_test_support_library_is_not_major(self):
        self.repo.write("libraries/Core/test/CMakeLists.txt", "add_library(vlms_testsupport s.cpp)\n")
        self.repo.commit("Test support")
        self.assertEqual(self.plan().level, "none")

    def test_new_dependency_is_major(self):
        self.repo.write("cmake/Pdf.cmake", "FetchContent_Declare(pdfium GIT_REPOSITORY x)\n")
        self.repo.commit("Dependency")
        plan = self.plan()
        self.assertEqual(plan.level, "major")
        self.assertIn("new dependency pdfium", plan.reasons)

    def test_removed_public_header_is_major(self):
        self.repo.remove("libraries/Core/include/VLMS/Core/Strings.h")
        self.repo.commit("Header")
        plan = self.plan()
        self.assertEqual(plan.level, "major")
        self.assertIn("public header removed: libraries/Core/include/VLMS/Core/Strings.h", plan.reasons)

    def test_highest_level_wins_and_keeps_only_its_reasons(self):
        self.repo.write("applications/vlms/src/ui/MainWindow.cpp", "// fixed\n")
        self.repo.write("applications/vlms/src/ui/New.cpp", "// new\n")
        self.repo.write("database/schema.sql", "PRAGMA user_version = 8;\n")
        self.repo.commit("Everything")
        plan = self.plan()
        self.assertEqual(plan.level, "major")
        self.assertEqual(plan.reasons, ["schema user_version 7 -> 8"])

    def test_latest_tag_is_highest_numerically(self):
        self.repo.tag("v0.9.0", "## 0.9.0 — 2026-09-24 — minor\nWhy minor: x\n")
        self.repo.tag("v0.10.0", "## 0.10.0 — 2026-09-24 — minor\nWhy minor: x\n")
        self.assertEqual(self.plan().base, "v0.10.0")

    def test_bad_tag_fails(self):
        self.repo.tag("v1.2")
        with self.assertRaises(release.ReleaseError):
            self.plan()

    def test_tag_on_unrelated_history_fails(self):
        self.repo.git("checkout", "-q", "--orphan", "other")
        self.repo.commit("Unrelated")
        self.repo.tag("v9.0.0", "## 9.0.0 — 2026-09-24 — major\nWhy major: x\n")
        self.repo.git("checkout", "-q", "main")
        with self.assertRaises(release.ReleaseError):
            self.plan()

    def test_tag_that_is_not_an_ancestor_still_plans(self):
        # Beta never contains main's merge commit, which is where releases are tagged.
        self.repo.git("checkout", "-q", "-b", "beta")
        self.repo.git("checkout", "-q", "main")
        self.repo.write("applications/vlms/src/ui/MainWindow.cpp", "// on main\n")
        self.repo.commit("Main only")
        self.repo.tag("v0.3.2", "## 0.3.2 — 2026-09-25 — patch\nWhy patch: x\n")
        self.repo.git("checkout", "-q", "beta")
        self.repo.write("applications/vlms/src/ui/Other.cpp", "// new on beta\n")
        self.repo.commit("Beta feature")
        plan = self.plan()
        self.assertEqual(plan.base, "v0.3.2")
        self.assertEqual(plan.level, "minor")


class VersionTests(unittest.TestCase):
    def test_next_version(self):
        self.assertEqual(release.next_version((0, 3, 1), "patch"), (0, 3, 2))
        self.assertEqual(release.next_version((0, 3, 1), "minor"), (0, 4, 0))
        self.assertEqual(release.next_version((0, 3, 1), "major"), (1, 0, 0))

    def test_parse_and_format(self):
        self.assertEqual(release.parse_version("v0.10.2"), (0, 10, 2))
        self.assertEqual(release.parse_version("0.10.2"), (0, 10, 2))
        self.assertEqual(release.format_version((0, 10, 2)), "0.10.2")
        with self.assertRaises(release.ReleaseError):
            release.parse_version("1.2")

    def test_ignored_paths(self):
        for path in ("docs/a.md", "CLAUDE.md", ".github/x.yml", ".vscode/s.json", ".claude/x",
                     "libraries/Core/test/src/a.cpp", "scripts/release/release.py", "README.md"):
            self.assertTrue(release.is_ignored(path), path)
        for path in ("applications/vlms/src/a.cpp", "installer/vlms.iss", "scripts/x.py",
                     "database/schema.sql", "docs"):
            self.assertFalse(release.is_ignored(path), path)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest discover -s scripts/release -v`
Expected: `ModuleNotFoundError: No module named 'release'`.

- [ ] **Step 3: Implement the plan half of `release.py`**

Create `scripts/release/release.py`:

```python
#!/usr/bin/env python3
"""Decide, tag and describe VLMS releases from the diff since the last one.

The level is the highest signal in `git diff <latest tag> <head>`:
major  -- schema user_version, library targets, new dependencies, public headers removed;
minor  -- source files added or removed, string keys added or removed, a new spec;
patch  -- any other path outside the ignored set;
none   -- only ignored paths (docs, tests, CI, the session log).
See docs/superpowers/specs/2026-09-24-automatic-versioning-design.md.
"""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

LEVELS = ("none", "patch", "minor", "major")
TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
SCHEMA_PATH = "database/schema.sql"
STRINGS_PATH = "libraries/Core/src/Strings.cpp"
SPECS_DIR = "docs/superpowers/specs/"
IGNORED_PREFIXES = ("docs/", ".github/", ".vscode/", ".claude/", "scripts/release/")
PUBLIC_HEADER_RE = re.compile(r"^libraries/[^/]+/include/")
USER_VERSION_RE = re.compile(r"^\s*PRAGMA\s+user_version\s*=\s*(\d+)", re.IGNORECASE | re.MULTILINE)
KEY_RE = re.compile(r'^\s*\{"([A-Za-z0-9_.]+)",', re.MULTILINE)
ADD_LIBRARY_RE = re.compile(r"add_library\s*\(\s*([A-Za-z0-9_.:+-]+)")
DEPENDENCY_RE = re.compile(r"(?:find_package|FetchContent_Declare)\s*\(\s*([A-Za-z0-9_.+-]+)")


class ReleaseError(Exception):
    """Anything that must stop a release: the workflow fails and tags nothing."""


def git(repo: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-c", "core.quotePath=false", "-C", str(repo), *args],
        capture_output=True, text=True, encoding="utf-8",
    )
    if result.returncode != 0:
        raise ReleaseError(f"git {' '.join(args)}: {result.stderr.strip()}")
    return result.stdout


def git_ok(repo: Path, *args: str) -> bool:
    return subprocess.run(
        ["git", "-C", str(repo), *args], capture_output=True, text=True
    ).returncode == 0


def parse_version(text: str) -> tuple[int, int, int]:
    match = TAG_RE.match(text if text.startswith("v") else "v" + text)
    if not match:
        raise ReleaseError(f"not a vX.Y.Z version: {text}")
    major, minor, patch = (int(part) for part in match.groups())
    return (major, minor, patch)


def format_version(version: tuple[int, int, int]) -> str:
    return ".".join(str(part) for part in version)


def version_tags(repo: Path) -> list[tuple[tuple[int, int, int], str]]:
    tags = []
    for name in git(repo, "tag", "--list", "v*").split():
        if not TAG_RE.match(name):
            raise ReleaseError(f"tag {name} is not vX.Y.Z")
        tags.append((parse_version(name), name))
    return sorted(tags)


def latest_tag(repo: Path, head: str, below: tuple[int, int, int] | None = None):
    """The highest vX.Y.Z tag (below `below`, if given). Ancestry is not required:
    releases are tagged on main's merge commits, which Beta never contains."""
    tags = version_tags(repo)
    if below is not None:
        tags = [tag for tag in tags if tag[0] < below]
    if not tags:
        raise ReleaseError("no vX.Y.Z tag to compare against")
    version, name = tags[-1]
    if not git_ok(repo, "merge-base", name, head):
        raise ReleaseError(f"{name} shares no history with {head}")
    return version, name


def is_ignored(path: str) -> bool:
    if path.startswith(IGNORED_PREFIXES) or path == "CLAUDE.md":
        return True
    if "/" not in path and path.endswith(".md"):
        return True
    return "test" in path.split("/")[:-1]


def is_source(path: str) -> bool:
    return (path.startswith(("applications/", "libraries/"))
            and path.endswith((".cpp", ".h"))
            and not is_ignored(path))


@dataclass
class Change:
    status: str
    path: str
    old_path: str | None = None


def changes(repo: Path, base: str, head: str) -> list[Change]:
    found = []
    for line in git(repo, "diff", "--name-status", "-M", base, head).splitlines():
        parts = line.split("\t")
        status = parts[0][0]
        if status in "RC":
            found.append(Change(status, parts[2], parts[1]))
        else:
            found.append(Change(status, parts[1]))
    return found


def file_at(repo: Path, rev: str, path: str) -> str:
    if not git_ok(repo, "cat-file", "-e", f"{rev}:{path}"):
        return ""
    return git(repo, "show", f"{rev}:{path}")


def schema_version(repo: Path, rev: str) -> str | None:
    found = USER_VERSION_RE.findall(file_at(repo, rev, SCHEMA_PATH))
    return found[-1] if found else None


def cmake_names(repo: Path, rev: str, pattern: re.Pattern, lists_only: bool) -> set[str]:
    names: set[str] = set()
    for path in git(repo, "ls-tree", "-r", "--name-only", rev).splitlines():
        if is_ignored(path) or path.startswith(("third_party/", "_deps/")):
            continue
        name = path.rsplit("/", 1)[-1]
        if name == "CMakeLists.txt" or (not lists_only and name.endswith(".cmake")):
            names.update(pattern.findall(file_at(repo, rev, path)))
    return names


def string_keys(repo: Path, rev: str) -> set[str]:
    return set(KEY_RE.findall(file_at(repo, rev, STRINGS_PATH)))


def classify(repo: Path, base: str, head: str) -> dict[str, list[str]]:
    found: dict[str, list[str]] = {"major": [], "minor": [], "patch": []}

    for change in changes(repo, base, head):
        old = change.old_path or change.path
        if change.status == "D" and PUBLIC_HEADER_RE.match(old):
            found["major"].append(f"public header removed: {old}")
        elif change.status == "R" and PUBLIC_HEADER_RE.match(old):
            found["major"].append(f"public header renamed: {old} -> {change.path}")
        if change.status in "AC" and change.path.startswith(SPECS_DIR):
            found["minor"].append(f"new spec {change.path}")
        if change.status in "AC" and is_source(change.path):
            found["minor"].append(f"new file {change.path}")
        if change.status == "D" and is_source(old):
            found["minor"].append(f"removed file {old}")
        if not (is_ignored(change.path) and is_ignored(old)):
            found["patch"].append(f"changed {change.path}")

    before, after = schema_version(repo, base), schema_version(repo, head)
    if before != after:
        found["major"].append(f"schema user_version {before} -> {after}")

    libraries_before = cmake_names(repo, base, ADD_LIBRARY_RE, lists_only=True)
    libraries_after = cmake_names(repo, head, ADD_LIBRARY_RE, lists_only=True)
    found["major"] += [f"new library {name}" for name in sorted(libraries_after - libraries_before)]
    found["major"] += [f"removed library {name}" for name in sorted(libraries_before - libraries_after)]

    dependencies_before = cmake_names(repo, base, DEPENDENCY_RE, lists_only=False)
    dependencies_after = cmake_names(repo, head, DEPENDENCY_RE, lists_only=False)
    found["major"] += [f"new dependency {name}" for name in sorted(dependencies_after - dependencies_before)]

    keys_before, keys_after = string_keys(repo, base), string_keys(repo, head)
    found["minor"] += [f"new string key {key}" for key in sorted(keys_after - keys_before)]
    found["minor"] += [f"removed string key {key}" for key in sorted(keys_before - keys_after)]
    return found


def next_version(version: tuple[int, int, int], level: str) -> tuple[int, int, int]:
    major, minor, patch = version
    if level == "major":
        return (major + 1, 0, 0)
    if level == "minor":
        return (major, minor + 1, 0)
    if level == "patch":
        return (major, minor, patch + 1)
    raise ReleaseError(f"no next version for level {level}")


@dataclass
class Plan:
    level: str
    reasons: list[str]
    base: str
    current: str
    next: str | None
    head: str


def make_plan(repo: Path, head: str = "HEAD") -> Plan:
    head_sha = git(repo, "rev-parse", f"{head}^{{commit}}").strip()
    version, base = latest_tag(repo, head_sha)
    found = classify(repo, base, head_sha)
    level = next((name for name in ("major", "minor", "patch") if found[name]), "none")
    return Plan(
        level=level,
        reasons=found.get(level, []),
        base=base,
        current=format_version(version),
        next=None if level == "none" else format_version(next_version(version, level)),
        head=head_sha,
    )
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m unittest discover -s scripts/release -v`
Expected: every `PlanTests` and `VersionTests` test passes (`OK`).

- [ ] **Step 5: Run it on this repository as a smoke check**

Run: `python3 -c "import sys; sys.path.insert(0,'scripts/release'); import release; print(release.make_plan(release.Path('.'), 'HEAD'))"`
Expected: a `Plan` with `base='v0.1.0'` (the only tag so far) and `level='major'` (schema 3 → 7 since 0.1.0). No traceback.

- [ ] **Step 6: Commit**

```bash
git add scripts/release/release.py scripts/release/test_release.py
git commit -m "Decide the release level from the diff since the last tag." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Entries, tags, changelog, what's-new and the CLI

**Files:**
- Modify: `scripts/release/release.py` (append)
- Modify: `scripts/release/test_release.py` (append a test class)

**Interfaces:**
- Consumes: everything Task 1 produces.
- Produces (used by Tasks 4–7):
  - `entry_date(repo, rev) -> str` (`YYYY-MM-DD`, UTC)
  - `commit_subjects(repo, base, head) -> list[str]`
  - `summarise(reasons: list[str], limit: int = 8) -> str`
  - `render_entry(version: str, date: str, level: str, why: str, subjects: list[str]) -> str`
  - `level_between(old, new) -> str`
  - `cut_tag(repo, head: str = "HEAD", version: str | None = None) -> str | None`
  - `tag_entry(repo, name: str) -> str`
  - `notes(repo, version: str, head: str = "HEAD") -> str`
  - `changelog(repo) -> str`
  - `whats_new(repo) -> str`
  - `main(argv: list[str] | None = None) -> int`
  - CLI: `release.py [--repo DIR] plan [--head REV] [--json]`, `notes VERSION [--head REV]`, `tag [--head REV] [--version X.Y.Z]`, `changelog --out FILE`, `whats-new --out FILE`.

- [ ] **Step 1: Write the failing tests**

Append to `scripts/release/test_release.py`, before the `if __name__ == "__main__":` line:

```python
import contextlib  # noqa: E402
import io  # noqa: E402
import json  # noqa: E402


def run_cli(repo: Repo, *args: str) -> tuple[int, str]:
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = release.main(["--repo", str(repo.root), *args])
    return code, out.getvalue()


class EntryTests(ReleaseTestCase):
    def test_cut_tag_writes_an_annotated_entry(self):
        self.repo.write("applications/vlms/src/ui/MainWindow.cpp", "// fixed\n")
        self.repo.commit("Fix the window.", date="2026-09-25T23:30:00+02:00")
        self.assertEqual(release.cut_tag(self.repo.root), "0.3.2")
        self.assertEqual(self.repo.git("cat-file", "-t", "v0.3.2").strip(), "tag")
        entry = release.tag_entry(self.repo.root, "v0.3.2")
        self.assertEqual(
            entry,
            "## 0.3.2 — 2026-09-25 — patch\n"
            "Why patch: changed applications/vlms/src/ui/MainWindow.cpp\n"
            "- Fix the window.\n",
        )

    def test_cut_tag_rerun_reuses_the_tag(self):
        self.repo.write("applications/vlms/src/ui/MainWindow.cpp", "// fixed\n")
        self.repo.commit("Fix")
        self.assertEqual(release.cut_tag(self.repo.root), "0.3.2")
        self.assertEqual(release.cut_tag(self.repo.root), "0.3.2")
        self.assertEqual(self.repo.git("tag", "--list", "v*").split(), ["v0.1.0", "v0.3.1", "v0.3.2"])

    def test_cut_tag_on_none_creates_nothing(self):
        self.repo.write("docs/manual/index.md", "# changed\n")
        self.repo.commit("Docs")
        self.assertIsNone(release.cut_tag(self.repo.root))
        self.assertEqual(self.repo.git("tag", "--list", "v*").split(), ["v0.1.0", "v0.3.1"])

    def test_ignored_only_commits_are_left_out_of_the_bullets(self):
        self.repo.write("CLAUDE.md", "log\n")
        self.repo.commit("Log it.")
        self.repo.write("applications/vlms/src/ui/MainWindow.cpp", "// fixed\n")
        self.repo.commit("Fix.")
        release.cut_tag(self.repo.root)
        entry = release.tag_entry(self.repo.root, "v0.3.2")
        self.assertIn("- Fix.\n", entry)
        self.assertNotIn("Log it.", entry)

    def test_backfill_takes_the_level_from_the_digits(self):
        self.repo.git("tag", "-d", "v0.3.1")
        self.assertEqual(release.cut_tag(self.repo.root, "HEAD", version="0.2.0"), "0.2.0")
        entry = release.tag_entry(self.repo.root, "v0.2.0")
        self.assertTrue(entry.startswith("## 0.2.0 — 2026-09-24 — minor\n"), entry)
        self.assertIn("Why minor: released before automatic versioning\n", entry)
        self.assertIn("- Second\n", entry)
        self.assertEqual(release.cut_tag(self.repo.root, "HEAD", version="0.2.0"), "0.2.0")

    def test_reasons_are_capped_at_eight(self):
        self.assertEqual(release.summarise([str(i) for i in range(10)]), "0; 1; 2; 3; 4; 5; 6; 7; and 2 more")
        self.assertEqual(release.summarise(["a", "b"]), "a; b")

    def test_level_between(self):
        self.assertEqual(release.level_between((0, 3, 1), (0, 3, 2)), "patch")
        self.assertEqual(release.level_between((0, 3, 1), (0, 4, 0)), "minor")
        self.assertEqual(release.level_between((0, 3, 1), (1, 0, 0)), "major")

    def test_lightweight_tag_renders_as_initial(self):
        self.assertEqual(
            release.tag_entry(self.repo.root, "v0.1.0"),
            "## 0.1.0 — 2026-07-20 — initial\nWhy initial: the first release.\n",
        )

    def test_changelog_is_newest_first(self):
        self.assertEqual(
            release.changelog(self.repo.root),
            "# Release history\n\n"
            "## 0.3.1 — 2026-09-24 — patch\nWhy patch: changed x\n- Second\n\n"
            "## 0.1.0 — 2026-07-20 — initial\nWhy initial: the first release.\n",
        )

    def test_whats_new_records(self):
        self.assertEqual(
            release.whats_new(self.repo.root),
            "V|0.3.1|2026-09-24|patch\nW|changed x\n-|Second\n"
            "V|0.1.0|2026-07-20|initial\nW|the first release.\n",
        )

    def test_notes_for_an_untagged_version_follow_the_plan(self):
        self.repo.write("applications/vlms/src/ui/New.cpp", "// new\n")
        self.repo.commit("Add a page.", date="2026-09-26T10:00:00+00:00")
        self.assertEqual(
            release.notes(self.repo.root, "0.4.0"),
            "## 0.4.0 — 2026-09-26 — minor\nWhy minor: new file applications/vlms/src/ui/New.cpp\n- Add a page.\n",
        )


class CliTests(ReleaseTestCase):
    def test_plan_json(self):
        self.repo.write("applications/vlms/src/ui/New.cpp", "// new\n")
        self.repo.commit("Feature")
        code, out = run_cli(self.repo, "plan", "--json")
        self.assertEqual(code, 0)
        data = json.loads(out)
        self.assertEqual(data["level"], "minor")
        self.assertEqual(data["next"], "0.4.0")
        self.assertEqual(data["base"], "v0.3.1")

    def test_plan_text_for_none(self):
        code, out = run_cli(self.repo, "plan")
        self.assertEqual(code, 0)
        self.assertIn("Next release: none", out)

    def test_tag_prints_the_version_or_nothing(self):
        code, out = run_cli(self.repo, "tag")
        self.assertEqual((code, out), (0, "0.3.1\n"))  # HEAD already carries v0.3.1
        self.repo.write("docs/manual/index.md", "# changed\n")
        self.repo.commit("Docs")
        code, out = run_cli(self.repo, "tag")
        self.assertEqual((code, out), (0, ""))
        self.repo.write("applications/vlms/src/ui/MainWindow.cpp", "// fixed\n")
        self.repo.commit("Fix")
        code, out = run_cli(self.repo, "tag")
        self.assertEqual((code, out), (0, "0.3.2\n"))

    def test_files_are_written(self):
        changelog_path = self.repo.root / "out" / "CHANGELOG.md"
        whats_new_path = self.repo.root / "out" / "whats_new.txt"
        self.assertEqual(run_cli(self.repo, "changelog", "--out", str(changelog_path))[0], 0)
        self.assertEqual(run_cli(self.repo, "whats-new", "--out", str(whats_new_path))[0], 0)
        self.assertTrue(changelog_path.read_text(encoding="utf-8").startswith("# Release history\n"))
        raw = whats_new_path.read_bytes()
        self.assertTrue(raw.startswith(b"\xef\xbb\xbf"), "what's-new needs a UTF-8 BOM for Inno Setup")

    def test_errors_exit_non_zero(self):
        self.repo.tag("v1.2")
        code, _ = run_cli(self.repo, "plan")
        self.assertEqual(code, 1)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest discover -s scripts/release -v`
Expected: `EntryTests` and `CliTests` fail with `AttributeError: module 'release' has no attribute 'cut_tag'` (and similar).

- [ ] **Step 3: Append the implementation**

Add these imports to the top of `scripts/release/release.py`, beside the existing ones:

```python
import argparse
import datetime
import json
import sys
from dataclasses import asdict
```

Append to the end of `scripts/release/release.py`:

```python
ENTRY_HEAD_RE = re.compile(r"^## (\S+) — (\S+) — (\S+)$")


def entry_date(repo: Path, rev: str) -> str:
    stamp = int(git(repo, "log", "-1", "--format=%ct", rev).strip())
    return datetime.datetime.fromtimestamp(stamp, datetime.timezone.utc).strftime("%Y-%m-%d")


def commit_subjects(repo: Path, base: str, head: str) -> list[str]:
    subjects = []
    log = git(repo, "log", "--reverse", "--no-merges", "--format=%H%x09%s", f"{base}..{head}")
    for line in log.splitlines():
        sha, subject = line.split("\t", 1)
        files = [f for f in git(repo, "diff-tree", "--no-commit-id", "--name-only", "-r", "--root", sha).splitlines() if f]
        if files and all(is_ignored(path) for path in files):
            continue
        subjects.append(subject)
    return subjects


def summarise(reasons: list[str], limit: int = 8) -> str:
    shown = reasons[:limit]
    more = len(reasons) - len(shown)
    return "; ".join(shown) + (f"; and {more} more" if more else "")


def render_entry(version: str, date: str, level: str, why: str, subjects: list[str]) -> str:
    lines = [f"## {version} — {date} — {level}", f"Why {level}: {why}"]
    lines += [f"- {subject}" for subject in subjects]
    return "\n".join(lines) + "\n"


def level_between(old: tuple[int, int, int], new: tuple[int, int, int]) -> str:
    if new[0] != old[0]:
        return "major"
    if new[1] != old[1]:
        return "minor"
    return "patch"


def cut_tag(repo: Path, head: str = "HEAD", version: str | None = None) -> str | None:
    """Tag `head` with the next version and its entry; print nothing on none.

    A head that already carries a vX.Y.Z tag is a re-run: its version comes back
    and nothing new is tagged. `version` is the backfill: given, not computed."""
    head_sha = git(repo, "rev-parse", f"{head}^{{commit}}").strip()
    if version is None:
        existing = [name for name in git(repo, "tag", "--points-at", head_sha).split() if TAG_RE.match(name)]
        if existing:
            return format_version(max(parse_version(name) for name in existing))
        plan = make_plan(repo, head_sha)
        if plan.level == "none":
            return None
        target, base, level, why = parse_version(plan.next), plan.base, plan.level, summarise(plan.reasons)
    else:
        target = parse_version(version)
        if git_ok(repo, "rev-parse", "-q", "--verify", f"refs/tags/v{format_version(target)}"):
            return format_version(target)
        base_version, base = latest_tag(repo, head_sha, below=target)
        level, why = level_between(base_version, target), "released before automatic versioning"
    name = f"v{format_version(target)}"
    text = render_entry(format_version(target), entry_date(repo, head_sha), level, why,
                        commit_subjects(repo, base, head_sha))
    git(repo, "tag", "-a", "--cleanup=verbatim", "-m", text, name, head_sha)
    return format_version(target)


def tag_entry(repo: Path, name: str) -> str:
    if git(repo, "cat-file", "-t", name).strip() == "tag":
        return git(repo, "tag", "--list", "--format=%(contents)", name).rstrip("\n") + "\n"
    version = format_version(parse_version(name))
    return render_entry(version, entry_date(repo, name), "initial", "the first release.", [])


def notes(repo: Path, version: str, head: str = "HEAD") -> str:
    name = f"v{format_version(parse_version(version))}"
    if git_ok(repo, "rev-parse", "-q", "--verify", f"refs/tags/{name}"):
        return tag_entry(repo, name)
    plan = make_plan(repo, head)
    return render_entry(format_version(parse_version(version)), entry_date(repo, plan.head), plan.level,
                        summarise(plan.reasons), commit_subjects(repo, plan.base, plan.head))


def changelog(repo: Path) -> str:
    entries = [tag_entry(repo, name) for _, name in reversed(version_tags(repo))]
    return "# Release history\n\n" + "\n".join(entries)


def whats_new(repo: Path) -> str:
    records = []
    for _, name in reversed(version_tags(repo)):
        for line in tag_entry(repo, name).splitlines():
            head = ENTRY_HEAD_RE.match(line)
            if head:
                records.append(f"V|{head[1]}|{head[2]}|{head[3]}")
            elif line.startswith("Why ") and ": " in line:
                records.append("W|" + line.split(": ", 1)[1])
            elif line.startswith("- "):
                records.append("-|" + line[2:])
    return "\n".join(records) + "\n"


def print_plan(plan: Plan) -> None:
    print(f"Latest release: {plan.base}")
    if plan.level == "none":
        print(f"Next release: none (only docs, tests or CI changed since {plan.base})")
        return
    print(f"Next release: {plan.level} -> {plan.next}")
    for reason in plan.reasons:
        print(f"- {reason}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    commands = parser.add_subparsers(dest="command", required=True)
    plan_parser = commands.add_parser("plan")
    plan_parser.add_argument("--head", default="HEAD")
    plan_parser.add_argument("--json", action="store_true")
    notes_parser = commands.add_parser("notes")
    notes_parser.add_argument("version")
    notes_parser.add_argument("--head", default="HEAD")
    tag_parser = commands.add_parser("tag")
    tag_parser.add_argument("--head", default="HEAD")
    tag_parser.add_argument("--version")
    for command in ("changelog", "whats-new"):
        commands.add_parser(command).add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)

    try:
        if args.command == "plan":
            plan = make_plan(args.repo, args.head)
            if args.json:
                print(json.dumps(asdict(plan)))
            else:
                print_plan(plan)
        elif args.command == "notes":
            sys.stdout.write(notes(args.repo, args.version, args.head))
        elif args.command == "tag":
            version = cut_tag(args.repo, args.head, args.version)
            if version is not None:
                print(version)
        elif args.command == "changelog":
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(changelog(args.repo), encoding="utf-8")
        elif args.command == "whats-new":
            args.out.parent.mkdir(parents=True, exist_ok=True)
            args.out.write_text(whats_new(args.repo), encoding="utf-8-sig")
    except ReleaseError as error:
        print(f"release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m unittest discover -s scripts/release -v`
Expected: every test passes (`OK`).

- [ ] **Step 5: Smoke-check the CLI on this repository**

Run: `python3 scripts/release/release.py plan && python3 scripts/release/release.py changelog --out /tmp/claude-1000/cl.md && head -5 /tmp/claude-1000/cl.md`
Expected: `Latest release: v0.1.0`, `Next release: major -> 1.0.0` with reasons, and a changelog starting `# Release history` then `## 0.1.0 — 2026-07-20 — initial`. (The backfill in Task 7 gives the real history.)

- [ ] **Step 6: Commit**

```bash
git add scripts/release/release.py scripts/release/test_release.py
git commit -m "Write release entries into annotated tags and render the history." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The version comes from the latest tag

**Files:**
- Create: `cmake/GitVersion.cmake`
- Create: `cmake/test/GitVersionTest.cmake`
- Modify: `CMakeLists.txt:1-7` and the `enable_testing()` block
- Modify: `applications/vlms/CMakeLists.txt:79-82`

**Interfaces:**
- Produces: `vlms_git_version(<source_dir> <numeric_var> <full_var>)`; variables `VLMS_VERSION_NUMERIC` (`X.Y.Z`) and `VLMS_VERSION_FULL` (`X.Y.Z`, `X.Y.Z+N.gSHA`, or `0.0.0-dev`); file `${CMAKE_BINARY_DIR}/vlms_version.txt` holding `PROJECT_VERSION` and a newline (read by Task 5).

- [ ] **Step 1: Write the failing test**

Create `cmake/test/GitVersionTest.cmake`:

```cmake
# Run as: cmake -DGIT_VERSION_MODULE=<cmake/GitVersion.cmake> -DWORK_DIR=<dir> -P GitVersionTest.cmake
include("${GIT_VERSION_MODULE}")
find_program(GIT git REQUIRED)

function(run_git dir)
    execute_process(COMMAND "${GIT}" -C "${dir}" ${ARGN}
                    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "git ${ARGN}: ${error}")
    endif()
endfunction()

function(expect_version dir numeric_expected full_regex)
    vlms_git_version("${dir}" numeric full)
    if(NOT numeric STREQUAL numeric_expected)
        message(FATAL_ERROR "${dir}: numeric '${numeric}', expected '${numeric_expected}'")
    endif()
    if(NOT full MATCHES "${full_regex}")
        message(FATAL_ERROR "${dir}: full '${full}' does not match '${full_regex}'")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")

# No git history at all. WORK_DIR sits in the build tree, which is itself inside
# this repository, so this also proves a parent repository is not picked up.
file(MAKE_DIRECTORY "${WORK_DIR}/plain")
expect_version("${WORK_DIR}/plain" "0.0.0" "^0\\.0\\.0-dev$")

# A tag on HEAD.
set(repo "${WORK_DIR}/tagged")
file(MAKE_DIRECTORY "${repo}")
run_git("${repo}" init -q)
run_git("${repo}" -c user.name=T -c user.email=t@example.org commit -q --allow-empty -m one)
run_git("${repo}" tag v0.3.1)
expect_version("${repo}" "0.3.1" "^0\\.3\\.1$")

# Commits past the tag.
run_git("${repo}" -c user.name=T -c user.email=t@example.org commit -q --allow-empty -m two)
run_git("${repo}" -c user.name=T -c user.email=t@example.org commit -q --allow-empty -m three)
expect_version("${repo}" "0.3.1" "^0\\.3\\.1\\+2\\.g[0-9a-f]+$")

# A repository without a version tag.
set(repo "${WORK_DIR}/untagged")
file(MAKE_DIRECTORY "${repo}")
run_git("${repo}" init -q)
run_git("${repo}" -c user.name=T -c user.email=t@example.org commit -q --allow-empty -m one)
expect_version("${repo}" "0.0.0" "^0\\.0\\.0-dev$")

message(STATUS "GitVersion: all cases passed")
```

In the top-level `CMakeLists.txt`, replace:

```cmake
if(BUILD_TESTING)
    enable_testing()
endif()
```

with:

```cmake
if(BUILD_TESTING)
    enable_testing()
    add_test(NAME cmake_GitVersion
             COMMAND ${CMAKE_COMMAND}
                     -DGIT_VERSION_MODULE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/GitVersion.cmake
                     -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/git-version-test
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test/GitVersionTest.cmake)
endif()
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S . -B build >/dev/null && (cd build && ctest -R cmake_GitVersion --output-on-failure)`
Expected: FAIL, `include could not find requested file` for `GitVersion.cmake`.

- [ ] **Step 3: Implement `cmake/GitVersion.cmake`**

```cmake
# vlms_git_version(<source_dir> <numeric_var> <full_var>)
#
# The version is the latest vX.Y.Z tag; see
# docs/superpowers/specs/2026-09-24-automatic-versioning-design.md.
#   numeric -- X.Y.Z, for project(). 0.0.0 without git or a tag.
#   full    -- X.Y.Z on the tagged commit, X.Y.Z+N.gSHA past it, 0.0.0-dev
#              without git or a tag, so a dev build never passes for a release.
# <source_dir> must be the top of its own repository: a source tree unpacked
# inside some other checkout must not borrow that checkout's tags.
function(vlms_git_version source_dir numeric_var full_var)
    set(numeric "0.0.0")
    set(full "0.0.0-dev")

    find_program(VLMS_GIT_EXECUTABLE git)
    if(VLMS_GIT_EXECUTABLE)
        execute_process(
            COMMAND "${VLMS_GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --show-toplevel
            OUTPUT_VARIABLE toplevel OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE toplevel_result ERROR_QUIET)
        get_filename_component(source_real "${source_dir}" REALPATH)
        get_filename_component(toplevel_real "${toplevel}" REALPATH)
        if(toplevel_result EQUAL 0 AND source_real STREQUAL toplevel_real)
            execute_process(
                COMMAND "${VLMS_GIT_EXECUTABLE}" -C "${source_dir}" describe --tags --abbrev=0
                        --match "v[0-9]*.[0-9]*.[0-9]*"
                OUTPUT_VARIABLE tag OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE describe_result ERROR_QUIET)
            if(describe_result EQUAL 0 AND tag MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
                set(numeric "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
                set(full "${numeric}")
                execute_process(
                    COMMAND "${VLMS_GIT_EXECUTABLE}" -C "${source_dir}" rev-list --count "${tag}..HEAD"
                    OUTPUT_VARIABLE ahead OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
                if(ahead GREATER 0)
                    execute_process(
                        COMMAND "${VLMS_GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --short=7 HEAD
                        OUTPUT_VARIABLE sha OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
                    set(full "${numeric}+${ahead}.g${sha}")
                endif()
            endif()
        endif()
    endif()

    set(${numeric_var} "${numeric}" PARENT_SCOPE)
    set(${full_var} "${full}" PARENT_SCOPE)
endfunction()
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake -S . -B build >/dev/null && (cd build && ctest -R cmake_GitVersion --output-on-failure)`
Expected: `1/1 Test #...: cmake_GitVersion ... Passed`.

- [ ] **Step 5: Use it in the build**

Replace the first seven lines of the top-level `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.21)

# 0.3.1: a patch on 0.3.0 (schema 7, unchanged) for the birth-date boxes in
# the member dialog. The installer compares these numbers to recognise an
# older install, so a bump that does not happen here is an upgrade the
# installer cannot see (0.1.0 predates PRAGMA user_version).
project(VLMS VERSION 0.3.1 LANGUAGES CXX)
```

with:

```cmake
cmake_minimum_required(VERSION 3.21)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# The version is the latest vX.Y.Z tag, cut by .github/workflows/release.yml
# from the diff since the previous one. Nothing here is edited by hand.
include(GitVersion)
vlms_git_version("${CMAKE_CURRENT_SOURCE_DIR}" VLMS_VERSION_NUMERIC VLMS_VERSION_FULL)
project(VLMS VERSION ${VLMS_VERSION_NUMERIC} LANGUAGES CXX)
message(STATUS "VLMS version ${VLMS_VERSION_FULL}")

# Read by scripts/build_windows_installer.ps1: the installer's version is the
# one this configure chose, not a second copy parsed out of this file.
file(WRITE "${CMAKE_BINARY_DIR}/vlms_version.txt" "${PROJECT_VERSION}\n")
```

and delete the later, now duplicate line `list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)` (keep `include(Common)` where it is).

In `applications/vlms/CMakeLists.txt`, replace lines 79–82:

```cmake
# One version number for the application, the installer and the .exe metadata:
# the top-level project() call. Application.cpp carried its own copy of the
# string, and it was already a release behind.
target_compile_definitions(vlms_ui PRIVATE VLMS_VERSION="${PROJECT_VERSION}")
```

with:

```cmake
# The version from the latest tag (cmake/GitVersion.cmake): the bare X.Y.Z on
# a release, X.Y.Z+N.gSHA on a commit past one, so a dev build never passes
# for a release in the About text or a bug report.
target_compile_definitions(vlms_ui PRIVATE VLMS_VERSION="${VLMS_VERSION_FULL}")
```

- [ ] **Step 6: Build and run the whole suite**

Run: `cmake -S . -B build 2>&1 | grep "VLMS version" && cmake --build build -j8 2>&1 | grep -E "error" ; (cd build && QT_QPA_PLATFORM=offscreen ctest -j8 2>&1 | grep "tests passed")`
Expected: `-- VLMS version 0.1.0+N.g<sha>` (only `v0.1.0` is tagged until Task 7), no `error`, `100% tests passed`.

- [ ] **Step 7: Commit**

```bash
git add cmake/GitVersion.cmake cmake/test/GitVersionTest.cmake CMakeLists.txt applications/vlms/CMakeLists.txt
git commit -m "Take the version from the latest release tag." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: CI previews the next release

**Files:**
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: `scripts/release/release.py plan`, `scripts/release/test_release.py`.

- [ ] **Step 1: Fetch full history**

In `.github/workflows/ci.yml`, replace:

```yaml
    steps:
      - uses: actions/checkout@v4
```

with:

```yaml
    steps:
      # Full history and tags: the version comes from the latest vX.Y.Z tag,
      # and the release preview diffs against it.
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
```

- [ ] **Step 2: Add the release-tool tests and the preview**

After the `Test` step (before `Upload test output on failure`), insert:

```yaml
      - name: Release tool tests
        if: matrix.name == 'default'
        run: python3 -m unittest discover -s scripts/release -v

      # What merging this into main would release. Never fails the run.
      - name: Next release
        if: matrix.name == 'default'
        run: |
          {
            echo '### Next release'
            echo '```'
            python3 scripts/release/release.py plan 2>&1 || true
            echo '```'
          } >> "$GITHUB_STEP_SUMMARY"
```

- [ ] **Step 3: Check the YAML parses**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml')); print('ok')"`
Expected: `ok`. (If PyYAML is missing: `python3 -m pip install --user pyyaml` first.)

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "Preview the next release on every CI run." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The installer follows the level and shows what's new

**Files:**
- Modify: `installer/vlms.iss`
- Modify: `scripts/build_windows_installer.ps1`
- Modify: `docs/windows-installer.md`

**Interfaces:**
- Consumes: `vlms_version.txt` (Task 3), `release.py whats-new --out`, `release.py changelog --out` (Task 2).
- Produces: `iscc` defines `/DMyAppVersion`, `/DSchemaVersion`, `/DWhatsNewFile` (optional).

- [ ] **Step 1: Version from the configure, and the two generated files**

In `scripts/build_windows_installer.ps1`, replace the whole `function Get-ProjectVersion { ... }` with:

```powershell
function Get-ProjectVersion {
  # Written by the top-level CMakeLists.txt at configure time from the latest
  # vX.Y.Z tag (cmake/GitVersion.cmake). Throwing beats guessing: a wrong
  # version here makes the installer's update level a number nobody chose.
  $versionFile = Join-Path $BuildDir "vlms_version.txt"
  if (-not (Test-Path $versionFile)) {
    throw "No $versionFile; configure the build first (it is written by CMakeLists.txt)."
  }
  $version = (Get-Content $versionFile -Raw).Trim()
  if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Unexpected version '$version' in $versionFile."
  }
  return $version
}

function New-ReleaseHistoryFiles {
  # The installer's "What's new" box and the CHANGELOG.md installed beside
  # vlms.exe, both rendered from the annotated release tags.
  param([string]$WhatsNewPath, [string]$ChangelogPath)
  $python = Get-Command python -ErrorAction SilentlyContinue
  if (-not $python) { $python = Get-Command python3 -ErrorAction SilentlyContinue }
  if (-not $python) { throw "Python is needed to render the release history (scripts/release/release.py)." }
  $tool = Join-Path $RepoRoot "scripts\release\release.py"
  & $python.Source $tool --repo $RepoRoot whats-new --out $WhatsNewPath
  if ($LASTEXITCODE -ne 0) { throw "release.py whats-new failed." }
  & $python.Source $tool --repo $RepoRoot changelog --out $ChangelogPath
  if ($LASTEXITCODE -ne 0) { throw "release.py changelog failed." }
  foreach ($line in Get-Content $WhatsNewPath -Encoding UTF8) {
    if ($line -notmatch '^(V\|[^|]+\|[^|]+\|[^|]+|W\|.*|-\|.*)$') {
      throw "Unexpected what's-new line: '$line'"
    }
  }
}
```

Delete these three lines (the version is now read after the configure):

```powershell
$Version = Get-ProjectVersion
$SchemaVersion = Get-SchemaVersion
Write-Host "Version:   $Version"
Write-Host "Schema:    user_version $SchemaVersion"
```

and insert them, unchanged, immediately before the line `Write-Host "`n--- Inno Setup ---"`. Directly after them, insert:

```powershell
$WhatsNewFile = Join-Path $BuildDir "whats_new.txt"
New-ReleaseHistoryFiles -WhatsNewPath $WhatsNewFile -ChangelogPath (Join-Path $StageDir "CHANGELOG.md")
```

Then add the define to the `iscc` call, so it reads:

```powershell
& $Iscc `
  "/DStageDir=$stageForIss" `
  "/DMyAppVersion=$Version" `
  "/DSchemaVersion=$SchemaVersion" `
  "/DWhatsNewFile=$((Resolve-Path $WhatsNewFile).Path)" `
  $IssFile
```

If the script has an early `exit 0` for `$SkipInstaller` between staging and Inno Setup, leave it; the history files are only needed by Inno Setup.

- [ ] **Step 2: Bundle the what's-new file**

In `installer/vlms.iss`, change the `MyAppVersion` default comment block to:

```
; Passed in by build_windows_installer.ps1 from the latest vX.Y.Z tag
; (vlms_version.txt); this default only serves a hand-run iscc.
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif
```

At the end of the `[Files]` section, add:

```
#ifdef WhatsNewFile
; The release entries, read by the existing-installation page and never installed.
Source: "{#WhatsNewFile}"; DestName: "whats_new.txt"; Flags: dontcopy
#endif
```

- [ ] **Step 3: Replace the recommendation with the update level**

In the `[Code]` section, replace the whole `RemovalRecommended` function and its comment (the block that begins `{ The wizard's recommendation. A different version whose recorded schema is`) with:

```pascal
const
  LevelReinstall = 0;
  LevelPatch = 1;
  LevelMinor = 2;
  LevelMajor = 3;

{ One dotted component of a version, or -1 when it is missing or not a number. }
function VersionPart(const Version: String; Index: Integer): Integer;
var
  Rest: String;
  I, P: Integer;
begin
  Rest := Version;
  for I := 1 to Index do
  begin
    P := Pos('.', Rest);
    if P = 0 then
    begin
      Result := -1;
      Exit;
    end;
    Rest := Copy(Rest, P + 1, Length(Rest));
  end;
  P := Pos('.', Rest);
  if P > 0 then
    Rest := Copy(Rest, 1, P - 1);
  Result := StrToIntDef(Rest, -1);
end;

{ What this update is, read from the version digits. Every version is cut by
  release.py from the diff since the previous one, so the digits carry the
  level: only a major can change the database schema. Anything the digits
  cannot vouch for counts as major: an unreadable or missing version, a
  downgrade, or a recorded schema that is not this build's. }
function UpdateLevel: Integer;
begin
  if PrevVersion = '' then
    Result := LevelMajor
  else if PrevRelation = RelSame then
    Result := LevelReinstall
  else if (PrevRelation = RelNewer) or (PrevSchema <> '{#SchemaVersion}') then
    Result := LevelMajor
  else if VersionPart(PrevVersion, 0) <> VersionPart('{#MyAppVersion}', 0) then
    Result := LevelMajor
  else if VersionPart(PrevVersion, 1) <> VersionPart('{#MyAppVersion}', 1) then
    Result := LevelMinor
  else
    Result := LevelPatch;
end;

function UpdateLevelName: String;
begin
  case UpdateLevel of
    LevelPatch: Result := 'patch';
    LevelMinor: Result := 'minor';
    LevelMajor: Result := 'major';
  else
    Result := 'reinstall';
  end;
end;

{ The wizard's recommendation: a clean install only for a major. A patch or
  minor holds the library's own records in a database this build reads as it
  is, and deleting it by default would throw a live catalogue away. }
function RemovalRecommended: Boolean;
begin
  Result := UpdateLevel = LevelMajor;
end;

{ The page's first line. }
function UpdateSummary: String;
begin
  if UpdateLevel = LevelReinstall then
    Result := 'Reinstalling version {#MyAppVersion}.'
  else if PrevVersion = '' then
    Result := 'Update from an unidentified version to {#MyAppVersion}: major.'
  else
  begin
    Result := 'Update from ' + PrevVersion + ' to {#MyAppVersion}: ' + UpdateLevelName + '.';
    if (UpdateLevel = LevelPatch) or (UpdateLevel = LevelMinor) then
      Result := Result + ' The catalogue''s structure is unchanged.';
  end;
end;

{ The release entries newer than the installed version, newest first, from the
  V|/W|/-| records release.py wrote. Empty when there are none. }
function WhatsNewText: String;
var
  Lines: TArrayOfString;
  I, P: Integer;
  Include: Boolean;
  Line, Rest, Version: String;
begin
  Result := '';
#ifdef WhatsNewFile
  ExtractTemporaryFile('whats_new.txt');
  if not LoadStringsFromFile(ExpandConstant('{tmp}\whats_new.txt'), Lines) then
    Exit;
  Include := False;
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    Line := Lines[I];
    if Copy(Line, 1, 2) = 'V|' then
    begin
      Rest := Copy(Line, 3, Length(Line));
      P := Pos('|', Rest);
      Version := Copy(Rest, 1, P - 1);
      Rest := Copy(Rest, P + 1, Length(Rest));
      StringChangeEx(Rest, '|', ', ', True);
      Include := (PrevVersion = '') or (CompareVersionStrings(PrevVersion, Version) = RelOlder);
      if Include then
      begin
        if Result <> '' then
          Result := Result + #13#10;
        Result := Result + Version + ' (' + Rest + ')' + #13#10;
      end;
    end
    else if Include and (Copy(Line, 1, 2) = 'W|') then
      Result := Result + '  Why: ' + Copy(Line, 3, Length(Line)) + #13#10
    else if Include and (Copy(Line, 1, 2) = '-|') then
      Result := Result + '  - ' + Copy(Line, 3, Length(Line)) + #13#10;
  end;
#endif
end;
```

- [ ] **Step 4: Put the summary and the memo on the page**

In `CreateExistingInstallPage`:

1. Add `Warning` initialisation and two new variables. Its `var` block becomes:

```pascal
var
  Info: TNewStaticText;
  Detail: TNewStaticText;
  Warning: TNewStaticText;
  NewsLabel: TNewStaticText;
  News: TNewMemo;
  NewsText: String;
  Top: Integer;
```

2. Change the `Info.Caption` assignment to:

```pascal
  Info.Caption :=
    UpdateSummary + #13#10 +
    'Setup found ' + PrevVersionLabel + ' in' + #13#10 +
    PrevDir + #13#10 +
    PrevSchemaLabel;
```

3. Replace the tail of the procedure, from `if not RemovalPossible then` to `UpdateBackupCheckboxState;`, with:

```pascal
  if not RemovalPossible then
  begin
    RadRemove.Enabled := False;
    RadKeep.Checked := True;
    Warning := AddDescription(ExistingPage,
      'The installed copy was installed for all users, so removing it needs administrator' +
      ' rights. To delete it, close Setup and start it again with "Run as administrator".',
      Top);
    Warning.Font.Style := [fsBold];
    Warning.AdjustHeight;
    Top := Warning.Top + Warning.Height + ScaleY(12);
  end;

  NewsText := WhatsNewText;
  if (NewsText <> '') and (ExistingPage.SurfaceHeight - Top > ScaleY(60)) then
  begin
    NewsLabel := AddDescription(ExistingPage, 'What''s new since ' + PrevVersionLabel + ':', Top);
    NewsLabel.Left := 0;
    Top := NewsLabel.Top + NewsLabel.Height + ScaleY(4);
    News := TNewMemo.Create(ExistingPage);
    News.Parent := ExistingPage.Surface;
    News.Left := 0;
    News.Top := Top;
    News.Width := ExistingPage.SurfaceWidth;
    News.Height := ExistingPage.SurfaceHeight - Top;
    News.ReadOnly := True;
    News.ScrollBars := ssVertical;
    News.WordWrap := True;
    News.Text := NewsText;
  end;

  UpdateBackupCheckboxState;
```

- [ ] **Step 5: Silent installs follow the level**

Replace the silent-choice comment and function:

```pascal
{ Silent installs never see the page, so the command line stands in for it:
    /PREVIOUS=remove   delete the old version and its data (the default)
    /PREVIOUS=keep     install over it and leave the data alone
    /NOBACKUP          skip the database copy
  An interactive run ignores all three; the radio buttons win. }
function SilentChoiceIsKeep: Boolean;
begin
  Result := CompareText(ExpandConstant('{param:PREVIOUS|remove}'), 'keep') = 0;
end;
```

with:

```pascal
{ Silent installs never see the page, so the command line stands in for it:
    /PREVIOUS=remove   delete the old version and its data
    /PREVIOUS=keep     install over it and leave the data alone
    (neither)          follow the recommendation: keep for a patch, minor
                       or reinstall, remove for a major
    /NOBACKUP          skip the database copy
  An interactive run ignores all three; the radio buttons win. }
function SilentChoiceIsKeep: Boolean;
var
  Choice: String;
begin
  Choice := ExpandConstant('{param:PREVIOUS|}');
  if Choice = '' then
    Result := not RemovalRecommended
  else
    Result := CompareText(Choice, 'keep') = 0;
end;
```

`WhatsNewText` and `UpdateLevel` must be declared before `CreateExistingInstallPage` and `SilentChoiceIsKeep`: they are, because they replace `RemovalRecommended`, which already sits above both.

- [ ] **Step 6: Update the installer documentation**

In `docs/windows-installer.md`, replace the paragraph that begins `**Delete it, including its database and cover images** — recommended when` through the end of the paragraph that begins `**Keep its database and cover images** — recommended for a reinstall` (up to, not including, `Either way, the checkbox`) with:

```markdown
The page opens with what the update is, read from the version digits:
`Update from 0.3.1 to 0.4.0: minor. The catalogue's structure is unchanged.`
Every version is cut automatically from the diff since the previous one (see
*Releases* below), so the digits carry the level: a patch fixes, a minor adds,
and only a major may change the database schema.

**Keep its database and cover images** — recommended for a patch, a minor and
a reinstall of the same version. The program files are replaced and nothing
under `database\` or `resources\` is touched. The application migrates the
database on its next launch if it needs to (`Database::upgradeSchemaIfNeeded`);
if a row cannot be migrated the database is left exactly as it was and the
attempt is repeated on the following launch.

**Delete it, including its database and cover images** — recommended for a
major, and for anything the digits cannot vouch for: an installed copy with no
readable version, a downgrade, or a recorded schema that is not this build's
(0.1.0 predates the record). Setup runs the old uninstaller silently, waits for
it, deletes whatever it left in `database\` and `resources\`, and installs clean
with the catalogue this version ships with.

Under the choices, **What's new** lists the release entries between the
installed version and this one.
```

Replace the silent-install sentence `` `/PREVIOUS` defaults to `remove` whatever the schema. For a patch on the same schema, pass `/PREVIOUS=keep` to keep the library's records. `` with:

```markdown
Without `/PREVIOUS`, a silent install follows the recommendation: keep for a
patch, minor or reinstall, remove for a major.
```

Change the two example lines to use `VLMS_Setup_X.Y.Z.exe` instead of `VLMS_Setup_0.3.1.exe`.

At the end of the file, append:

```markdown
## Releases

Nobody picks a version number. A merge into `main` runs
`.github/workflows/release.yml`, which asks `scripts/release/release.py` what
changed since the latest `vX.Y.Z` tag:

| Level | When |
|---|---|
| major | the schema's `PRAGMA user_version` changes, a library target is added or removed, a dependency is added, a public header is removed or renamed |
| minor | a source file is added or removed, a string key is added or removed, a new design spec |
| patch | any other change to what ships |
| none | only docs, tests, CI or the session log changed; no release |

It then tags the merge commit, creates the GitHub release with the entry as its
notes and `CHANGELOG.md` attached, builds this installer from the tag and
attaches the `.exe`. The version the build uses comes from that tag
(`cmake/GitVersion.cmake`); a build past the latest tag reports
`X.Y.Z+N.gSHA`. Each CI run on `Beta` shows the level a merge would release
under **Next release** in its summary.

The catalogue data zip stays on release `v0.1.0`; the build fetches it from
there, not from the latest release.
```

- [ ] **Step 7: Check the script's syntax where possible**

Run: `command -v pwsh && pwsh -NoProfile -Command "[System.Management.Automation.Language.Parser]::ParseFile('scripts/build_windows_installer.ps1',[ref]\$null,[ref]\$e) | Out-Null; \$e.Count"`
Expected: `0`, or no output if `pwsh` is not installed (the Windows build in Task 6's run is then the check). Pascal cannot be compiled on Linux; Task 6's installer run compiles it.

- [ ] **Step 8: Commit**

```bash
git add installer/vlms.iss scripts/build_windows_installer.ps1 docs/windows-installer.md
git commit -m "Let the installer read the update level from the version digits." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Workflows: release on main, installer on a tag

**Files:**
- Modify: `.github/workflows/windows-installer.yml`
- Modify: `scripts/fetch_installer_data.ps1`
- Create: `.github/workflows/release.yml`

**Interfaces:**
- Consumes: `release.py tag`, `notes`, `changelog` (Task 2); the installer build (Task 5).
- Produces: reusable workflow `./.github/workflows/windows-installer.yml` with `workflow_call` input `tag` (string, optional); its artifact is still named `VLMS_Setup`.

- [ ] **Step 1: Make the installer workflow callable and pin its data**

In `.github/workflows/windows-installer.yml`, replace the `on:` block:

```yaml
on:
  workflow_dispatch:
    inputs:
      upload_to_release:
        description: Upload .exe to the latest GitHub release
        type: boolean
        default: false
  push:
    tags:
      - "v*"
```

with:

```yaml
# Releases call this from release.yml with the tag they just cut. A tag push
# no longer starts it: release.yml pushes tags with GITHUB_TOKEN, which starts
# no workflows, and a second build of the same tag would only race the first.
on:
  workflow_dispatch:
    inputs:
      upload_to_release:
        description: Upload .exe to the latest GitHub release
        type: boolean
        default: false
  workflow_call:
    inputs:
      tag:
        description: The vX.Y.Z tag to build
        type: string
        required: true
```

Replace the `Checkout` step:

```yaml
      - name: Checkout
        uses: actions/checkout@v4
```

with:

```yaml
      # Full history: the version comes from the latest vX.Y.Z tag and the
      # "What's new" box from every tag's message.
      - name: Checkout
        uses: actions/checkout@v4
        with:
          ref: ${{ inputs.tag || github.ref }}
          fetch-depth: 0
```

In the `Resolve installer data asset` step, change `releases/latest` to `releases/tags/v0.1.0` and add a comment line above `stamp=`:

```yaml
          # The data zip lives on v0.1.0 for good; releases cut by release.yml
          # carry only the installer and CHANGELOG.md.
          stamp="$(gh api "repos/${GITHUB_REPOSITORY}/releases/tags/v0.1.0" \
```

- [ ] **Step 2: Pin the data in the fetch script**

In `scripts/fetch_installer_data.ps1`, change the `param(` block to:

```powershell
param(
  [switch]$Require,
  # The release that holds the catalogue data zip. Not "latest": releases cut
  # automatically carry only the installer and CHANGELOG.md.
  [string]$DataTag = "v0.1.0"
)
```

In `Download-ReleaseAsset`, replace:

```powershell
      -Uri "https://api.github.com/repos/$RepoSlug/releases/latest" `
```

with:

```powershell
      -Uri "https://api.github.com/repos/$RepoSlug/releases/tags/$DataTag" `
```

and change `Write-Host "No latest release available for installer data."` to `Write-Host "No release $DataTag available for installer data."`. Change the error text `"No $ZipName on the latest release; installer catalog data is missing."` to `"No $ZipName on release $DataTag; installer catalog data is missing."`.

- [ ] **Step 3: Create the release workflow**

Create `.github/workflows/release.yml`:

```yaml
name: Release

# A merge into main releases itself: scripts/release/release.py decides the
# level from the diff since the latest vX.Y.Z tag, tags the merge commit with
# the entry as its message, and this workflow publishes the GitHub release and
# the installer built from that tag. Nothing is committed, so Beta never falls
# behind. See docs/superpowers/specs/2026-09-24-automatic-versioning-design.md.
on:
  push:
    branches: [main]
  workflow_dispatch:

# One release at a time, and never cancelled halfway: a cancelled run could
# leave a tag without its release.
concurrency:
  group: release
  cancel-in-progress: false

permissions:
  contents: write

jobs:
  tag:
    runs-on: ubuntu-24.04
    outputs:
      version: ${{ steps.tag.outputs.version }}
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0

      # Prints nothing when only docs, tests or CI changed. On a re-run of a
      # commit that is already tagged it prints that tag's version instead of
      # cutting a new one, so a retry never skips a number.
      - name: Cut the tag
        id: tag
        run: |
          git config user.name "github-actions[bot]"
          git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
          version="$(python3 scripts/release/release.py tag)"
          echo "version=${version}" >> "$GITHUB_OUTPUT"
          if [ -n "$version" ]; then
            git push origin "v${version}"
            echo "Releasing ${version}" >> "$GITHUB_STEP_SUMMARY"
          else
            echo "Nothing to release: only docs, tests or CI changed." >> "$GITHUB_STEP_SUMMARY"
          fi

      - name: Create the GitHub release
        if: steps.tag.outputs.version != ''
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
          VERSION: ${{ steps.tag.outputs.version }}
        run: |
          python3 scripts/release/release.py notes "$VERSION" > notes.md
          python3 scripts/release/release.py changelog --out CHANGELOG.md
          if gh release view "v${VERSION}" >/dev/null 2>&1; then
            gh release edit "v${VERSION}" --notes-file notes.md
          else
            gh release create "v${VERSION}" --verify-tag --latest \
              --title "VLMS ${VERSION}" --notes-file notes.md
          fi
          gh release upload "v${VERSION}" CHANGELOG.md --clobber

  installer:
    needs: tag
    if: needs.tag.outputs.version != ''
    uses: ./.github/workflows/windows-installer.yml
    with:
      tag: v${{ needs.tag.outputs.version }}
    secrets: inherit

  publish:
    needs: [tag, installer]
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/download-artifact@v4
        with:
          name: VLMS_Setup
          path: dist

      - name: Attach the installer
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
          GH_REPO: ${{ github.repository }}
        run: gh release upload "v${{ needs.tag.outputs.version }}" dist/VLMS_Setup_*.exe --clobber
```

- [ ] **Step 4: Check the YAML parses**

Run: `for f in .github/workflows/*.yml; do python3 -c "import yaml,sys; yaml.safe_load(open('$f')); print('ok $f')"; done`
Expected: `ok` for all three files.

- [ ] **Step 5: Commit, push, and prove the installer builds from a tag-less ref**

```bash
git add .github/workflows/windows-installer.yml .github/workflows/release.yml scripts/fetch_installer_data.ps1
git commit -m "Release from main automatically and build the installer from the tag." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin Beta
gh workflow run windows-installer.yml --ref Beta -f upload_to_release=false
```

Then watch both runs: `gh run list --limit 3` and `gh run watch <id> --exit-status` for CI and for the installer.
Expected: CI green with a **Next release** section in its summary; the installer run green (it compiles the new Pascal and runs `New-ReleaseHistoryFiles`), and its log shows `Version:   0.1.0` or later from `vlms_version.txt`. If the Pascal fails to compile, the log names the line; fix it, commit, push, and re-run before going on.

---

### Task 7: Backfill the history and log it

**Files:**
- Modify: `CLAUDE.md` (session log)
- No source changes; tags and GitHub releases.

**Interfaces:**
- Consumes: `release.py tag --head REV --version X.Y.Z`, `release.py notes X.Y.Z`.

- [ ] **Step 1: Cut the three tags locally, oldest first**

```bash
python3 scripts/release/release.py tag --head 4478a02 --version 0.2.0
python3 scripts/release/release.py tag --head f8ca450 --version 0.3.0
python3 scripts/release/release.py tag --head 6d98da1 --version 0.3.1
python3 scripts/release/release.py changelog --out /tmp/claude-1000/CHANGELOG.md && cat /tmp/claude-1000/CHANGELOG.md | head -40
```

Expected: prints `0.2.0`, `0.3.0`, `0.3.1`; the changelog lists `0.3.1 — 2026-09-24 — patch`, `0.3.0 — 2026-09-24 — minor`, `0.2.0 — … — minor`, `0.1.0 — 2026-07-20 — initial`, each (except 0.1.0) with `Why …: released before automatic versioning` and its commit subjects.

- [ ] **Step 2: Push the tags and cancel the builds they start**

```bash
git push origin v0.2.0 v0.3.0 v0.3.1
sleep 20
gh run list --workflow windows-installer.yml --event push --limit 5 --json databaseId,status,headBranch
```

For every run listed with `status` `queued` or `in_progress` whose `headBranch` is `v0.2.0`, `v0.3.0` or `v0.3.1`:
`gh api -X POST repos/medaminben/VLMS/actions/runs/<id>/force-cancel`
Expected: those runs end `cancelled`. (The tagged commits still carry the old `push: tags` trigger.)

- [ ] **Step 3: Create the three GitHub releases**

```bash
for v in 0.2.0 0.3.0; do
  python3 scripts/release/release.py notes "$v" > /tmp/claude-1000/notes-$v.md
  gh release create "v$v" --verify-tag --latest=false --title "VLMS $v" --notes-file /tmp/claude-1000/notes-$v.md
done
python3 scripts/release/release.py notes 0.3.1 > /tmp/claude-1000/notes-0.3.1.md
gh release create v0.3.1 --verify-tag --latest --title "VLMS 0.3.1" --notes-file /tmp/claude-1000/notes-0.3.1.md
gh run download 35940977281 -n VLMS_Setup -D /tmp/claude-1000/setup-0.3.1
gh release upload v0.3.1 /tmp/claude-1000/setup-0.3.1/VLMS_Setup_0.3.1.exe
python3 scripts/release/release.py changelog --out /tmp/claude-1000/CHANGELOG.md
gh release upload v0.3.1 /tmp/claude-1000/CHANGELOG.md
gh release list
```

Expected: `gh release list` shows `v0.3.1 Latest`, `v0.3.0`, `v0.2.0`, `v0.1.0`; the `v0.1.0` release still holds `VLMS_installer_data.zip` (`gh release view v0.1.0 --json assets --jq '.assets[].name'`).

- [ ] **Step 4: Confirm the preview on Beta now diffs against v0.3.1**

Run: `python3 scripts/release/release.py plan`
Expected: `Latest release: v0.3.1` and `Next release: patch -> 0.3.2` (this work changes `CMakeLists.txt`, the installer and its build script; `scripts/release/`, workflows, docs and tests are ignored).

Run: `cmake -S . -B build 2>&1 | grep "VLMS version"`
Expected: `-- VLMS version 0.3.1+N.g<sha>`.

- [ ] **Step 5: Log it and commit**

Add at the top of the session log in `CLAUDE.md` (under the `_Newest first…_` paragraph):

```markdown
- 2026-09-24 — Automatic versioning. A merge into `main` runs `release.yml`: `scripts/release/release.py` classifies the diff since the highest `vX.Y.Z` tag (major: schema `user_version`, `add_library` targets, new `find_package`/`FetchContent_Declare`, public header removed; minor: source file added/removed, string key added/removed, new spec; patch: any other shipped path; none: docs, tests, CI, CLAUDE.md, `scripts/release/`), tags the merge commit with the entry as an annotated message, creates the release, and builds the installer from the tag. CMake takes the version from the tag (`cmake/GitVersion.cmake`, dev builds `X.Y.Z+N.gSHA`); nothing is committed, so `Beta` never falls behind. The installer reads patch/minor/major from the digits: keep the data for patch and minor, clean install for major, and `/PREVIOUS` follows it. Traps: `git tag -m` strips `## ` lines as comments unless `--cleanup=verbatim`; tags sit on `main`'s merge commits, which `Beta` never contains, so the tool diffs trees and never requires ancestry; the data zip is fetched from `v0.1.0`, since the latest release no longer holds it. Backfilled `v0.2.0` (`4478a02`), `v0.3.0` (`f8ca450`), `v0.3.1` (`6d98da1`, with run 35940977281's installer). Spec: `docs/superpowers/specs/2026-09-24-automatic-versioning-design.md`.
```

Also change the Repo facts line `- Branch flow: work lands on \`Beta\`, then merges into \`main\`. Tag \`v0.1.0\` exists.` to:

```markdown
- Branch flow: work lands on `Beta`, then merges into `main`; each merge into `main` releases itself (`release.yml`), so never tag or bump a version by hand.
```

```bash
git add CLAUDE.md
git commit -m "Log automatic versioning in the session log." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push origin Beta
```

Expected: the CI run for this push is green and its summary reads `Next release: patch -> 0.3.2`.
