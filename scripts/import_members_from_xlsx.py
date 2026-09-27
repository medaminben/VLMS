#!/usr/bin/env python3
"""Import قائمة المشتركين from the public-library workbook into VLMS.

Default source:

  /home/amin/Dokumente/doc/KLMS resources/واجهة المكتبة العمومية نسخة أمين بن حسين.xlsx

Default target: database/vlms.db

Membership numbers stay UNIQUE. When Excel reused a number for two people in
2019, the earlier ت.الإشتراك keeps N and the later row becomes Nb.

Does not apply to the live database unless --apply is passed. --dry-run
(the default) only prints counts. --repair-names re-splits stored
first_name/last_name from full_name without deleting members or loans.
"""

from __future__ import annotations

import argparse
import collections
import datetime
import re
import sqlite3
import sys
import unittest
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCHEMA = ROOT / "database" / "schema.sql"
DEFAULT_TARGET_DB = ROOT / "database" / "vlms.db"
DEFAULT_XLSX = Path(
    "/home/amin/Dokumente/doc/KLMS resources/واجهة المكتبة العمومية نسخة أمين بن حسين.xlsx"
)

NS = {"m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
MEMBERS_SHEET = "قائمة المشتركين"

CITY_ALIASES = {
    "قصور الساف": "قصورالساف",
}


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


NAME_MARKERS = {"بن", "بنت"}


def split_full_name(name: str) -> tuple[str, str]:
    tokens = [part for part in re.split(r"\s+", name.strip()) if part]
    if not tokens:
        return "", ""
    if len(tokens) == 1:
        return tokens[0], tokens[0]
    for index, token in enumerate(tokens):
        if token in NAME_MARKERS and index > 0:
            return " ".join(tokens[:index]), " ".join(tokens[index:])
    return tokens[0], " ".join(tokens[1:])


def split_address(address: str) -> tuple[str, str]:
    text = address.strip()
    if not text:
        return "", ""
    if "/" not in text:
        return text, ""
    street, city = [part.strip() for part in text.split("/", 1)]
    city = CITY_ALIASES.get(city, city)
    return street, city


def map_sex(value: str) -> str:
    text = value.strip()
    if text == "ذكر":
        return "male"
    if text == "أنثى":
        return "female"
    return ""


def map_age_group(value: str) -> str:
    text = value.strip()
    if text == "شباب":
        return "youth"
    if text == "كهول":
        return "adult"
    return ""


def assign_membership_numbers(rows: list[dict]) -> list[str]:
    """Earlier subscription date keeps N; the later twin becomes Nb."""
    groups: dict[str, list[int]] = collections.defaultdict(list)
    for index, row in enumerate(rows):
        groups[row["excel_number"]].append(index)

    numbers = [""] * len(rows)
    for excel_number, indexes in groups.items():
        ordered = sorted(indexes, key=lambda i: (rows[i]["registered_at"], i))
        numbers[ordered[0]] = excel_number
        if len(ordered) > 1:
            numbers[ordered[1]] = excel_number + "b"
        if len(ordered) > 2:
            raise ValueError(f"more than two rows for membership number {excel_number}")
    return numbers


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
            for node in inline.iter("{http://schemas.openxmlformats.org/spreadsheetml/2006/main}t")
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
            for node in item.iter("{http://schemas.openxmlformats.org/spreadsheetml/2006/main}t")
        ]
        shared.append("".join(texts))
    return shared


def members_sheet_path(archive: zipfile.ZipFile) -> str:
    workbook = ET.fromstring(archive.read("xl/workbook.xml"))
    rels = ET.fromstring(archive.read("xl/_rels/workbook.xml.rels"))
    rel_ns = {"r": "http://schemas.openxmlformats.org/package/2006/relationships"}
    targets = {rel.get("Id"): rel.get("Target") for rel in rels.findall("r:Relationship", rel_ns)}
    sheet_ns = {
        "m": "http://schemas.openxmlformats.org/spreadsheetml/2006/main",
        "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
    }
    for sheet in workbook.findall("m:sheets/m:sheet", sheet_ns):
        if sheet.get("name") == MEMBERS_SHEET:
            target = targets[sheet.get("{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id")]
            return "xl/" + target.lstrip("/")
    raise SystemExit(f"workbook has no sheet named {MEMBERS_SHEET}")


def read_member_rows(xlsx_path: Path) -> list[dict]:
    with zipfile.ZipFile(xlsx_path) as archive:
        shared = load_shared_strings(archive)
        root = ET.fromstring(archive.read(members_sheet_path(archive)))
        rows: list[dict] = []
        for xml_row in root.findall("m:sheetData/m:row", NS):
            row_index = int(xml_row.get("r") or 0)
            if row_index < 6:
                continue
            cells = {
                col_letter(cell.get("r")): (cell_value(cell, shared) or "").strip()
                for cell in xml_row.findall("m:c", NS)
            }
            if not any(cells.get(column) for column in "ABCDEFGHIJ"):
                continue
            full_name = cells.get("C", "")
            first_name, last_name = split_full_name(full_name)
            street, city = split_address(cells.get("D", ""))
            rows.append(
                {
                    "source_row": row_index,
                    "excel_number": cells.get("A", ""),
                    "registered_at": excel_serial_to_iso(cells.get("B", "")),
                    "full_name": full_name,
                    "first_name": first_name,
                    "last_name": last_name,
                    "address": street,
                    "city": city,
                    "date_of_birth": excel_serial_to_iso(cells.get("E", "")),
                    "sex": map_sex(cells.get("G", "")),
                    "occupation": cells.get("H", ""),
                    "phone": cells.get("I", ""),
                    "age_group": map_age_group(cells.get("J", "")),
                }
            )
        for index, number in enumerate(assign_membership_numbers(rows)):
            rows[index]["membership_number"] = number
        return rows


def summarize(rows: list[dict]) -> dict[str, int]:
    return {
        "rows": len(rows),
        "suffix_b": sum(1 for row in rows if row["membership_number"].endswith("b")),
        "missing_phone": sum(1 for row in rows if not row["phone"]),
        "unparsed_registered_at": sum(1 for row in rows if not row["registered_at"]),
        "unparsed_date_of_birth": sum(1 for row in rows if not row["date_of_birth"]),
        "missing_sex": sum(1 for row in rows if not row["sex"]),
        "missing_age_group": sum(1 for row in rows if not row["age_group"]),
    }


def print_report(stats: dict[str, int]) -> None:
    print(
        "Members sheet: "
        f"rows={stats['rows']}, "
        f"b_suffixes={stats['suffix_b']}, "
        f"missing_phones={stats['missing_phone']}, "
        f"unparsed_registered_at={stats['unparsed_registered_at']}, "
        f"unparsed_date_of_birth={stats['unparsed_date_of_birth']}"
    )


def clear_members(conn: sqlite3.Connection) -> None:
    conn.execute("PRAGMA foreign_keys = ON")
    conn.execute("DELETE FROM loans")
    conn.execute("DELETE FROM member_status_history")
    conn.execute("DELETE FROM members")
    conn.commit()


def _optional(value: str) -> str | None:
    return value if value else None


def insert_members(
    conn: sqlite3.Connection, rows: list[dict], today: str | None = None
) -> None:
    # The import day is the local calendar date, the same day Clock::today()
    # names in the app. Callers pass an ISO date; a normal import uses today.
    if today is None:
        today = datetime.date.today().isoformat()
    payload = [
        {
            **row,
            "sex": _optional(row["sex"]),
            "date_of_birth": _optional(row["date_of_birth"]),
            "phone": _optional(row["phone"]),
            "address": _optional(row["address"]),
            "city": _optional(row["city"]),
            "occupation": _optional(row["occupation"]),
            "age_group": _optional(row["age_group"]),
            "full_name": _optional(row["full_name"]),
        }
        for row in rows
    ]
    conn.executemany(
        """
        INSERT INTO members (
            membership_number, first_name, last_name, sex, date_of_birth,
            phone, address, city, active_until, notes, registered_at, updated_at,
            occupation, age_group, full_name, source_row
        ) VALUES (
            :membership_number, :first_name, :last_name, :sex, :date_of_birth,
            :phone, :address, :city, date(:registered_at, '+1 year', '-1 day'),
            NULL, :registered_at,
            datetime('now', 'localtime'), :occupation, :age_group, :full_name,
            :source_row
        )
        """,
        payload,
    )
    # Status is not stored: a member is active while active_until is today or
    # later. The history row records what that made them on the day of import.
    for row in conn.execute(
        "SELECT id, CASE WHEN active_until >= :today "
        "THEN 'active' ELSE 'non_active' END FROM members",
        {"today": today},
    ).fetchall():
        conn.execute(
            "INSERT INTO member_status_history (member_id, old_status, new_status) "
            "VALUES (?, NULL, ?)",
            (row[0], row[1]),
        )
    conn.commit()


def repair_member_names(conn: sqlite3.Connection) -> int:
    updates: list[tuple[str, str, int]] = []
    for row_id, full_name, first_name, last_name in conn.execute(
        "SELECT id, full_name, first_name, last_name FROM members"
    ):
        new_first, new_last = split_full_name(full_name or "")
        if not new_first or not new_last:
            continue
        if (new_first, new_last) != (first_name, last_name):
            updates.append((new_first, new_last, row_id))
    conn.executemany(
        "UPDATE members SET first_name = ?, last_name = ?, "
        "updated_at = datetime('now', 'localtime') WHERE id = ?",
        updates,
    )
    conn.commit()
    return len(updates)


def apply_import(target_db: Path, rows: list[dict]) -> None:
    if not target_db.is_file():
        raise SystemExit(f"target database does not exist: {target_db}")
    conn = sqlite3.connect(target_db)
    try:
        clear_members(conn)
        insert_members(conn, rows)
        count = conn.execute("SELECT COUNT(*) FROM members").fetchone()[0]
        print(f"Imported {count} members into {target_db}")
    finally:
        conn.close()


def apply_name_repair(target_db: Path) -> None:
    if not target_db.is_file():
        raise SystemExit(f"target database does not exist: {target_db}")
    conn = sqlite3.connect(target_db)
    try:
        changed = repair_member_names(conn)
        print(f"Repaired first/last names for {changed} members in {target_db}")
    finally:
        conn.close()


class ImportMembersSelfTest(unittest.TestCase):
    def test_excel_serial_to_iso(self) -> None:
        self.assertEqual(excel_serial_to_iso("43467"), "2019-01-02")
        self.assertEqual(excel_serial_to_iso(""), "")

    def test_split_full_name(self) -> None:
        self.assertEqual(split_full_name("أميمة بنت حمودة بالحج"), ("أميمة", "بنت حمودة بالحج"))
        self.assertEqual(split_full_name("وحيد"), ("وحيد", "وحيد"))
        self.assertEqual(
            split_full_name("محمد أمين بن أحمد عمارة"),
            ("محمد أمين", "بن أحمد عمارة"),
        )
        self.assertEqual(
            split_full_name("نور الإسلام بنت شرف الدين الفريقي"),
            ("نور الإسلام", "بنت شرف الدين الفريقي"),
        )
        self.assertEqual(
            split_full_name("عبد الرحمان المنصف بن علي التست"),
            ("عبد الرحمان المنصف", "بن علي التست"),
        )
        self.assertEqual(split_full_name("محمد علي"), ("محمد", "علي"))
        self.assertEqual(split_full_name(""), ("", ""))

    def test_split_address_normalises_city(self) -> None:
        self.assertEqual(
            split_address("نهج بن منظور / قصور الساف"),
            ("نهج بن منظور", "قصورالساف"),
        )

    def test_maps(self) -> None:
        self.assertEqual(map_sex("ذكر"), "male")
        self.assertEqual(map_sex("أنثى"), "female")
        self.assertEqual(map_age_group("شباب"), "youth")
        self.assertEqual(map_age_group("كهول"), "adult")

    def test_later_subscription_gets_b_suffix(self) -> None:
        rows = [
            {"excel_number": "1", "registered_at": "2019-07-03"},
            {"excel_number": "1", "registered_at": "2019-01-02"},
            {"excel_number": "12", "registered_at": "2020-01-01"},
        ]
        self.assertEqual(assign_membership_numbers(rows), ["1b", "1", "12"])

    def test_repair_member_names_updates_wrong_split_only(self) -> None:
        conn = sqlite3.connect(":memory:")
        conn.execute(
            "CREATE TABLE members ("
            "id INTEGER PRIMARY KEY, full_name TEXT, first_name TEXT, "
            "last_name TEXT, updated_at TEXT)"
        )
        conn.executemany(
            "INSERT INTO members VALUES (?, ?, ?, ?, '2020-01-01')",
            [
                (1, "محمد أمين بن أحمد عمارة", "محمد", "أمين بن أحمد عمارة"),
                (2, "أميمة بنت حمودة بالحج", "أميمة", "بنت حمودة بالحج"),
            ],
        )
        self.assertEqual(repair_member_names(conn), 1)
        rows = {
            row[0]: row[1:]
            for row in conn.execute(
                "SELECT id, first_name, last_name FROM members ORDER BY id"
            )
        }
        self.assertEqual(rows[1], ("محمد أمين", "بن أحمد عمارة"))
        self.assertEqual(rows[2], ("أميمة", "بنت حمودة بالحج"))
        conn.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xlsx", type=Path, default=DEFAULT_XLSX)
    parser.add_argument("--target-db", type=Path, default=DEFAULT_TARGET_DB)
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Write into the target database after deleting test loans and members.",
    )
    parser.add_argument(
        "--repair-names",
        action="store_true",
        help="Re-split first_name/last_name from stored full_name without deleting rows.",
    )
    parser.add_argument("--self-test", action="store_true", help="Run the unit tests and exit.")
    args = parser.parse_args()

    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(ImportMembersSelfTest)
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        sys.exit(0 if result.wasSuccessful() else 1)

    if args.repair_names:
        apply_name_repair(args.target_db)
        return

    if not args.xlsx.is_file():
        raise SystemExit(f"workbook not found: {args.xlsx}")

    rows = read_member_rows(args.xlsx)
    print_report(summarize(rows))
    if not args.apply:
        print("Dry run only. Pass --apply to write the live database.")
        return
    apply_import(args.target_db, rows)


if __name__ == "__main__":
    main()
