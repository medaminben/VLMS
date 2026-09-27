#!/usr/bin/env python3
"""Import a catalog (and book covers) from a source library database into VLMS.

Default paths assume the source system is checked out beside this one:

  source DB:     ../KLMS/database/klms.db
  covers:        ../KLMS/database/covers/
  covers CSV:    ../KLMS/database/covers/book_covers.csv
  target DB:     database/vlms.db
  target books:  resources/books/<id>/cover.jpg
  target members: resources/members/<id>/

Book IDs are preserved so cover CSV mappings stay valid.
"""

from __future__ import annotations

import argparse
import csv
import shutil
import sqlite3
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCHEMA = ROOT / "database" / "schema.sql"
DEFAULT_SOURCE_ROOT = ROOT.parent / "KLMS"
DEFAULT_SOURCE_DB = DEFAULT_SOURCE_ROOT / "database" / "klms.db"
DEFAULT_COVERS_DIR = DEFAULT_SOURCE_ROOT / "database" / "covers"
DEFAULT_COVERS_CSV = DEFAULT_COVERS_DIR / "book_covers.csv"
DEFAULT_TARGET_DB = ROOT / "database" / "vlms.db"
DEFAULT_RESOURCES_DIR = ROOT / "resources"


def split_sql(script: str) -> list[str]:
    """Splits a schema script into statements.

    A scanner rather than script.split(';'), for the same reason
    SqlText::splitStatements is one on the C++ side: schema.sql contains
    semicolons inside comments ("Must equal Database::kSchemaVersion; ...") and
    naive splitting cuts a comment in half, leaving the tail of an English
    sentence to be executed as SQL.
    """
    statements: list[str] = []
    current: list[str] = []
    in_string = False
    in_comment = False
    index = 0

    while index < len(script):
        char = script[index]

        if in_comment:
            if char == "\n":
                in_comment = False
                current.append(char)
            index += 1
            continue

        if in_string:
            current.append(char)
            if char == "'":
                # '' is an escaped quote, not the end of the string.
                if index + 1 < len(script) and script[index + 1] == "'":
                    current.append(script[index + 1])
                    index += 2
                    continue
                in_string = False
            index += 1
            continue

        if char == "-" and script.startswith("--", index):
            in_comment = True
            index += 2
            continue

        if char == "'":
            in_string = True
            current.append(char)
            index += 1
            continue

        if char == ";":
            statement = "".join(current).strip()
            if statement:
                statements.append(statement)
            current = []
            index += 1
            continue

        current.append(char)
        index += 1

    trailing = "".join(current).strip()
    if trailing:
        statements.append(trailing)
    return statements


def apply_schema(conn: sqlite3.Connection, schema_path: Path) -> None:
    sql = schema_path.read_text(encoding="utf-8")
    for statement in split_sql(sql):
        conn.execute(statement)
    conn.commit()


def table_exists(conn: sqlite3.Connection, name: str) -> bool:
    row = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1",
        (name,),
    ).fetchone()
    return row is not None


def backup_database(target_db: Path) -> Path | None:
    """Copies the database aside before --replace throws the catalog away.

    Members, their photos and their loan history live in the same file as the
    catalog being dropped. They are not touched by the import, but a wrong
    --source-db is a cheap mistake and this is a cheap insurance policy.
    """
    if not target_db.is_file():
        return None
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = target_db.with_name(f"{target_db.name}.bak-{stamp}")
    shutil.copy2(target_db, backup)
    return backup


def clear_catalog(conn: sqlite3.Connection) -> None:
    # Order matters for foreign keys.
    for table in ("loans", "book_copies", "books", "categories", "publishers", "authors"):
        if table_exists(conn, table):
            conn.execute(f"DELETE FROM {table}")
    conn.commit()


def copy_table(
    source: sqlite3.Connection,
    target: sqlite3.Connection,
    table: str,
    columns: list[str],
) -> int:
    cols = ", ".join(columns)
    placeholders = ", ".join("?" for _ in columns)
    rows = source.execute(f"SELECT {cols} FROM {table}").fetchall()
    target.executemany(
        f"INSERT INTO {table} ({cols}) VALUES ({placeholders})",
        rows,
    )
    return len(rows)


def sync_sequence(conn: sqlite3.Connection, table: str) -> None:
    max_id = conn.execute(f"SELECT COALESCE(MAX(id), 0) FROM {table}").fetchone()[0]
    exists = conn.execute(
        "SELECT 1 FROM sqlite_sequence WHERE name=? LIMIT 1",
        (table,),
    ).fetchone()
    if exists:
        conn.execute("UPDATE sqlite_sequence SET seq=? WHERE name=?", (max_id, table))
    else:
        conn.execute("INSERT INTO sqlite_sequence(name, seq) VALUES (?, ?)", (table, max_id))


# A 946-byte placeholder that got fetched for books whose ISBN normalised to
# "0". It is not a cover of anything, and importing it would put a wrong image
# on a real book -- worse than no image at all.
JUNK_COVER_NAMES = {"isbn-0.jpg"}


def load_cover_map(csv_path: Path) -> dict[int, str]:
    # A dict, so the handful of book ids the CSV lists twice resolve to the
    # last row deterministically rather than to whichever one happened to win.
    mapping: dict[int, str] = {}
    with csv_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            try:
                book_id = int((row.get("id") or "").strip())
            except ValueError:
                continue
            image_name = (row.get("image_name") or "").strip()
            if image_name in JUNK_COVER_NAMES:
                continue
            if book_id > 0 and image_name:
                mapping[book_id] = image_name
    return mapping


def import_covers(
    target: sqlite3.Connection,
    covers_dir: Path,
    cover_map: dict[int, str],
    data_dir: Path,
) -> tuple[int, int, int]:
    copied = 0
    missing = 0
    skipped = 0

    # Everything under books/ goes first. Book ids are reused across imports,
    # so a cover left behind from a previous catalog would be silently adopted
    # by whatever book now holds that id -- the wrong picture on a real record,
    # with nothing to indicate it.
    books_root = data_dir / "books"
    if books_root.is_dir():
        for entry in books_root.iterdir():
            # .gitkeep is tracked and holds the directory in the repository.
            if entry.name == ".gitkeep":
                continue
            if entry.is_dir():
                shutil.rmtree(entry)
            else:
                entry.unlink()
    books_root.mkdir(parents=True, exist_ok=True)
    target.execute("UPDATE books SET cover_image_path = NULL")
    target.commit()

    book_ids = {
        row[0]
        for row in target.execute("SELECT id FROM books").fetchall()
    }

    for book_id, image_name in cover_map.items():
        if book_id not in book_ids:
            skipped += 1
            continue

        source_file = covers_dir / image_name
        if not source_file.is_file():
            missing += 1
            continue

        dest_dir = data_dir / "books" / str(book_id)
        dest_dir.mkdir(parents=True, exist_ok=True)

        suffix = source_file.suffix.lstrip(".") or "jpg"
        relative = f"books/{book_id}/cover.{suffix}"
        dest_file = data_dir / relative

        shutil.copy2(source_file, dest_file)
        target.execute(
            "UPDATE books SET cover_image_path = ?, updated_at = datetime('now') WHERE id = ?",
            (relative, book_id),
        )
        copied += 1

    target.commit()
    return copied, missing, skipped


def import_catalog(
    source_db: Path,
    target_db: Path,
    schema_path: Path,
    covers_dir: Path,
    covers_csv: Path,
    resources_dir: Path,
    *,
    replace: bool,
) -> None:
    if not source_db.is_file():
        raise SystemExit(f"Source database not found: {source_db}")
    if not schema_path.is_file():
        raise SystemExit(f"Schema not found: {schema_path}")
    if not covers_dir.is_dir():
        raise SystemExit(f"Covers directory not found: {covers_dir}")
    if not covers_csv.is_file():
        raise SystemExit(f"Covers CSV not found: {covers_csv}")

    target_db.parent.mkdir(parents=True, exist_ok=True)
    (resources_dir / "books").mkdir(parents=True, exist_ok=True)
    (resources_dir / "members").mkdir(parents=True, exist_ok=True)

    source = sqlite3.connect(source_db)
    target = sqlite3.connect(target_db)
    try:
        target.execute("PRAGMA foreign_keys = ON")

        if not table_exists(target, "members"):
            print(f"Applying schema from {schema_path}")
            apply_schema(target, schema_path)

        if table_exists(target, "books"):
            book_count = target.execute("SELECT COUNT(*) FROM books").fetchone()[0]
            if book_count > 0 and not replace:
                raise SystemExit(
                    f"Target already has {book_count} books. Re-run with --replace to overwrite catalog."
                )
            if replace:
                backup = backup_database(target_db)
                if backup is not None:
                    print(f"Backed up existing database to {backup.name}")
                print("Clearing existing VLMS catalog (and loans)…")
                clear_catalog(target)

        print(f"Importing from {source_db}")
        target.execute("PRAGMA foreign_keys = OFF")
        target.execute("BEGIN")

        authors = copy_table(
            source,
            target,
            "authors",
            ["id", "name", "code"],
        )
        publishers = copy_table(
            source,
            target,
            "publishers",
            ["id", "name"],
        )
        categories = copy_table(
            source,
            target,
            "categories",
            ["id", "code", "label"],
        )

        # VLMS books table has created_at / updated_at; the source may not.
        #
        # isbn is copied exactly as it stands, NULLs included. VLMS's own
        # createBook() stores '' for a blank ISBN so that
        # UNIQUE (title, author_id, publisher_id, isbn, language) can see
        # duplicates -- but applying that convention here would collide on 2591
        # Source rows that only coexist because SQLite treats NULLs in a unique
        # index as distinct. Do not "fix" this.
        #
        # publication_date lands in both columns raw. The normaliser is C++
        # (DateText::normalizePublicationDate) and reimplementing its rules here
        # would fork them; instead the database is left at schema version 2
        # below and the app's 2 -> 3 upgrade does the transform on first open,
        # reading the original column that this fills in.
        book_rows = source.execute(
            """
            SELECT
                id, title, author_id, publisher_id, category_id, isbn,
                publication_date, publication_date, place_of_publication,
                pages, dimensions, language
            FROM books
            """
        ).fetchall()
        target.executemany(
            """
            INSERT INTO books (
                id, title, author_id, publisher_id, category_id, isbn,
                publication_date, publication_date_original, place_of_publication,
                pages, dimensions, language, cover_image_path, created_at, updated_at
            ) VALUES (
                ?, ?, ?, ?, ?, ?,
                ?, ?, ?,
                ?, ?, ?, NULL, datetime('now'), datetime('now')
            )
            """,
            book_rows,
        )
        books = len(book_rows)

        copies = copy_table(
            source,
            target,
            "book_copies",
            [
                "id",
                "book_id",
                "global_copy_id",
                "source",
                "local_id",
                "central_id",
                "classification",
                "subject",
                "notes",
                "inventory_status",
                "compensation",
                "location",
                "index_code",
                "source_row",
            ],
        )

        for table in ("authors", "publishers", "categories", "books", "book_copies"):
            sync_sequence(target, table)

        target.commit()
        target.execute("PRAGMA foreign_keys = ON")

        print(
            f"Catalog imported: authors={authors}, publishers={publishers}, "
            f"categories={categories}, books={books}, copies={copies}"
        )

        # Seed default employee if missing.
        if table_exists(target, "employees"):
            count = target.execute("SELECT COUNT(*) FROM employees").fetchone()[0]
            if count == 0:
                target.execute(
                    """
                    INSERT INTO employees (username, password_hash, first_name, last_name)
                    VALUES ('admin', 'admin', 'Admin', 'User')
                    """
                )
                target.commit()
                print("Created default employee admin/admin")

        cover_map = load_cover_map(covers_csv)
        print(f"Importing covers from {covers_dir} ({len(cover_map)} mappings)…")
        copied, missing, skipped = import_covers(target, covers_dir, cover_map, resources_dir)
        print(f"Covers: copied={copied}, missing_files={missing}, unmatched_ids={skipped}")

        # Deliberately one behind. The imported publication_date columns still
        # hold the cataloguer's raw text, and the app's 2 -> 3 upgrade is what
        # normalises them -- through the same C++ function the editor uses, so
        # the two can never disagree. Stamping the current version here would
        # skip that step and leave the catalog un-normalised.
        target.execute("PRAGMA user_version = 2")
        target.commit()

        with_cover = target.execute(
            "SELECT COUNT(*) FROM books WHERE cover_image_path IS NOT NULL AND TRIM(cover_image_path) != ''"
        ).fetchone()[0]
        print(f"Done. Target: {target_db}")
        print(f"Books with cover_image_path set: {with_cover}")
        print("Schema stamped at version 2; the app normalises publication dates on first open.")
    finally:
        source.close()
        target.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-db", type=Path, default=DEFAULT_SOURCE_DB)
    parser.add_argument("--target-db", type=Path, default=DEFAULT_TARGET_DB)
    parser.add_argument("--resources-dir", type=Path, default=DEFAULT_RESOURCES_DIR)
    parser.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    parser.add_argument("--covers-dir", type=Path, default=DEFAULT_COVERS_DIR)
    parser.add_argument("--covers-csv", type=Path, default=DEFAULT_COVERS_CSV)
    parser.add_argument(
        "--replace",
        action="store_true",
        help="Replace existing VLMS catalog (also deletes loans that reference copies).",
    )
    args = parser.parse_args()

    import_catalog(
        args.source_db,
        args.target_db,
        args.schema,
        args.covers_dir,
        args.covers_csv,
        args.resources_dir,
        replace=args.replace,
    )


if __name__ == "__main__":
    main()
