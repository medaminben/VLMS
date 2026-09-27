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

import argparse
import datetime
import json
import re
import subprocess
import sys
from dataclasses import asdict, dataclass
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
