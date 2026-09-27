#!/usr/bin/env python3
"""Insert a small pre-2026 archive: members, books, copies, and returned loans.

Archive rows are the ones with archived_at set. These are new historical
records. The script does not archive the live 2026 demo members.

Every archived_at, registration, active_until, and loan date here is before
2026-01-01. A loan is checked out on or before that member's active_until,
and it is returned (open loans stay on the circulation page). Local numbers
continue the integer sequence used by the live catalogue.

Images are not drawn here. Portrait and membership-card files live at
resources/members/<id>/photo.jpg and id.jpg. One cover per book lives at
resources/books/<id>/cover.png. A re-run links those files on the archived
rows, the same relative paths MemberRepository and CatalogRepository store.
A row that already has a path is left alone.
"""

from __future__ import annotations

import argparse
import sqlite3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DB = ROOT / "database" / "vlms.db"
RESOURCES = ROOT / "resources"

# number, first, last, sex, dob, phone, address, city, occupation,
# registered_at, active_until, archived_at
ARCHIVE_MEMBERS = [
    (
        "28", "نادية", "الحلو", "female", "1978-02-11", "+961 03 110 204",
        "نهج البيئة 18، جندوبة الشمالية", "جندوبة", "Teacher",
        "2018-04-02 10:00:00", "2019-04-01", "2021-06-15 11:00:00",
    ),
    (
        "29", "Georges", "Abi Nader", "male", "1965-11-03", "+961 70 220 118",
        "Avenue de la République 3, Siliana", "Siliana", "Merchant",
        "2019-01-20 10:00:00", "2020-01-19", "2022-03-08 09:30:00",
    ),
    (
        "30", "يارا", "خوري", "female", "1990-07-14", "+961 76 441 903",
        "شارع أبو القاسم الشابي 7، توزر", "توزر", "Nurse",
        "2020-09-01 10:00:00", "2021-08-31", "2023-01-12 16:20:00",
    ),
    (
        "31", "Kamal", "Daher", "male", "1982-05-19", "+961 71 330 441",
        "Rue des Ksours 6, Tataouine", "Tataouine", "Engineer",
        "2021-02-14 10:00:00", "2022-02-13", "2024-05-20 10:05:00",
    ),
    (
        "32", "ليلى", "صعب", "female", "2001-12-01", "+961 81 552 017",
        "نهج النخيل 11، مدنين", "مدنين", "Student",
        "2022-03-03 10:00:00", "2023-03-02", "2024-09-18 14:40:00",
    ),
    (
        "33", "بيير", "حداد", "male", "1958-08-22", "+961 01 448 220",
        "شارع الحبيب بورقيبة 22، الكورنيش", "المهدية", "Retired",
        "2017-06-10 10:00:00", "2018-06-09", "2019-12-01 08:15:00",
    ),
]

# title, language, publication_date, archived_at, copy_count
ARCHIVE_BOOKS = [
    (
        "سجلات المكتبة: 2019", "ar", "2019", "2023-04-02 10:00:00", 1,
    ),
    (
        "Cahiers d'inventaire 2018", "fr", "2018", "2022-11-19 15:10:00", 2,
    ),
    (
        "دفتر الإعارة القديم", "ar", "2016", "2024-02-28 12:00:00", 1,
    ),
    (
        "Registre des prêts 2020", "fr", "2020", "2025-08-14 09:45:00", 1,
    ),
]

# member number, book title, borrowed_at, due_at, returned_at, archived_at, note
ARCHIVE_LOANS = [
    ("28", "سجلات المكتبة: 2019", "2018-11-02", "2018-11-16", "2018-11-20",
     "2021-06-15 11:00:00", "Mock archive loan 1"),
    ("28", "Cahiers d'inventaire 2018", "2019-01-08", "2019-01-22", "2019-01-18",
     "2021-06-15 11:00:00", "Mock archive loan 2"),
    ("29", "دفتر الإعارة القديم", "2019-05-03", "2019-05-17", "2019-05-21",
     "2022-03-08 09:30:00", "Mock archive loan 3"),
    ("29", "Registre des prêts 2020", "2019-09-12", "2019-10-03", "2019-09-30",
     "2022-03-08 09:30:00", "Mock archive loan 4"),
    ("30", "سجلات المكتبة: 2019", "2020-11-04", "2020-11-18", "2020-11-25",
     "2023-01-12 16:20:00", "Mock archive loan 5"),
    ("31", "Cahiers d'inventaire 2018", "2021-06-01", "2021-06-15", "2021-06-14",
     "2024-05-20 10:05:00", "Mock archive loan 6"),
    ("32", "دفتر الإعارة القديم", "2022-07-19", "2022-08-02", "2022-08-09",
     "2024-09-18 14:40:00", "Mock archive loan 7"),
    ("33", "Registre des prêts 2020", "2018-02-06", "2018-02-20", "2018-02-18",
     "2019-12-01 08:15:00", "Mock archive loan 8"),
]


def age_group(dob: str, registered_at: str) -> str:
    born_y, born_m, born_d = (int(part) for part in dob.split("-"))
    reg = registered_at[:10]
    reg_y, reg_m, reg_d = (int(part) for part in reg.split("-"))
    age = reg_y - born_y
    if (reg_m, reg_d) < (born_m, born_d):
        age -= 1
    return "youth" if age < 30 else "adult"


def insert_members(conn: sqlite3.Connection) -> int:
    added = 0
    for row in ARCHIVE_MEMBERS:
        (
            number, first, last, sex, dob, phone, address, city, occupation,
            registered_at, active_until, archived_at,
        ) = row
        exists = conn.execute(
            "SELECT id FROM members WHERE membership_number = ?", (number,)
        ).fetchone()
        if exists:
            continue
        cur = conn.execute(
            """
            INSERT INTO members (
                membership_number, first_name, last_name, sex, date_of_birth,
                phone, address, city, occupation, age_group, full_name,
                registered_at, updated_at, active_until, archived_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                number, first, last, sex, dob, phone, address, city, occupation,
                age_group(dob, registered_at), f"{first} {last}",
                registered_at, archived_at, active_until, archived_at,
            ),
        )
        conn.execute(
            """
            INSERT INTO member_status_history (
                member_id, old_status, new_status, changed_at,
                changed_by_employee_id, note
            ) VALUES (?, NULL, 'active', ?, 1, 'Mock archive: registered')
            """,
            (cur.lastrowid, registered_at),
        )
        added += 1
    return added


def allocate_local(conn: sqlite3.Connection, source: str, counters: dict[str, int]) -> str:
    counters[source] = counters.get(source, 0) + 1
    return str(counters[source])


def load_counters(conn: sqlite3.Connection) -> dict[str, int]:
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


def insert_books(conn: sqlite3.Connection) -> int:
    author_id = conn.execute("SELECT id FROM authors ORDER BY id LIMIT 1").fetchone()
    publisher_id = conn.execute("SELECT id FROM publishers ORDER BY id LIMIT 1").fetchone()
    category_id = conn.execute("SELECT id FROM categories ORDER BY id LIMIT 1").fetchone()
    if not author_id or not publisher_id or not category_id:
        raise SystemExit("Catalog is empty; archive books need an author, publisher, and category.")

    counters = load_counters(conn)
    added = 0
    for title, language, published, archived_at, copy_count in ARCHIVE_BOOKS:
        exists = conn.execute("SELECT id FROM books WHERE title = ?", (title,)).fetchone()
        if exists:
            continue
        cur = conn.execute(
            """
            INSERT INTO books (
                title, author_id, publisher_id, category_id, language,
                publication_date, publication_date_original,
                created_at, updated_at, archived_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                title, author_id[0], publisher_id[0], category_id[0], language,
                published, published, archived_at, archived_at, archived_at,
            ),
        )
        source = "arabic" if language == "ar" else "foreign"
        prefix = "AR" if source == "arabic" else "FR"
        for _ in range(copy_count):
            number = allocate_local(conn, source, counters)
            conn.execute(
                """
                INSERT INTO book_copies (
                    book_id, global_copy_id, source, local_id,
                    classification, location, index_code, notes, archived_at
                ) VALUES (?, ?, ?, ?, 'MOCK', 'Archive', ?, ?, ?)
                """,
                (
                    cur.lastrowid, f"{prefix}-{number}", source, number,
                    number, "Mock archive copy", archived_at,
                ),
            )
        added += 1
    return added


def insert_loans(conn: sqlite3.Connection) -> int:
    added = 0
    for number, title, borrowed, due, returned, archived_at, note in ARCHIVE_LOANS:
        if conn.execute("SELECT 1 FROM loans WHERE notes = ?", (note,)).fetchone():
            continue
        member = conn.execute(
            "SELECT id, active_until FROM members WHERE membership_number = ?",
            (number,),
        ).fetchone()
        if member is None:
            raise SystemExit(f"Archive member {number} is missing.")
        if borrowed > member[1]:
            raise SystemExit(
                f"Loan {note} is checked out on {borrowed}, after active_until {member[1]}."
            )
        copy = conn.execute(
            """
            SELECT bc.id
            FROM book_copies bc
            JOIN books b ON b.id = bc.book_id
            WHERE b.title = ? AND bc.archived_at IS NOT NULL
            ORDER BY bc.id
            LIMIT 1
            """,
            (title,),
        ).fetchone()
        if copy is None:
            raise SystemExit(f"Archive copy for {title!r} is missing.")
        conn.execute(
            """
            INSERT INTO loans (
                member_id, book_copy_id, borrowed_at, due_at, returned_at,
                borrowed_by_employee_id, returned_by_employee_id, notes, archived_at
            ) VALUES (?, ?, ?, ?, ?, 1, 1, ?, ?)
            """,
            (member[0], copy[0], borrowed, due, returned, note, archived_at),
        )
        added += 1
    return added


def attach_images(conn: sqlite3.Connection) -> tuple[int, int, int]:
    """Point archived rows at files that already sit in resources/.

    Directories are keyed by row id, matching the live catalogue. Only the
    membership numbers and titles in this script are touched, and only when
    the image column is still empty.
    """
    photos = 0
    cards = 0
    for row in ARCHIVE_MEMBERS:
        number = row[0]
        found = conn.execute(
            """
            SELECT id, photo_path, id_image_path
            FROM members
            WHERE membership_number = ?
            """,
            (number,),
        ).fetchone()
        if found is None:
            continue
        member_id, photo_path, id_path = found
        photo = RESOURCES / "members" / str(member_id) / "photo.jpg"
        card = RESOURCES / "members" / str(member_id) / "id.jpg"
        sets: list[str] = []
        params: list[str | int] = []
        if photo.is_file() and not (photo_path or "").strip():
            sets.append("photo_path = ?")
            params.append(f"members/{member_id}/photo.jpg")
            photos += 1
        if card.is_file() and not (id_path or "").strip():
            sets.append("id_image_path = ?")
            params.append(f"members/{member_id}/id.jpg")
            cards += 1
        if not sets:
            continue
        params.append(member_id)
        conn.execute(
            f"UPDATE members SET {', '.join(sets)} WHERE id = ?",
            params,
        )

    covers = 0
    for title, *_rest in ARCHIVE_BOOKS:
        found = conn.execute(
            "SELECT id, cover_image_path FROM books WHERE title = ?",
            (title,),
        ).fetchone()
        if found is None:
            continue
        book_id, cover_path = found
        cover = RESOURCES / "books" / str(book_id) / "cover.png"
        if cover.is_file() and not (cover_path or "").strip():
            conn.execute(
                "UPDATE books SET cover_image_path = ? WHERE id = ?",
                (f"books/{book_id}/cover.png", book_id),
            )
            covers += 1
    return photos, cards, covers


def seed(db_path: Path) -> None:
    if not db_path.is_file():
        raise SystemExit(f"Database not found: {db_path}")
    conn = sqlite3.connect(db_path, timeout=30)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        members = insert_members(conn)
        books = insert_books(conn)
        loans = insert_loans(conn)
        photos, cards, covers = attach_images(conn)
        conn.commit()
        print(f"Archived members inserted: {members}")
        print(f"Archived books inserted: {books}")
        print(f"Archived loans inserted: {loans}")
        print(f"Linked {photos} portrait(s), {cards} ID image(s), {covers} cover(s).")
    finally:
        conn.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db", type=Path, default=DEFAULT_DB)
    args = parser.parse_args()
    seed(args.db)


if __name__ == "__main__":
    main()
