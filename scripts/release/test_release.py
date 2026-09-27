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

    def test_notes_for_a_tagged_version_are_the_tag_message(self):
        expected = "## 0.3.1 — 2026-09-24 — patch\nWhy patch: changed x\n- Second\n"
        self.assertEqual(release.notes(self.repo.root, "0.3.1"), expected)
        self.assertEqual(release.tag_entry(self.repo.root, "v0.3.1"), expected)


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

    def test_notes_cli_prints_the_entry(self):
        code, out = run_cli(self.repo, "notes", "0.3.1")
        self.assertEqual(code, 0)
        self.assertEqual(out, "## 0.3.1 — 2026-09-24 — patch\nWhy patch: changed x\n- Second\n")

    def test_tag_cli_backfills_a_given_version(self):
        self.repo.git("tag", "-d", "v0.3.1")
        code, out = run_cli(self.repo, "tag", "--version", "0.2.0")
        self.assertEqual(code, 0)
        self.assertEqual(out, "0.2.0\n")
        self.assertEqual(self.repo.git("cat-file", "-t", "v0.2.0").strip(), "tag")


if __name__ == "__main__":
    unittest.main()
