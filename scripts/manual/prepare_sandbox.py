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
