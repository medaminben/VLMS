# scripts/manual/test_prepare_sandbox.py
import sqlite3
import tempfile
import unittest
from datetime import timedelta
from pathlib import Path

import prepare_sandbox as ps

SCHEMA = """
CREATE TABLE employees (id INTEGER PRIMARY KEY, username TEXT UNIQUE, password_hash TEXT,
  first_name TEXT, last_name TEXT);
CREATE TABLE members (id INTEGER PRIMARY KEY, membership_number TEXT, first_name TEXT,
  last_name TEXT, full_name TEXT, sex TEXT, date_of_birth TEXT, phone TEXT, address TEXT,
  email TEXT, notes TEXT, photo_path TEXT, id_image_path TEXT, city TEXT);
"""


def make_db(path):
    conn = sqlite3.connect(path)
    conn.executescript(SCHEMA)
    conn.execute("INSERT INTO employees VALUES (1,'amin','x','أمين','بن حسين')")
    conn.execute(
        "INSERT INTO members VALUES (1,'12','رنيم','بنت سفيان','رنيم بنت سفيان',"
        "'female','2001-05-17','22 333 444','نهج 5','a@b.tn','ملاحظة','p.jpg','i.jpg','تونس')")
    conn.execute(
        "INSERT INTO members VALUES (2,'13','Karim','Ben Ali',NULL,"
        "'male','1990-02-03',NULL,NULL,NULL,NULL,NULL,NULL,'المهدية')")
    conn.commit()
    return conn


class ScrubTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.conn = make_db(Path(self.tmp.name) / "t.db")

    def tearDown(self):
        self.conn.close()
        self.tmp.cleanup()

    def test_scrub_removes_every_live_name(self):
        names = ps.live_names(self.conn)
        self.assertIn("رنيم بنت سفيان", names)
        ps.scrub(self.conn)
        self.assertEqual(ps.remaining_names(self.conn, names), [])

    def test_scrub_clears_contact_details_and_images(self):
        ps.scrub(self.conn)
        row = self.conn.execute(
            "SELECT phone, address, email, notes, photo_path, id_image_path FROM members WHERE id=1"
        ).fetchone()
        self.assertEqual(row[0], "00 000 000")
        self.assertEqual(row[1:], (None, None, None, None, None))

    def test_scrub_keeps_birth_year_only(self):
        ps.scrub(self.conn)
        dob = self.conn.execute("SELECT date_of_birth FROM members WHERE id=1").fetchone()[0]
        self.assertEqual(dob, "2001-01-01")

    def test_scrub_keeps_city_sex_and_number(self):
        ps.scrub(self.conn)
        row = self.conn.execute(
            "SELECT membership_number, sex, city FROM members WHERE id=1").fetchone()
        self.assertEqual(row, ("12", "female", "تونس"))

    def test_fake_full_name_is_first_plus_last(self):
        ps.scrub(self.conn)
        first, last, full = self.conn.execute(
            "SELECT first_name, last_name, full_name FROM members WHERE id=2").fetchone()
        self.assertEqual(full, f"{first} {last}")
        self.assertIn(first, ps.FAKE_FIRST)

    def test_scrub_renames_employees(self):
        ps.scrub(self.conn)
        row = self.conn.execute(
            "SELECT username, first_name, last_name FROM employees").fetchone()
        self.assertEqual(row, ("librarian", "أمين", "المكتبة"))

    def test_remaining_names_reports_a_leak(self):
        names = ps.live_names(self.conn)
        self.assertIn("رنيم", ps.remaining_names(self.conn, names))


class ShiftDatesTest(unittest.TestCase):
    def setUp(self):
        self.conn = sqlite3.connect(":memory:")
        self.conn.executescript(
            "CREATE TABLE loans (id INTEGER PRIMARY KEY, borrowed_at TEXT, due_at TEXT,"
            " returned_at TEXT, archived_at TEXT);"
            "CREATE TABLE members (id INTEGER PRIMARY KEY, date_of_birth TEXT,"
            " active_until TEXT, registered_at TEXT);")
        self.conn.execute(
            "INSERT INTO loans VALUES (1,'2026-09-20','2026-10-04',NULL,'2026-09-25 08:15:00')")
        self.conn.execute(
            "INSERT INTO members VALUES (1,'1990-01-01','2026-09-29','2026-07-14 10:00:00')")
        self.conn.execute("INSERT INTO members VALUES (2,'1991-01-01','2027-01-31','2026-07-14')")

    def tearDown(self):
        self.conn.close()

    def test_moves_activity_dates_by_the_days_since_the_data_was_made(self):
        today = ps.DATA_AS_OF + timedelta(days=31)
        self.assertEqual(ps.shift_dates(self.conn, today), 31)
        loan = self.conn.execute(
            "SELECT borrowed_at, due_at, returned_at, archived_at FROM loans").fetchone()
        self.assertEqual(loan, ("2026-10-21", "2026-11-04", None, "2026-10-26 08:15:00"))

    def test_keeps_each_value_in_its_own_format(self):
        ps.shift_dates(self.conn, ps.DATA_AS_OF + timedelta(days=2))
        rows = self.conn.execute(
            "SELECT active_until, registered_at FROM members ORDER BY id").fetchall()
        self.assertEqual(rows, [("2026-10-01", "2026-07-16 10:00:00"),
                                ("2027-02-02", "2026-07-16")])

    def test_leaves_birth_dates_alone(self):
        ps.shift_dates(self.conn, ps.DATA_AS_OF + timedelta(days=40))
        dob = self.conn.execute("SELECT date_of_birth FROM members WHERE id=1").fetchone()[0]
        self.assertEqual(dob, "1990-01-01")

    def test_does_nothing_on_or_before_the_data_date(self):
        self.assertEqual(ps.shift_dates(self.conn, ps.DATA_AS_OF), 0)
        due = self.conn.execute("SELECT due_at FROM loans").fetchone()[0]
        self.assertEqual(due, "2026-10-04")

    def test_moves_a_row_in_one_step_so_ordering_checks_hold(self):
        conn = sqlite3.connect(":memory:")
        conn.execute("CREATE TABLE loans (id INTEGER PRIMARY KEY, borrowed_at TEXT, due_at TEXT,"
                     " CHECK (date(due_at) > date(borrowed_at)))")
        conn.execute("INSERT INTO loans VALUES (1,'2026-09-20','2026-10-04')")
        ps.shift_dates(conn, ps.DATA_AS_OF + timedelta(days=60))
        self.assertEqual(conn.execute("SELECT borrowed_at, due_at FROM loans").fetchone(),
                         ("2026-11-19", "2026-12-03"))
        conn.close()

    def test_skips_tables_the_database_does_not_have(self):
        conn = make_db(":memory:")
        self.assertEqual(ps.shift_dates(conn, ps.DATA_AS_OF + timedelta(days=5)), 5)
        conn.close()


class CliTest(unittest.TestCase):
    def test_prepare_writes_marker_and_empty_members(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            make_db(tmp / "live.db").close()
            (tmp / "books" / "1").mkdir(parents=True)
            (tmp / "books" / "1" / "cover.jpg").write_bytes(b"x")
            out = tmp / "sandbox"
            code = ps.main(["--source", str(tmp / "live.db"), "--books", str(tmp / "books"),
                            "--out", str(out)])
            self.assertEqual(code, 0)
            self.assertEqual((out / ".vlms-manual-sandbox").read_text().strip(),
                             "vlms-manual-sandbox v1")
            self.assertTrue((out / "resources" / "books" / "1" / "cover.jpg").exists())
            self.assertEqual(list((out / "resources" / "members").iterdir()), [])
            self.assertTrue((out / "config").is_dir())
            live = sqlite3.connect(tmp / "live.db")
            self.assertEqual(live.execute("SELECT first_name FROM members WHERE id=1").fetchone()[0],
                             "رنيم")  # the source is never modified

    def test_prepare_refuses_to_write_into_the_source_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "database").mkdir()
            make_db(tmp / "database" / "vlms.db").close()
            code = ps.main(["--source", str(tmp / "database" / "vlms.db"),
                            "--books", str(tmp), "--out", str(tmp)])
            self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
