#!/usr/bin/env python3
"""Rebuild the demo loans for a VLMS database.

Importing the catalog replaces every book_copies row, so any loans that were
there before point at copies that no longer exist -- import_catalog
deletes them for exactly that reason. This puts a comparable set back.

Copies are drawn from books that have a cover image, so the circulation list
and the loan cards show real artwork instead of the placeholder. Members and
employees are used as they are; nothing here creates or edits either.

The mix is deliberate rather than uniform: some loans open, some returned,
some overdue, spread over the past eight months so the metrics page has a
shape to draw. The RNG is seeded, so two runs on the same catalog produce the
same loans.

A member is active while active_until is today or later (MemberSql::isActive).
After that day they are non_active and cannot take out a new book, so every
loan here has borrowed_at on or before that member's active_until. An inactive
member still keeps both of the situations the desk actually sees: some loans
still out (open when the due date has not passed, overdue when it has) because
the book was not returned after the membership ended, and some loans returned.
"""

from __future__ import annotations

import argparse
import random
import sqlite3
from dataclasses import dataclass
from datetime import date, timedelta
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DB = ROOT / "database" / "vlms.db"

# Same open/overdue proportions as the first demo set (45 / 27 / 11),
# doubled so the circulation list and the metrics page have more to show
# once the extra members from seed_mock_members.py are in the database.
DEFAULT_TOTAL = 90
DEFAULT_OPEN = 54
DEFAULT_OVERDUE = 22
HISTORY_DAYS = 240
LOAN_DAYS = (14, 21, 30)

ARABIC_NOTES = [
    "غلاف متضرر قليلاً عند الاستلام",
    "تم التمديد مرة واحدة.",
    "القارئ طالب، بطاقة سارية إلى نهاية السنة الدراسية.",
    "نسخة مطلوبة من قارئ آخر؛ يرجى التذكير قبل الأجل.",
    "صفحات مفكوكة في آخر الكتاب، سُجّل ذلك عند الإعارة.",
]

FRENCH_NOTES = [
    "Prêt renouvelé une fois.",
    "Exemplaire remis en main propre après vérification de la carte.",
    "Le lecteur a signalé une pliure sur le coin supérieur droit avant l'emprunt.",
    "Ouvrage réservé par un autre lecteur : rappeler avant l'échéance.",
    "Retour anticipé souhaité, l'exemplaire est demandé pour un atelier.",
]


def fetch_scalar(conn: sqlite3.Connection, sql: str) -> int:
    return conn.execute(sql).fetchone()[0]


def candidate_copies(conn: sqlite3.Connection) -> list[int]:
    """Copy ids belonging to books that have a cover, one copy per book.

    One per book on purpose: two loans of the same title would show the same
    artwork twice in a list that is meant to demonstrate variety.
    """
    rows = conn.execute(
        """
        SELECT MIN(bc.id)
        FROM book_copies bc
        JOIN books b ON b.id = bc.book_id
        WHERE b.cover_image_path IS NOT NULL
          AND TRIM(b.cover_image_path) != ''
          AND LENGTH(TRIM(b.title)) > 0
        GROUP BY bc.book_id
        ORDER BY bc.book_id
        """
    ).fetchall()
    return [row[0] for row in rows]


def clear_loans(conn: sqlite3.Connection) -> int:
    removed = fetch_scalar(conn, "SELECT COUNT(*) FROM loans")
    conn.execute("DELETE FROM loans")
    conn.commit()
    return removed


@dataclass(frozen=True)
class Member:
    id: int
    active_until: date

    def active_on(self, day: date) -> bool:
        return self.active_until >= day


def loan_kind(index: int, open_count: int, overdue_count: int) -> str:
    if index < overdue_count:
        return "overdue"
    if index < open_count:
        return "open"
    return "returned"


def can_take(member: Member, kind: str, loan_days: int, today: date) -> bool:
    """True when this member could have been handed the book on a legal day.

    Active members can hold any kind. An inactive member can hold a loan only
    when checkout falls on or before active_until. An open loan (due today or
    later, not returned) needs that window to still reach a future due date.
    Overdue and returned loans can always be placed inside the old window.
    """
    if member.active_on(today):
        return True
    if kind != "open":
        return True
    earliest = today - timedelta(days=loan_days)
    return earliest <= member.active_until


def dates_for(
    member: Member,
    kind: str,
    loan_days: int,
    rng: random.Random,
    today: date,
) -> tuple[date, date, date | None]:
    """Borrow, due, and return dates. Checkout is never after active_until."""
    if member.active_on(today):
        if kind == "overdue":
            # Due somewhere in the last three weeks and still out.
            due = today - timedelta(days=rng.randint(1, 21))
            borrowed = due - timedelta(days=loan_days)
        elif kind == "open":
            # Still running: due ahead of today.
            borrowed = today - timedelta(days=rng.randint(0, loan_days - 1))
            due = borrowed + timedelta(days=loan_days)
        else:
            borrowed = today - timedelta(days=rng.randint(30, HISTORY_DAYS))
            due = borrowed + timedelta(days=loan_days)
            # Most come back on time, some a little late -- never before
            # they were borrowed, which the CHECK would refuse anyway.
            returned = borrowed + timedelta(days=rng.randint(1, loan_days + 7))
            return borrowed, due, returned
        return borrowed, due, None

    until = member.active_until
    if kind == "open":
        earliest = today - timedelta(days=loan_days)
        latest = min(until, today)
        borrowed = earliest + timedelta(days=rng.randint(0, (latest - earliest).days))
        return borrowed, borrowed + timedelta(days=loan_days), None
    if kind == "overdue":
        # Still out, but the due date has passed. Checkout is on or before
        # the last active day, which is what makes the loan legal.
        latest = min(until, today - timedelta(days=loan_days + 1))
        borrowed = latest - timedelta(days=rng.randint(0, 21))
        return borrowed, borrowed + timedelta(days=loan_days), None

    borrowed = until - timedelta(days=rng.randint(0, min(HISTORY_DAYS, 180)))
    due = borrowed + timedelta(days=loan_days)
    returned = borrowed + timedelta(days=rng.randint(1, loan_days + 7))
    if returned > today:
        returned = today
    if returned < borrowed:
        returned = borrowed
    return borrowed, due, returned


def load_members(conn: sqlite3.Connection, today: date) -> tuple[list[Member], list[Member]]:
    active: list[Member] = []
    inactive: list[Member] = []
    for member_id, active_until in conn.execute(
        """
        SELECT id, active_until FROM members
        WHERE archived_at IS NULL AND active_until IS NOT NULL
        ORDER BY id
        """
    ):
        member = Member(member_id, date.fromisoformat(active_until))
        (active if member.active_on(today) else inactive).append(member)
    return active, inactive


def seed(
    db_path: Path,
    total: int,
    open_count: int,
    overdue_count: int,
    seed_value: int,
) -> None:
    if not db_path.is_file():
        raise SystemExit(f"Database not found: {db_path}")
    if open_count > total:
        raise SystemExit("--open cannot exceed --total")
    if overdue_count > open_count:
        raise SystemExit("--overdue cannot exceed --open")

    rng = random.Random(seed_value)
    conn = sqlite3.connect(db_path, timeout=30)
    try:
        conn.execute("PRAGMA foreign_keys = ON")

        today = date.today()
        active_members, inactive_members = load_members(conn, today)
        if not active_members:
            raise SystemExit("No active members to lend to. Seed members first.")
        if inactive_members and open_count < 1:
            raise SystemExit(
                "Inactive members need an outstanding loan. Pass --open of at least 1."
            )
        if inactive_members and open_count >= total:
            raise SystemExit(
                "Inactive members need a returned loan. Pass a --total above --open."
            )
        employees = [row[0] for row in conn.execute("SELECT id FROM employees ORDER BY id")]
        if not employees:
            raise SystemExit("No employees to record the loans against.")

        copies = candidate_copies(conn)
        if len(copies) < total:
            raise SystemExit(
                f"Only {len(copies)} copies of books with covers; need {total}."
            )

        removed = clear_loans(conn)
        if removed:
            print(f"Removed {removed} existing loan(s).")

        # Drawn without replacement. The partial unique index
        # idx_loans_one_open_per_copy allows only one open loan per copy, and
        # reusing a copy for a returned loan as well would just make the demo
        # data look accidental.
        chosen = rng.sample(copies, total)
        rows = []
        gave_inactive_outstanding = False
        gave_inactive_returned = False
        members = active_members + inactive_members

        for index, copy_id in enumerate(chosen):
            kind = loan_kind(index, open_count, overdue_count)
            loan_days = rng.choice(LOAN_DAYS)

            member = None
            if inactive_members and kind != "returned" and not gave_inactive_outstanding:
                eligible = [
                    item for item in inactive_members if can_take(item, kind, loan_days, today)
                ]
                if eligible:
                    member = rng.choice(eligible)
                    gave_inactive_outstanding = True
            if member is None and inactive_members and kind == "returned" and not gave_inactive_returned:
                member = rng.choice(inactive_members)
                gave_inactive_returned = True
            if member is None:
                pool = [item for item in members if can_take(item, kind, loan_days, today)]
                if not pool:
                    pool = active_members
                member = rng.choice(pool)

            borrowed, due, returned = dates_for(member, kind, loan_days, rng, today)
            if borrowed > member.active_until:
                raise SystemExit(
                    f"Refusing to check out copy {copy_id} to member {member.id} "
                    f"on {borrowed.isoformat()}, after active_until {member.active_until.isoformat()}."
                )

            note = None
            if rng.random() < 0.65:
                note = rng.choice(ARABIC_NOTES if rng.random() < 0.6 else FRENCH_NOTES)

            rows.append(
                (
                    member.id,
                    copy_id,
                    borrowed.isoformat(),
                    due.isoformat(),
                    returned.isoformat() if returned else None,
                    rng.choice(employees),
                    rng.choice(employees) if returned else None,
                    note,
                )
            )

        if inactive_members and not gave_inactive_outstanding:
            raise SystemExit(
                "Could not place an outstanding loan on an inactive member "
                "without a checkout after active_until."
            )
        if inactive_members and not gave_inactive_returned:
            raise SystemExit("Could not place a returned loan on an inactive member.")

        conn.executemany(
            """
            INSERT INTO loans (
                member_id, book_copy_id, borrowed_at, due_at, returned_at,
                borrowed_by_employee_id, returned_by_employee_id, notes
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            rows,
        )
        conn.commit()

        open_now = fetch_scalar(
            conn,
            "SELECT COUNT(*) FROM loans WHERE returned_at IS NULL "
            "AND due_at >= date('now', 'localtime')",
        )
        overdue_now = fetch_scalar(
            conn,
            "SELECT COUNT(*) FROM loans "
            "WHERE returned_at IS NULL AND due_at < date('now', 'localtime')",
        )
        inactive_sql = (
            "FROM loans l JOIN members m ON m.id = l.member_id "
            "WHERE NOT (m.active_until IS NOT NULL "
            "AND m.active_until >= date('now', 'localtime'))"
        )
        borrowed_after = fetch_scalar(
            conn,
            "SELECT COUNT(*) " + inactive_sql + " AND (m.active_until IS NULL "
            "OR date(l.borrowed_at) > date(m.active_until))",
        )
        inactive_out = fetch_scalar(
            conn, "SELECT COUNT(*) " + inactive_sql + " AND l.returned_at IS NULL"
        )
        inactive_returned = fetch_scalar(
            conn, "SELECT COUNT(*) " + inactive_sql + " AND l.returned_at IS NOT NULL"
        )
        if borrowed_after:
            raise SystemExit(
                f"Seed left {borrowed_after} loan(s) borrowed after the member went inactive."
            )
        if inactive_members and (inactive_out < 1 or inactive_returned < 1):
            raise SystemExit(
                "Inactive members must keep both an outstanding loan and a returned one "
                f"(outstanding={inactive_out}, returned={inactive_returned})."
            )
        still_out = open_now + overdue_now
        print(
            f"Seeded {len(rows)} loan(s): {still_out} open, "
            f"{len(rows) - still_out} returned, {overdue_now} overdue."
        )
        print(
            "Inactive members: "
            f"{inactive_out} still out, {inactive_returned} returned, "
            f"{borrowed_after} borrowed after active_until."
        )
    finally:
        conn.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db", type=Path, default=DEFAULT_DB)
    parser.add_argument("--total", type=int, default=DEFAULT_TOTAL)
    parser.add_argument("--open", dest="open_count", type=int, default=DEFAULT_OPEN)
    parser.add_argument("--overdue", type=int, default=DEFAULT_OVERDUE)
    parser.add_argument("--seed", type=int, default=20260816)
    args = parser.parse_args()

    seed(args.db, args.total, args.open_count, args.overdue, args.seed)


if __name__ == "__main__":
    main()
