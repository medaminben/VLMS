#!/usr/bin/env python3
"""Remove M-000013 (momo bobo) and renumber membership_number to KLMS_lite integers.

Keeps member row ids (and therefore resources/members/<id>/ paths) stable.
Deletes loans and status history for the removed member, then the image dir.
"""

from __future__ import annotations

import argparse
import shutil
import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DB = ROOT / "database" / "vlms.db"
RESOURCES = ROOT / "resources"


def remove_momo(conn: sqlite3.Connection) -> int | None:
    row = conn.execute(
        """
        SELECT id FROM members
        WHERE membership_number = 'M-000013'
           OR (lower(first_name) = 'momo' AND lower(last_name) = 'bobo')
        """
    ).fetchone()
    if not row:
        return None
    member_id = row[0]
    conn.execute("DELETE FROM loans WHERE member_id = ?", (member_id,))
    conn.execute("DELETE FROM member_status_history WHERE member_id = ?", (member_id,))
    conn.execute("DELETE FROM members WHERE id = ?", (member_id,))
    image_dir = RESOURCES / "members" / str(member_id)
    if image_dir.is_dir():
        shutil.rmtree(image_dir)
    return member_id


def renumber_members(conn: sqlite3.Connection) -> int:
    ids = [
        row[0]
        for row in conn.execute(
            "SELECT id FROM members WHERE archived_at IS NULL ORDER BY id"
        )
    ]
    # Two-phase update for UNIQUE(membership_number).
    for index, member_id in enumerate(ids, start=1):
        conn.execute(
            "UPDATE members SET membership_number = ? WHERE id = ?",
            (f"__renum_{index}", member_id),
        )
    for index, member_id in enumerate(ids, start=1):
        conn.execute(
            "UPDATE members SET membership_number = ? WHERE id = ?",
            (str(index), member_id),
        )
    return len(ids)


def migrate(db_path: Path) -> None:
    if not db_path.is_file():
        raise SystemExit(f"Database not found: {db_path}")
    conn = sqlite3.connect(db_path, timeout=30)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        removed = remove_momo(conn)
        count = renumber_members(conn)
        conn.commit()

        nums = [
            row[0]
            for row in conn.execute(
                "SELECT membership_number FROM members WHERE archived_at IS NULL ORDER BY CAST(membership_number AS INTEGER)"
            )
        ]
        non_digit = [n for n in nums if not str(n).isdigit()]
        print(f"Removed member id {removed}." if removed else "M-000013 already absent.")
        print(f"Renumbered {count} live member(s) to 1..{count}.")
        print(f"Live membership numbers: min={nums[0] if nums else None}, "
              f"max={nums[-1] if nums else None}, count={len(nums)}")
        if non_digit:
            raise SystemExit(f"Non-digit membership numbers remain: {non_digit[:10]}")
        next_num = conn.execute(
            "SELECT COALESCE(MAX(CAST(membership_number AS INTEGER)), 0) + 1 FROM members"
        ).fetchone()[0]
        print(f"Next suggestMembershipNumber would be: {next_num}")
        leftovers = list((RESOURCES / "members").glob("13")) if (RESOURCES / "members").exists() else []
        # After remove, id 13 is gone; leftover dir should be deleted. Other
        # members keep their id dirs (14..28 etc.).
        if removed == 13 and (RESOURCES / "members" / "13").exists():
            raise SystemExit("resources/members/13 still exists after removal")
        print("Image dir for removed member cleaned." if removed else "No image cleanup needed.")
    finally:
        conn.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db", type=Path, default=DEFAULT_DB)
    args = parser.parse_args()
    migrate(args.db)


if __name__ == "__main__":
    main()
