#!/usr/bin/env python3
"""Give the catalogue integer local numbers and more than one copy per some books.

KLMS_lite stores book_copies.local_id as a plain integer in a TEXT column
("1", "2", …) and global_copy_id as AR-<n> or FR-<n>. This script rewrites
the demo MOCK-<n> values into that shape, then adds extra copies so the
catalogue's copy chooser has something to list.

How many extra copies a book gets is a function of its id, so a second run
fills only what is still missing. Existing loans keep their book_copy_id.
New copies take the next free local number for their source, the same rule
as BookCopyStore::nextCopyNumber.
"""

from __future__ import annotations

import argparse
import random
import re
import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DB = ROOT / "database" / "vlms.db"
MOCK_LOCAL = re.compile(r"^MOCK-(\d+)$")


def extra_copies_for(book_id: int) -> int:
    """Copies to add on top of the book's first one.

    About 70% of titles stay at one copy, most of the rest land on 2–4,
    and a few titles get 5–9.
    """
    roll = random.Random(book_id).randrange(100)
    if roll < 70:
        return 0
    if roll < 86:
        return 1
    if roll < 94:
        return 2
    if roll < 98:
        return 3
    return random.Random(book_id + 1).randint(4, 8)


def normalize_local_ids(conn: sqlite3.Connection) -> int:
    rows = conn.execute(
        "SELECT id, source, local_id, index_code FROM book_copies WHERE local_id IS NOT NULL"
    ).fetchall()
    plan: list[tuple[str, str, str | None, int]] = []
    for copy_id, source, local_id, index_code in rows:
        match = MOCK_LOCAL.fullmatch(str(local_id))
        if not match:
            continue
        number = str(int(match.group(1)))
        prefix = "AR" if source == "arabic" else "FR"
        new_index = number if index_code and str(index_code).startswith("MOCK-") else index_code
        plan.append((number, f"{prefix}-{number}", new_index, copy_id))
    if not plan:
        return 0

    for _number, _global_id, _index_code, copy_id in plan:
        conn.execute(
            "UPDATE book_copies SET local_id = ?, global_copy_id = ? WHERE id = ?",
            (f"tmp-{copy_id}", f"tmpg-{copy_id}", copy_id),
        )
    for number, global_id, index_code, copy_id in plan:
        conn.execute(
            """
            UPDATE book_copies
            SET local_id = ?, global_copy_id = ?, index_code = ?
            WHERE id = ?
            """,
            (number, global_id, index_code, copy_id),
        )
    return len(plan)


def next_numbers(conn: sqlite3.Connection) -> dict[str, int]:
    counters: dict[str, int] = {}
    for source, in conn.execute("SELECT DISTINCT source FROM book_copies"):
        current = conn.execute(
            """
            SELECT COALESCE(MAX(CAST(local_id AS INTEGER)), 0)
            FROM book_copies
            WHERE source = ? AND local_id GLOB '[0-9]*'
            """,
            (source,),
        ).fetchone()[0]
        counters[source] = int(current)
    return counters


def add_extra_copies(conn: sqlite3.Connection) -> int:
    counters = next_numbers(conn)
    books = conn.execute(
        """
        SELECT b.id, b.language, COUNT(bc.id)
        FROM books b
        JOIN book_copies bc ON bc.book_id = b.id
        WHERE b.archived_at IS NULL
        GROUP BY b.id
        """
    ).fetchall()

    inserted = 0
    for book_id, language, have in books:
        target = 1 + extra_copies_for(book_id)
        missing = target - have
        if missing <= 0:
            continue
        source = "arabic" if language == "ar" else "foreign"
        sample = conn.execute(
            """
            SELECT classification, subject, location, inventory_status
            FROM book_copies
            WHERE book_id = ?
            ORDER BY id
            LIMIT 1
            """,
            (book_id,),
        ).fetchone()
        classification, subject, location, inventory = sample
        for _ in range(missing):
            counters[source] = counters.get(source, 0) + 1
            number = str(counters[source])
            prefix = "AR" if source == "arabic" else "FR"
            conn.execute(
                """
                INSERT INTO book_copies (
                    book_id, global_copy_id, source, local_id,
                    classification, subject, location, inventory_status,
                    index_code, notes
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    book_id,
                    f"{prefix}-{number}",
                    source,
                    number,
                    classification,
                    subject,
                    location,
                    inventory,
                    number,
                    "Mock seed: extra copy",
                ),
            )
            inserted += 1
    return inserted


def seed(db_path: Path) -> None:
    if not db_path.is_file():
        raise SystemExit(f"Database not found: {db_path}")
    conn = sqlite3.connect(db_path, timeout=30)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        rewritten = normalize_local_ids(conn)
        inserted = add_extra_copies(conn)
        conn.commit()
        print(f"Rewrote {rewritten} local number(s) to plain integers.")
        print(f"Inserted {inserted} extra cop(y/ies).")
        rows = conn.execute(
            """
            SELECT n, COUNT(*) FROM (
                SELECT COUNT(*) AS n FROM book_copies
                WHERE archived_at IS NULL
                GROUP BY book_id
            ) GROUP BY n ORDER BY n
            """
        ).fetchall()
        for copies, books in rows:
            print(f"  books with {copies} cop(y/ies): {books}")
    finally:
        conn.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db", type=Path, default=DEFAULT_DB)
    args = parser.parse_args()
    seed(args.db)


if __name__ == "__main__":
    main()
