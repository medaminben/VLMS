-- VLMS: standalone desktop library schema (SQLite)

PRAGMA foreign_keys = ON;

-- ---------------------------------------------------------------------------
-- Staff (app users — employers only)
-- ---------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS employees (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    first_name TEXT NOT NULL,
    last_name TEXT NOT NULL,
    is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
    created_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    last_login_at TEXT
);

-- ---------------------------------------------------------------------------
-- Library members (customers — no app login)
-- Status workflow:
--   subscribed   — registered, subscription fee paid
--   active       — in good standing, may borrow
--   non_active   — fees due, borrowing blocked
--   unsubscribed — membership ended
-- ---------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS members (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    membership_number TEXT NOT NULL UNIQUE,
    first_name TEXT NOT NULL,
    last_name TEXT NOT NULL,
    sex TEXT CHECK (sex IS NULL OR sex IN ('male', 'female')),
    -- A round trip through date(), not `date(x) IS NOT NULL`: SQLite reads a
    -- bare '1990' as a Julian day and rewrites '2024-02-30' to '2024-03-01',
    -- so both are NOT NULL and both are wrong. Comparing the result back
    -- against the input rejects anything it silently changed. IS, not =,
    -- because date('1999-09') = '1999-09' is NULL, and a CHECK only fails on
    -- FALSE -- so = would let it through. NULL round-trips to NULL, which is
    -- what a nullable column wants.
    date_of_birth TEXT CHECK (date(date_of_birth) IS date_of_birth),
    phone TEXT,
    address TEXT,
    city TEXT,
    status TEXT NOT NULL DEFAULT 'subscribed'
        CHECK (status IN ('subscribed', 'active', 'non_active', 'unsubscribed')),
    photo_path TEXT,
    id_image_path TEXT,
    notes TEXT,
    registered_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    -- Last on purpose, not beside phone where it reads better: an existing
    -- database gets this column from ALTER TABLE, which can only append. Put
    -- it anywhere else and a fresh database and a migrated one would hold the
    -- same data in a different column order. The same goes for archived_at,
    -- which is why it sits after email rather than beside the other dates.
    email TEXT,
    -- Set when a librarian retires a member instead of deleting the row. The
    -- row stays where it is precisely so loans.member_id keeps resolving: a
    -- member with borrowing history cannot be deleted without taking that
    -- history with them, and the history is what the metrics are counted from.
    -- NULL is an active member; every list in the application filters on it.
    archived_at TEXT CHECK (datetime(archived_at) IS archived_at),
    -- Appended after archived_at so a v4 ALTER and a brand-new database hold
    -- these columns in the same order. occupation and age_group come from the
    -- public-library subscriber sheet; age itself is not stored.
    occupation TEXT,
    age_group TEXT CHECK (age_group IS NULL OR age_group IN ('youth', 'adult')),
    -- Exact Excel "الإسم واللقب" so a later loan import can match المستعير.
    full_name TEXT,
    source_row INTEGER
);

CREATE INDEX IF NOT EXISTS idx_members_archived ON members(archived_at);

CREATE INDEX IF NOT EXISTS idx_members_name ON members(last_name, first_name);
CREATE INDEX IF NOT EXISTS idx_members_status ON members(status);
CREATE INDEX IF NOT EXISTS idx_members_number ON members(membership_number);

-- ---------------------------------------------------------------------------
-- Catalog (aligned with the source catalog — no reviews, no member notes)
-- ---------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS authors (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    code TEXT,
    UNIQUE (name, code)
);

CREATE TABLE IF NOT EXISTS publishers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS categories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    code TEXT NOT NULL UNIQUE,
    label TEXT
);

CREATE TABLE IF NOT EXISTS books (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    author_id INTEGER REFERENCES authors(id),
    publisher_id INTEGER REFERENCES publishers(id),
    category_id INTEGER REFERENCES categories(id),
    isbn TEXT,
    -- Reduced-precision ISO 8601 where the value could be read: YYYY,
    -- YYYY-MM or YYYY-MM-DD, all three of which sort correctly as text.
    -- Deliberately not constrained: a publication date column legitimately
    -- holds MARC notation for an uncertain decade ('201u', '195?') and Hijri
    -- years, and those are catalog data rather than corruption.
    publication_date TEXT,
    -- What the cataloguer actually typed. Normalisation discards wording and
    -- some of what it rewrites is genuinely ambiguous, so the original is kept
    -- beside it and the whole transform stays reversible with one UPDATE.
    publication_date_original TEXT,
    place_of_publication TEXT,
    pages TEXT,
    dimensions TEXT,
    language TEXT NOT NULL,
    description TEXT,
    cover_image_path TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    -- Set when Catalog Delete moves the title to the Archive. Last so an ALTER
    -- on an older database and a fresh one hold the columns in the same order.
    archived_at TEXT CHECK (datetime(archived_at) IS archived_at),
    UNIQUE (title, author_id, publisher_id, isbn, language)
);

CREATE INDEX IF NOT EXISTS idx_books_title ON books(title);
CREATE INDEX IF NOT EXISTS idx_books_category ON books(category_id);
CREATE INDEX IF NOT EXISTS idx_books_author ON books(author_id);
CREATE INDEX IF NOT EXISTS idx_books_archived ON books(archived_at);

CREATE TABLE IF NOT EXISTS book_copies (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
    -- Derived from the local number (AR-<local_id>, FR-<local_id>) and moved
    -- with it: both are NULL on an archived copy that gave its number away.
    -- SQLite treats NULLs as distinct, so any number of those may coexist.
    global_copy_id TEXT UNIQUE,
    source TEXT NOT NULL CHECK (source IN ('arabic', 'foreign')),
    local_id TEXT,
    central_id TEXT,
    classification TEXT,
    subject TEXT,
    notes TEXT,
    inventory_status TEXT,
    compensation TEXT,
    location TEXT,
    index_code TEXT,
    source_row INTEGER,
    archived_at TEXT CHECK (datetime(archived_at) IS archived_at),
    UNIQUE (source, local_id)
);

CREATE INDEX IF NOT EXISTS idx_copies_book ON book_copies(book_id);
CREATE INDEX IF NOT EXISTS idx_copies_local ON book_copies(source, local_id);
CREATE INDEX IF NOT EXISTS idx_copies_central ON book_copies(central_id);
CREATE INDEX IF NOT EXISTS idx_copies_archived ON book_copies(archived_at);

-- ---------------------------------------------------------------------------
-- Circulation
-- ---------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS loans (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id INTEGER NOT NULL REFERENCES members(id),
    book_copy_id INTEGER NOT NULL REFERENCES book_copies(id),
    borrowed_at TEXT NOT NULL DEFAULT (date('now', 'localtime')),
    due_at TEXT NOT NULL,
    returned_at TEXT,
    borrowed_by_employee_id INTEGER REFERENCES employees(id),
    returned_by_employee_id INTEGER REFERENCES employees(id),
    notes TEXT,
    -- Set when Circulation Delete moves a returned loan to the Archive.
    archived_at TEXT CHECK (datetime(archived_at) IS archived_at),
    -- See the note on members.date_of_birth for why every date guard here is a
    -- round trip rather than a NOT NULL test.
    CHECK (date(borrowed_at) IS borrowed_at),
    CHECK (date(due_at) IS due_at),
    CHECK (date(returned_at) IS returned_at),
    -- A loan due the day it was borrowed is not a loan.
    CHECK (date(due_at) > date(borrowed_at)),
    -- Replaces a raw string comparison, which agreed with this one only for as
    -- long as both sides happened to be ISO.
    CHECK (returned_at IS NULL OR date(returned_at) >= date(borrowed_at))
);

CREATE INDEX IF NOT EXISTS idx_loans_member ON loans(member_id);
CREATE INDEX IF NOT EXISTS idx_loans_copy ON loans(book_copy_id);
CREATE INDEX IF NOT EXISTS idx_loans_borrowed_at ON loans(borrowed_at);
CREATE INDEX IF NOT EXISTS idx_loans_open ON loans(returned_at);
CREATE INDEX IF NOT EXISTS idx_loans_archived ON loans(archived_at);

-- One physical copy can be out on one loan at a time. createLoan checks this
-- before inserting, but a check-then-act is only as good as the constraint
-- under it: anything writing to loans without going through the repository,
-- and any future concurrent writer, could produce a copy on loan twice.
CREATE UNIQUE INDEX IF NOT EXISTS idx_loans_one_open_per_copy
    ON loans(book_copy_id) WHERE returned_at IS NULL;

-- ---------------------------------------------------------------------------
-- Audit / metrics helpers
-- ---------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS member_status_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    old_status TEXT,
    new_status TEXT NOT NULL,
    changed_at TEXT NOT NULL DEFAULT (datetime('now', 'localtime')),
    changed_by_employee_id INTEGER REFERENCES employees(id),
    note TEXT
);

CREATE INDEX IF NOT EXISTS idx_status_history_member ON member_status_history(member_id);

-- ---------------------------------------------------------------------------
-- Schema version
--
-- Last, so a database that fails partway through applying this file is left at
-- 0 and gets sniffed by the legacy detectors on the next launch rather than
-- claiming a shape it does not have.
--
-- Must equal Database::kSchemaVersion; tst_database_schema reads this line and
-- asserts it. Everything written before this pragma existed reports 0.
-- ---------------------------------------------------------------------------

PRAGMA user_version = 6;

-- v6 rows for the v7 migration: one registered exactly a year before
-- 2026-09-24, one a day earlier, one on a leap day.
INSERT INTO members (id, membership_number, first_name, last_name, status, registered_at)
VALUES (1, '1', 'Amina', 'Ben Salah', 'active', '2025-09-24 10:00:00'),
       (2, '2', 'Karim', 'Trabelsi', 'active', '2025-09-23 09:00:00'),
       (3, '3', 'Leila', 'Haddad', 'non_active', '2024-02-29 12:00:00');
INSERT INTO member_status_history (member_id, old_status, new_status)
VALUES (1, NULL, 'active'), (2, NULL, 'active'), (3, NULL, 'non_active');
