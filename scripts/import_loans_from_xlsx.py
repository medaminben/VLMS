#!/usr/bin/env python3
"""Import إعارة من 2022 from the public-library workbook into VLMS.

Default source:

  /home/amin/Dokumente/doc/KLMS resources/واجهة المكتبة العمومية نسخة أمين بن حسين.xlsx

Default target: database/vlms.db

Matches each row to an existing member and book copy. Does not create either.

Member: membership number, then an exact unique full_name (borrower column or
a name left in the number column). A starred number such as *1231 is accepted
only when the borrower name is that member's full_name — the digits alone are
not enough.

Copy: local / central / title (2026-09-07 audit order). If those miss, a
normalized title that belongs to exactly one catalog book with exactly one
copy. The sheet has no ISBN; a title that is itself a unique ISBN is accepted
the same way. Several copies or several books with that title stay skipped.

Column H is a status, not free text:
  رجوع       → returned (returned_at = due_at; the sheet has no return date)
  في الإعارة → open
  anything else → returned, original text kept in notes

due_at is borrowed_at plus the app's 14-day loan period. في الإعارة
stays out (no returned_at); Circulation then marks it open or overdue
from that due date.

Does not apply to the live database unless --apply is passed. --apply
deletes existing loans first, then inserts the matched rows.
"""

from __future__ import annotations

import argparse
import datetime
import re
import sqlite3
import sys
import unittest
import zipfile
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TARGET_DB = ROOT / "database" / "vlms.db"
DEFAULT_XLSX = Path(
    "/home/amin/Dokumente/doc/KLMS resources/واجهة المكتبة العمومية نسخة أمين بن حسين.xlsx"
)

NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
LOANS_SHEET = "إعارة من 2022"
DEFAULT_LOAN_DAYS = 14
TITLE_OVERLAP_MIN = 0.4

STATUS_RETURNED = "رجوع"
STATUS_OPEN = "في الإعارة"
FILLERS = {"", "*", "**", "***", "****", "*****", "0", "-", ",", ",,,,,"}


def excel_serial_to_iso(value: str) -> str:
    text = (value or "").strip()
    if not text:
        return ""
    try:
        serial = float(text)
    except ValueError:
        if re.fullmatch(r"\d{4}-\d{2}-\d{2}", text):
            return text
        return ""
    base = datetime.date(1899, 12, 30)
    try:
        return (base + datetime.timedelta(days=int(serial))).isoformat()
    except OverflowError:
        return ""


def add_days(iso_date: str, days: int) -> str:
    return (
        datetime.date.fromisoformat(iso_date) + datetime.timedelta(days=days)
    ).isoformat()


def normalize_identifier(value: str) -> str:
    text = (value or "").strip()
    if text in FILLERS:
        return ""
    match = re.fullmatch(r"[A-Za-z]-?(\d+)", text)
    if match:
        return match.group(1)
    match = re.fullmatch(r"(\d+)-\d+", text)
    if match:
        return match.group(1)
    if re.fullmatch(r"\d+\.0+", text):
        return text.split(".", 1)[0]
    if re.fullmatch(r"\d+", text):
        return text
    return ""


def normalize_membership(value: str) -> str:
    text = (value or "").strip()
    if re.fullmatch(r"\d+\.0+", text):
        return text.split(".", 1)[0]
    return text


def normalize_person_name(value: str) -> str:
    text = (value or "").strip()
    text = re.sub(r"[؟?!.،,;:]+", "", text)
    return re.sub(r"\s+", " ", text).strip()


def normalize_title(value: str) -> str:
    text = (value or "").strip().lower()
    text = text.replace("\u0640", "")
    text = text.replace("أ", "ا").replace("إ", "ا").replace("آ", "ا")
    text = text.replace("ى", "ي").replace("ة", "ه")
    text = re.sub(r"[^\w\s]", " ", text, flags=re.UNICODE)
    return re.sub(r"\s+", " ", text).strip()


def compact_isbn(value: str) -> str:
    text = re.sub(r"[^0-9Xx]", "", value or "")
    if len(text) in {10, 13}:
        return text
    return ""


def looks_arabic(title: str) -> bool:
    return any("\u0600" <= char <= "\u06FF" for char in title)


def token_overlap(left: str, right: str) -> float:
    left_tokens = set(re.findall(r"\w+", (left or "").lower(), flags=re.UNICODE))
    right_tokens = set(re.findall(r"\w+", (right or "").lower(), flags=re.UNICODE))
    if not left_tokens or not right_tokens:
        return 0.0
    return len(left_tokens & right_tokens) / len(left_tokens | right_tokens)


def map_status(value: str) -> str:
    return "open" if value.strip() == STATUS_OPEN else "returned"


def notes_for_status(value: str) -> str | None:
    text = value.strip()
    if text in {STATUS_RETURNED, STATUS_OPEN, ""}:
        return None
    return text


def col_letter(ref: str) -> str:
    match = re.match(r"([A-Z]+)", ref)
    return match.group(1) if match else ref


def cell_value(cell: ET.Element, shared: list[str]) -> str:
    cell_type = cell.get("t")
    value = cell.find("m:v", NS)
    inline = cell.find("m:is", NS)
    if cell_type == "s" and value is not None and value.text is not None:
        return shared[int(value.text)]
    if cell_type == "inlineStr" and inline is not None:
        texts = [
            node.text or ""
            for node in inline.iter(
                "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}t"
            )
        ]
        return "".join(texts)
    if value is not None and value.text is not None:
        return value.text
    return ""


def load_shared_strings(archive: zipfile.ZipFile) -> list[str]:
    root = ET.fromstring(archive.read("xl/sharedStrings.xml"))
    shared: list[str] = []
    for item in root.findall("m:si", NS):
        texts = [
            node.text or ""
            for node in item.iter(
                "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}t"
            )
        ]
        shared.append("".join(texts))
    return shared


def sheet_path(archive: zipfile.ZipFile, name: str) -> str:
    workbook = ET.fromstring(archive.read("xl/workbook.xml"))
    rels = ET.fromstring(archive.read("xl/_rels/workbook.xml.rels"))
    rel_ns = {"r": "http://schemas.openxmlformats.org/package/2006/relationships"}
    targets = {rel.get("Id"): rel.get("Target") for rel in rels.findall("r:Relationship", rel_ns)}
    sheet_ns = {
        "m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main",
        "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
    }
    for sheet in workbook.findall("m:sheets/m:sheet", sheet_ns):
        if sheet.get("name") == name:
            target = targets[
                sheet.get(
                    "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id"
                )
            ]
            return "xl/" + target.lstrip("/")
    raise SystemExit(f"workbook has no sheet named {name}")


def read_loan_rows(xlsx_path: Path) -> list[dict]:
    with zipfile.ZipFile(xlsx_path) as archive:
        shared = load_shared_strings(archive)
        root = ET.fromstring(archive.read(sheet_path(archive, LOANS_SHEET)))
        rows: list[dict] = []
        for xml_row in root.findall("m:sheetData/m:row", NS):
            row_index = int(xml_row.get("r") or 0)
            if row_index < 5:
                continue
            cells = {
                col_letter(cell.get("r")): (cell_value(cell, shared) or "").strip()
                for cell in xml_row.findall("m:c", NS)
            }
            if not any(cells.get(column) for column in "ADEFGH"):
                continue
            rows.append(
                {
                    "source_row": row_index,
                    "membership_number": normalize_membership(cells.get("A", "")),
                    "borrower_name": cells.get("B", ""),
                    "title": cells.get("D", ""),
                    "local_id": normalize_identifier(cells.get("E", "")),
                    "central_id": normalize_identifier(cells.get("F", "")),
                    "borrowed_at": excel_serial_to_iso(cells.get("G", "")),
                    "status_text": cells.get("H", ""),
                }
            )
        return rows


def load_members(conn: sqlite3.Connection) -> dict[str, int]:
    return {
        number: member_id
        for member_id, number in conn.execute(
            "SELECT id, membership_number FROM members"
        )
    }


def load_member_name_indexes(
    conn: sqlite3.Connection,
) -> tuple[dict[str, list[int]], dict[int, str]]:
    by_name: dict[str, list[int]] = defaultdict(list)
    names_by_id: dict[int, str] = {}
    for member_id, full_name in conn.execute("SELECT id, full_name FROM members"):
        key = normalize_person_name(full_name or "")
        if not key:
            continue
        by_name[key].append(member_id)
        names_by_id[member_id] = key
    return by_name, names_by_id


def resolve_member(
    number: str,
    borrower_name: str,
    members: dict[str, int],
    members_by_name: dict[str, list[int]] | None = None,
    names_by_id: dict[int, str] | None = None,
) -> int | None:
    if number in members:
        return members[number]
    stripped = number.lstrip("*")
    if (
        stripped != number
        and re.fullmatch(r"\d+", stripped)
        and stripped in members
        and names_by_id is not None
    ):
        stored = names_by_id.get(members[stripped], "")
        if stored and normalize_person_name(borrower_name) == stored:
            return members[stripped]
    if members_by_name:
        for candidate in (borrower_name, number):
            key = normalize_person_name(candidate)
            hits = members_by_name.get(key, [])
            if len(hits) == 1:
                return hits[0]
    return None


def load_copy_index(conn: sqlite3.Connection) -> dict[str, list[dict]]:
    by_local: dict[str, list[dict]] = defaultdict(list)
    for copy_id, source, local_id, central_id, title in conn.execute(
        """
        SELECT bc.id, bc.source, bc.local_id, IFNULL(bc.central_id, ''), b.title
        FROM book_copies bc
        JOIN books b ON b.id = bc.book_id
        """
    ):
        by_local[str(local_id)].append(
            {
                "id": copy_id,
                "source": source,
                "local_id": str(local_id),
                "central_id": str(central_id),
                "title": title or "",
            }
        )
    return by_local


def copies_by_central(by_local: dict[str, list[dict]]) -> dict[str, list[dict]]:
    by_central: dict[str, list[dict]] = defaultdict(list)
    for copies in by_local.values():
        for copy in copies:
            if copy["central_id"]:
                by_central[copy["central_id"]].append(copy)
    return by_central


def unique_title_single_copies(by_local: dict[str, list[dict]]) -> dict[str, int]:
    by_title: dict[str, list[int]] = defaultdict(list)
    for copies in by_local.values():
        for copy in copies:
            key = normalize_title(copy["title"])
            if key:
                by_title[key].append(copy["id"])
    resolved: dict[str, int] = {}
    for key, copy_ids in by_title.items():
        unique_ids = list(dict.fromkeys(copy_ids))
        if len(unique_ids) == 1:
            resolved[key] = unique_ids[0]
    return resolved


def unique_isbn_single_copies(conn: sqlite3.Connection) -> dict[str, int]:
    by_isbn: dict[str, list[int]] = defaultdict(list)
    for copy_id, isbn in conn.execute(
        """
        SELECT bc.id, b.isbn
        FROM book_copies bc
        JOIN books b ON b.id = bc.book_id
        """
    ):
        key = compact_isbn(isbn or "")
        if key:
            by_isbn[key].append(copy_id)
    resolved: dict[str, int] = {}
    for key, copy_ids in by_isbn.items():
        unique_ids = list(dict.fromkeys(copy_ids))
        if len(unique_ids) == 1:
            resolved[key] = unique_ids[0]
    return resolved


def resolve_copy(
    local_id: str,
    central_id: str,
    title: str,
    by_local: dict[str, list[dict]],
    by_central: dict[str, list[dict]],
    title_copies: dict[str, int] | None = None,
    isbn_copies: dict[str, int] | None = None,
) -> int | None:
    if local_id and central_id:
        exact = [copy for copy in by_local.get(local_id, []) if copy["central_id"] == central_id]
        if len(exact) == 1:
            return exact[0]["id"]
    if central_id:
        central_hits = by_central.get(central_id, [])
        if len(central_hits) == 1:
            return central_hits[0]["id"]
    candidates = by_local.get(local_id, []) if local_id else []
    if len(candidates) == 1:
        return candidates[0]["id"]
    if len(candidates) > 1:
        preferred_source = "arabic" if looks_arabic(title) else "foreign"
        preferred = [copy for copy in candidates if copy["source"] == preferred_source]
        pool = preferred or candidates
        scored = sorted(
            ((token_overlap(title, copy["title"]), copy["id"]) for copy in pool),
            reverse=True,
        )
        if scored and scored[0][0] >= TITLE_OVERLAP_MIN and (
            len(scored) == 1 or scored[0][0] > scored[1][0]
        ):
            return scored[0][1]
        if len(preferred) == 1:
            return preferred[0]["id"]
        return None
    if isbn_copies:
        isbn_hit = isbn_copies.get(compact_isbn(title))
        if isbn_hit is not None:
            return isbn_hit
    if title_copies:
        return title_copies.get(normalize_title(title))
    return None


def collapse_extra_open_loans(rows: list[dict]) -> int:
    """Only the latest loan of a copy may stay open."""
    by_copy: dict[int, list[int]] = defaultdict(list)
    for index, row in enumerate(rows):
        by_copy[row["book_copy_id"]].append(index)
    collapsed = 0
    for indexes in by_copy.values():
        ordered = sorted(
            indexes, key=lambda i: (rows[i]["borrowed_at"], rows[i]["source_row"])
        )
        for index in ordered[:-1]:
            if rows[index]["returned_at"] is None:
                rows[index]["returned_at"] = rows[index]["due_at"]
                collapsed += 1
    return collapsed


def plan_import(
    rows: list[dict],
    members: dict[str, int],
    by_local: dict[str, list[dict]],
    by_central: dict[str, list[dict]] | None = None,
    members_by_name: dict[str, list[int]] | None = None,
    names_by_id: dict[int, str] | None = None,
    title_copies: dict[str, int] | None = None,
    isbn_copies: dict[str, int] | None = None,
) -> tuple[list[dict], dict[str, int]]:
    if by_central is None:
        by_central = copies_by_central(by_local)
    if title_copies is None:
        title_copies = unique_title_single_copies(by_local)
    planned: list[dict] = []
    skipped: dict[str, int] = Counter()
    for row in rows:
        if not row["borrowed_at"]:
            skipped["bad_date"] += 1
            continue
        member_id = resolve_member(
            row["membership_number"],
            row.get("borrower_name", ""),
            members,
            members_by_name,
            names_by_id,
        )
        if member_id is None:
            skipped["member_not_found"] += 1
            continue
        if row["membership_number"] not in members:
            skipped["recovered_member"] += 1
        copy_id = resolve_copy(
            row["local_id"],
            row["central_id"],
            row["title"],
            by_local,
            by_central,
        )
        used_title_fallback = copy_id is None
        if used_title_fallback:
            copy_id = resolve_copy(
                row["local_id"],
                row["central_id"],
                row["title"],
                by_local,
                by_central,
                title_copies,
                isbn_copies,
            )
        if copy_id is None:
            skipped["copy_not_found"] += 1
            continue
        if used_title_fallback:
            skipped["recovered_copy"] += 1
        due_at = add_days(row["borrowed_at"], DEFAULT_LOAN_DAYS)
        status = map_status(row["status_text"])
        planned.append(
            {
                "source_row": row["source_row"],
                "member_id": member_id,
                "book_copy_id": copy_id,
                "borrowed_at": row["borrowed_at"],
                "due_at": due_at,
                "returned_at": due_at if status == "returned" else None,
                "notes": notes_for_status(row["status_text"]),
            }
        )
    skipped["collapsed_open"] = collapse_extra_open_loans(planned)
    return planned, dict(skipped)


def summarize(rows: list[dict], planned: list[dict], skipped: dict[str, int]) -> dict[str, int]:
    open_count = sum(1 for row in planned if row["returned_at"] is None)
    return {
        "sheet_rows": len(rows),
        "imported": len(planned),
        "open": open_count,
        "returned": len(planned) - open_count,
        "skipped_member": skipped.get("member_not_found", 0),
        "skipped_copy": skipped.get("copy_not_found", 0),
        "skipped_date": skipped.get("bad_date", 0),
        "collapsed_open": skipped.get("collapsed_open", 0),
        "recovered_member": skipped.get("recovered_member", 0),
        "recovered_copy": skipped.get("recovered_copy", 0),
    }


def print_report(stats: dict[str, int]) -> None:
    print(
        "Loans sheet: "
        f"rows={stats['sheet_rows']}, "
        f"importable={stats['imported']}, "
        f"open={stats['open']}, "
        f"returned={stats['returned']}, "
        f"skipped_member={stats['skipped_member']}, "
        f"skipped_copy={stats['skipped_copy']}, "
        f"skipped_date={stats['skipped_date']}, "
        f"collapsed_open={stats['collapsed_open']}, "
        f"recovered_member={stats['recovered_member']}, "
        f"recovered_copy={stats['recovered_copy']}"
    )


def apply_import(target_db: Path, planned: list[dict]) -> None:
    if not target_db.is_file():
        raise SystemExit(f"target database does not exist: {target_db}")
    conn = sqlite3.connect(target_db)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        conn.execute("DELETE FROM loans")
        conn.executemany(
            """
            INSERT INTO loans (
                member_id, book_copy_id, borrowed_at, due_at, returned_at, notes
            ) VALUES (
                :member_id, :book_copy_id, :borrowed_at, :due_at, :returned_at, :notes
            )
            """,
            planned,
        )
        conn.commit()
        count = conn.execute("SELECT COUNT(*) FROM loans").fetchone()[0]
        open_count = conn.execute(
            "SELECT COUNT(*) FROM loans WHERE returned_at IS NULL"
        ).fetchone()[0]
        print(f"Imported {count} loans ({open_count} open) into {target_db}")
    finally:
        conn.close()


class ImportLoansSelfTest(unittest.TestCase):
    def test_excel_serial_to_iso(self) -> None:
        self.assertEqual(excel_serial_to_iso("43467"), "2019-01-02")
        self.assertEqual(excel_serial_to_iso("44414"), "2021-08-06")
        self.assertEqual(excel_serial_to_iso("2021-07-07"), "2021-07-07")
        self.assertEqual(excel_serial_to_iso(""), "")

    def test_normalize_identifier(self) -> None:
        self.assertEqual(normalize_identifier("11842"), "11842")
        self.assertEqual(normalize_identifier("*****"), "")
        self.assertEqual(normalize_identifier("R-18098"), "18098")
        self.assertEqual(normalize_identifier("j29578"), "29578")
        self.assertEqual(normalize_identifier("11295103-9"), "11295103")
        self.assertEqual(normalize_identifier("11842.0"), "11842")

    def test_status_and_dates(self) -> None:
        self.assertEqual(map_status("رجوع"), "returned")
        self.assertEqual(map_status("في الإعارة"), "open")
        self.assertEqual(map_status("للتثبت"), "returned")
        self.assertIsNone(notes_for_status("رجوع"))
        self.assertEqual(notes_for_status("للتثبت"), "للتثبت")
        self.assertEqual(add_days("2021-07-07", 14), "2021-07-21")

    def test_open_sheet_status_keeps_the_fourteen_day_due_date(self) -> None:
        by_local = {
            "10": [
                {
                    "id": 4,
                    "source": "arabic",
                    "local_id": "10",
                    "central_id": "100",
                    "title": "alpha",
                }
            ],
            "11": [
                {
                    "id": 5,
                    "source": "arabic",
                    "local_id": "11",
                    "central_id": "101",
                    "title": "beta",
                }
            ],
            "12": [
                {
                    "id": 6,
                    "source": "arabic",
                    "local_id": "12",
                    "central_id": "102",
                    "title": "gamma",
                }
            ],
        }
        planned, _ = plan_import(
            [
                {
                    "source_row": 5,
                    "membership_number": "1",
                    "title": "alpha",
                    "local_id": "10",
                    "central_id": "100",
                    "borrowed_at": "2021-01-01",
                    "status_text": "في الإعارة",
                },
                {
                    "source_row": 6,
                    "membership_number": "1",
                    "title": "beta",
                    "local_id": "11",
                    "central_id": "101",
                    "borrowed_at": "2026-09-10",
                    "status_text": "في الإعارة",
                },
                {
                    "source_row": 7,
                    "membership_number": "1",
                    "title": "gamma",
                    "local_id": "12",
                    "central_id": "102",
                    "borrowed_at": "2021-01-01",
                    "status_text": "رجوع",
                },
            ],
            {"1": 3},
            by_local,
        )
        self.assertIsNone(planned[0]["returned_at"])
        self.assertEqual(planned[0]["due_at"], "2021-01-15")
        self.assertIsNone(planned[1]["returned_at"])
        self.assertEqual(planned[1]["due_at"], "2026-09-24")
        self.assertEqual(planned[2]["returned_at"], "2021-01-15")
        self.assertEqual(planned[2]["due_at"], "2021-01-15")

    def test_resolve_copy_prefers_local_and_central(self) -> None:
        by_local = {
            "10": [
                {
                    "id": 1,
                    "source": "arabic",
                    "local_id": "10",
                    "central_id": "100",
                    "title": "alpha",
                },
                {
                    "id": 2,
                    "source": "foreign",
                    "local_id": "10",
                    "central_id": "200",
                    "title": "beta",
                },
            ]
        }
        by_central = copies_by_central(by_local)
        self.assertEqual(resolve_copy("10", "200", "beta", by_local, by_central), 2)

    def test_resolve_copy_uses_title_when_local_is_in_both_registers(self) -> None:
        by_local = {
            "10": [
                {
                    "id": 1,
                    "source": "arabic",
                    "local_id": "10",
                    "central_id": "100",
                    "title": "ديوان شعر",
                },
                {
                    "id": 2,
                    "source": "foreign",
                    "local_id": "10",
                    "central_id": "200",
                    "title": "le cid",
                },
            ]
        }
        by_central = copies_by_central(by_local)
        self.assertEqual(resolve_copy("10", "", "le cid", by_local, by_central), 2)
        self.assertEqual(resolve_copy("10", "", "ديوان شعر", by_local, by_central), 1)

    def test_plan_skips_unknown_member_and_copy(self) -> None:
        rows = [
            {
                "source_row": 5,
                "membership_number": "1",
                "title": "alpha",
                "local_id": "10",
                "central_id": "100",
                "borrowed_at": "2021-07-07",
                "status_text": "رجوع",
            },
            {
                "source_row": 6,
                "membership_number": "missing",
                "title": "alpha",
                "local_id": "10",
                "central_id": "100",
                "borrowed_at": "2021-07-07",
                "status_text": "رجوع",
            },
            {
                "source_row": 7,
                "membership_number": "1",
                "title": "ghost",
                "local_id": "99",
                "central_id": "",
                "borrowed_at": "2021-07-07",
                "status_text": "في الإعارة",
            },
        ]
        by_local = {
            "10": [
                {
                    "id": 4,
                    "source": "arabic",
                    "local_id": "10",
                    "central_id": "100",
                    "title": "alpha",
                }
            ]
        }
        planned, skipped = plan_import(rows, {"1": 3}, by_local)
        self.assertEqual(len(planned), 1)
        self.assertEqual(planned[0]["member_id"], 3)
        self.assertEqual(planned[0]["book_copy_id"], 4)
        self.assertEqual(planned[0]["due_at"], "2021-07-21")
        self.assertEqual(planned[0]["returned_at"], "2021-07-21")
        self.assertEqual(skipped["member_not_found"], 1)
        self.assertEqual(skipped["copy_not_found"], 1)

    def test_only_latest_loan_of_a_copy_stays_open(self) -> None:
        rows = [
            {
                "source_row": 5,
                "membership_number": "1",
                "title": "alpha",
                "local_id": "10",
                "central_id": "100",
                "borrowed_at": "2021-01-01",
                "status_text": "في الإعارة",
            },
            {
                "source_row": 6,
                "membership_number": "1",
                "title": "alpha",
                "local_id": "10",
                "central_id": "100",
                "borrowed_at": "2022-01-01",
                "status_text": "في الإعارة",
            },
        ]
        by_local = {
            "10": [
                {
                    "id": 4,
                    "source": "arabic",
                    "local_id": "10",
                    "central_id": "100",
                    "title": "alpha",
                }
            ]
        }
        planned, skipped = plan_import(rows, {"1": 3}, by_local)
        self.assertEqual(len(planned), 2)
        self.assertEqual(planned[0]["returned_at"], "2021-01-15")
        self.assertIsNone(planned[1]["returned_at"])
        self.assertEqual(skipped["collapsed_open"], 1)

    def test_starred_number_needs_matching_name(self) -> None:
        members = {"1231": 9}
        names_by_id = {9: "الاسم المخزن"}
        self.assertIsNone(
            resolve_member("*1231", "اسم آخر", members, names_by_id=names_by_id)
        )
        self.assertEqual(
            resolve_member("*1231", "الاسم المخزن", members, names_by_id=names_by_id),
            9,
        )

    def test_unique_full_name_recovers_member(self) -> None:
        members = {"10": 1}
        by_name = {"فلان الفلاني": [2]}
        self.assertEqual(
            resolve_member("*****", "فلان الفلاني", members, members_by_name=by_name),
            2,
        )
        self.assertIsNone(
            resolve_member("*****", "اسم غير موجود", members, members_by_name=by_name)
        )
        self.assertIsNone(
            resolve_member(
                "*****",
                "اسم مكرر",
                members,
                members_by_name={"اسم مكرر": [3, 4]},
            )
        )

    def test_unique_title_recovers_single_copy_only(self) -> None:
        by_local = {
            "1": [
                {
                    "id": 11,
                    "source": "arabic",
                    "local_id": "1",
                    "central_id": "100",
                    "title": "النجوم تحاكم القمر",
                }
            ],
            "2": [
                {
                    "id": 12,
                    "source": "arabic",
                    "local_id": "2",
                    "central_id": "200",
                    "title": "نسخة أولى",
                },
                {
                    "id": 13,
                    "source": "arabic",
                    "local_id": "3",
                    "central_id": "201",
                    "title": "نسخة أولى",
                },
            ],
        }
        title_copies = unique_title_single_copies(by_local)
        by_central = copies_by_central(by_local)
        self.assertEqual(
            resolve_copy(
                "",
                "",
                "النجوم تحاكم القمر",
                by_local,
                by_central,
                title_copies,
            ),
            11,
        )
        self.assertIsNone(
            resolve_copy("", "", "نسخة أولى", by_local, by_central, title_copies)
        )

    def test_isbn_title_recovers_single_copy(self) -> None:
        by_local: dict[str, list[dict]] = {}
        by_central: dict[str, list[dict]] = {}
        isbn_copies = {"9781234567890": 22}
        self.assertEqual(
            resolve_copy(
                "",
                "",
                "978-1-234-56789-0",
                by_local,
                by_central,
                isbn_copies=isbn_copies,
            ),
            22,
        )
        self.assertIsNone(
            resolve_copy(
                "",
                "",
                "not-an-isbn",
                by_local,
                by_central,
                isbn_copies=isbn_copies,
            )
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xlsx", type=Path, default=DEFAULT_XLSX)
    parser.add_argument("--target-db", type=Path, default=DEFAULT_TARGET_DB)
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Replace loans in the target database with the matched sheet rows.",
    )
    parser.add_argument("--self-test", action="store_true", help="Run the unit tests and exit.")
    args = parser.parse_args()

    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(ImportLoansSelfTest)
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        sys.exit(0 if result.wasSuccessful() else 1)

    if not args.xlsx.is_file():
        raise SystemExit(f"workbook not found: {args.xlsx}")
    if not args.target_db.is_file():
        raise SystemExit(f"target database does not exist: {args.target_db}")

    rows = read_loan_rows(args.xlsx)
    conn = sqlite3.connect(args.target_db)
    try:
        members = load_members(conn)
        members_by_name, names_by_id = load_member_name_indexes(conn)
        by_local = load_copy_index(conn)
        isbn_copies = unique_isbn_single_copies(conn)
    finally:
        conn.close()
    planned, skipped = plan_import(
        rows,
        members,
        by_local,
        members_by_name=members_by_name,
        names_by_id=names_by_id,
        isbn_copies=isbn_copies,
    )
    print_report(summarize(rows, planned, skipped))
    if not args.apply:
        print("Dry run only. Pass --apply to write the live database.")
        return
    apply_import(args.target_db, planned)


if __name__ == "__main__":
    main()
