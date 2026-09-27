#!/usr/bin/env python3
"""Insert the extra demo members, then point photo_path and id_image_path at files.

Membership numbers are plain integers, the same sequence MemberRepository
assigns on insert: MAX(CAST(membership_number AS INTEGER)) + 1, stored as
text ("13", not "M-2026-013"). Rows that still carry an M- prefix are
rewritten to that sequence, in id order, before the extra members are
inserted. Image directories stay keyed by member id.

The original dozen members already live in database/vlms.db. This adds a
second set in the same shape: Tunisian cities and street addresses, a
registration date, and active_until one year minus a day later (a few are
already past that day, so the members list has both statuses). Some given
names, family names, and addresses are in Arabic script; the rest stay in
Latin script. Age group follows MemberRepository: youth when the member was
under 30 on the registration date.

Status is not stored. A member is active while active_until is today or
later, and non_active once that day has passed. This script does not create
loans. seed_mock_loans.py may still leave a book out with an inactive member
when borrowed_at is on or before active_until (checked out while active, not
brought back). Those members also get returned loans. A checkout after
active_until is not generated.

Images are not drawn here. Drop photo.jpg and id.jpg in
resources/members/<id>/ and run this again; rows whose files exist get
members/<id>/photo.jpg and members/<id>/id.jpg stored the way
MemberRepository::storeMemberImage stores them. A member who already has
both files is left alone.
"""

from __future__ import annotations

import argparse
import sqlite3
from datetime import date, timedelta
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DB = ROOT / "database" / "vlms.db"
RESOURCES = ROOT / "resources"

# membership_number, first, last, sex, dob, phone, address, city, email,
# occupation, notes, registered_at, active_until or None (then registered + 1y - 1d).
# Numbers continue after the original dozen (1–12).
EXTRA_MEMBERS = [
    (
        "13", "Hadi", "Chamoun", "male", "1985-04-03",
        "+961 70 214 880", "Avenue Habib Bourguiba 9, Centre Ville", "Gafsa",
        "hadi.chamoun@example.com", "Teacher", "Borrows for class",
        "2025-09-12", None,
    ),
    (
        "14", "لينا", "كرم", "female", "1994-08-19",
        "+961 03 441 902", "نهج الجمهورية 6، شنني", "قابس",
        "lina.karam@example.com", "Architect", None,
        "2026-02-11", None,
    ),
    (
        "15", "Jad", "Frem", "male", "2006-01-22",
        "+961 81 330 118", "Avenue de l'Environnement 18, Jendouba Nord", "Jendouba",
        None, "Student", "Youth membership",
        "2026-05-02", None,
    ),
    (
        "16", "Rita", "Gemayel", "female", "1972-12-02",
        "+961 76 102 447", "Avenue Abou el Kacem Chebbi 7, Centre", "Tozeur",
        "rita.gemayel@example.com", "Pharmacist", "Regular borrower",
        "2025-04-18", None,
    ),
    (
        "17", "وليد", "بركات", "male", "1991-06-30",
        "+961 71 908 221", "نهج النصر 21، حي النصر", "أريانة",
        None, "Journalist", "Reads French and Arabic",
        "2026-01-08", None,
    ),
    (
        "18", "Aya", "Najjar", "female", "2004-11-09",
        "+961 81 776 054", "Rue des Palmiers 11, Médenine", "Médenine",
        None, "Student", "Student discount",
        "2026-03-22", None,
    ),
    (
        "19", "إيلي", "معلوف", "male", "1980-02-14",
        "+961 70 665 319", "نهج محمد علي 9، وسط المدينة", "قفصة",
        None, "Engineer", "Membership ended 2025",
        "2024-06-01", "2025-05-31",
    ),
    (
        "20", "زينة", "أبو جودة", "female", "1996-05-27",
        "+961 03 228 641", "شارع الحبيب بورقيبة 3، الكورنيش", "المنستير",
        "zeina.aboujaoude@example.com", "Designer", None,
        "2025-12-09", None,
    ),
    (
        "21", "Bassam", "Itani", "male", "1964-09-16",
        "+961 01 554 773", "Rue Farhat Hached 10, Centre", "Sidi Bouzid",
        None, "Retired", "Membership ended 2025",
        "2024-08-01", "2025-07-31",
    ),
    (
        "22", "Nadine", "Sfeir", "female", "1989-03-08",
        "+961 76 419 880", "Rue du 14 Janvier 5, Centre Ville", "Kasserine",
        "nadine.sfeir@example.com", "Nurse", None,
        "2026-06-03", None,
    ),
    (
        "23", "شربل", "عون", "male", "1998-07-11",
        "+961 71 883 240", "نهج علي بلحوان 12، وسط نابل", "نابل",
        None, "Accountant", "Renewed for 2026",
        "2025-10-02", None,
    ),
    (
        "24", "Mira", "Dagher", "female", "2008-04-25",
        "+961 81 220 915", "Avenue Habib Bourguiba 22, Corniche", "Mahdia",
        None, "Student", "Youth membership",
        "2026-07-01", None,
    ),
    (
        "25", "Samir", "Tohme", "male", "1971-01-30",
        "+961 03 990 112", "Cité Ibn Khaldoun 14, Ben Arous", "Ben Arous",
        None, "Lawyer", "Membership ended 2026",
        "2025-01-15", "2026-01-14",
    ),
    (
        "26", "فرح", "غانم", "female", "1993-10-05",
        "+961 70 147 663", "شارع الحبيب بورقيبة 4، وسط المدينة", "باجة",
        "farah.ghanem@example.com", "Librarian", "Regular borrower",
        "2026-04-14", None,
    ),
    (
        "27", "Antoine", "Frem", "male", "1986-08-17",
        "+961 01 336 508", "Avenue Habib Bourguiba 8, Centre", "Le Kef",
        None, "Shopkeeper", None,
        "2025-11-20", None,
    ),
]


def add_year_minus_day(day: date) -> date:
    try:
        nxt = day.replace(year=day.year + 1)
    except ValueError:
        nxt = day.replace(year=day.year + 1, month=2, day=28)
    return nxt - timedelta(days=1)


def age_on(born: date, on: date) -> int:
    age = on.year - born.year
    if (on.month, on.day) < (born.month, born.day):
        age -= 1
    return age


def assign_plain_indexes(conn: sqlite3.Connection) -> int:
    """Rewrite M-prefixed membership numbers to 1, 2, 3… in id order.

    Already-numeric rows keep their numbers. Prefixed rows take the smallest
    free positive integers, so a later run on a migrated database is a no-op.
    Two passes avoid the UNIQUE constraint while the values move.
    """
    rows = conn.execute(
        "SELECT id, membership_number FROM members ORDER BY id"
    ).fetchall()
    prefixed = [(member_id, number) for member_id, number in rows if not str(number).isdigit()]
    if not prefixed:
        return 0

    taken = {int(number) for _, number in rows if str(number).isdigit()}
    nxt = 1
    plan: list[tuple[int, str]] = []
    for member_id, _number in prefixed:
        while nxt in taken:
            nxt += 1
        plan.append((member_id, str(nxt)))
        taken.add(nxt)
        nxt += 1

    for member_id, _number in plan:
        conn.execute(
            "UPDATE members SET membership_number = ? WHERE id = ?",
            (f"tmp-{member_id}", member_id),
        )
    for member_id, number in plan:
        conn.execute(
            "UPDATE members SET membership_number = ? WHERE id = ?",
            (number, member_id),
        )
    return len(plan)


def insert_members(conn: sqlite3.Connection) -> int:
    added = 0
    for row in EXTRA_MEMBERS:
        (
            number, first, last, sex, dob, phone, address, city, email,
            occupation, notes, registered, active_until,
        ) = row
        exists = conn.execute(
            """
            SELECT id FROM members
            WHERE membership_number = ? OR (first_name = ? AND last_name = ?)
            """,
            (number, first, last),
        ).fetchone()
        if exists:
            continue

        registered_day = date.fromisoformat(registered)
        until = active_until or add_year_minus_day(registered_day).isoformat()
        born = date.fromisoformat(dob)
        age_group = "youth" if age_on(born, registered_day) < 30 else "adult"
        full_name = f"{first} {last}"
        registered_at = f"{registered} 10:00:00"

        cur = conn.execute(
            """
            INSERT INTO members (
                membership_number, first_name, last_name, sex, date_of_birth,
                phone, address, city, email, notes, occupation, age_group,
                full_name, registered_at, updated_at, active_until
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                number, first, last, sex, dob, phone, address, city, email,
                notes, occupation, age_group, full_name, registered_at,
                registered_at, until,
            ),
        )
        conn.execute(
            """
            INSERT INTO member_status_history (
                member_id, old_status, new_status, changed_at,
                changed_by_employee_id, note
            ) VALUES (?, NULL, 'active', ?, 1, 'Mock seed: initial status')
            """,
            (cur.lastrowid, registered_at),
        )
        added += 1
    return added


def attach_images(conn: sqlite3.Connection) -> tuple[int, int]:
    photos = 0
    cards = 0
    for member_id, photo_path, id_path in conn.execute(
        "SELECT id, photo_path, id_image_path FROM members ORDER BY id"
    ):
        photo = RESOURCES / "members" / str(member_id) / "photo.jpg"
        card = RESOURCES / "members" / str(member_id) / "id.jpg"
        sets = []
        params: list[str] = []
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
        sets.append("updated_at = datetime('now', 'localtime')")
        params.append(member_id)
        conn.execute(
            f"UPDATE members SET {', '.join(sets)} WHERE id = ?",
            params,
        )
    return photos, cards


def seed(db_path: Path) -> None:
    if not db_path.is_file():
        raise SystemExit(f"Database not found: {db_path}")
    conn = sqlite3.connect(db_path, timeout=30)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        renumbered = assign_plain_indexes(conn)
        added = insert_members(conn)
        photos, cards = attach_images(conn)
        conn.commit()
        total = conn.execute("SELECT COUNT(*) FROM members").fetchone()[0]
        print(f"Renumbered {renumbered} membership number(s) to plain integers.")
        print(f"Inserted {added} member(s). Database now has {total}.")
        print(f"Linked {photos} photo(s) and {cards} ID image(s).")
    finally:
        conn.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db", type=Path, default=DEFAULT_DB)
    args = parser.parse_args()
    seed(args.db)


if __name__ == "__main__":
    main()
