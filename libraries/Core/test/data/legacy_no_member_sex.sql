-- Shape 4: everything is current except that `members` predates the sex field.
--
-- migrateMemberSexIfNeeded() adds the column with its CHECK constraint through
-- ALTER TABLE. The interesting part is the constraint: SQLite attaches a CHECK
-- added this way to the table for good, so after the migration a bad value has
-- to be refused on write, and the existing rows -- which have no value at all
-- -- have to remain legal as NULL.

CREATE TABLE employees (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    first_name TEXT NOT NULL,
    last_name TEXT NOT NULL,
    is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    last_login_at TEXT
);

INSERT INTO employees (id, username, password_hash, first_name, last_name)
VALUES (1, 'admin', 'admin', 'Admin', 'User');

CREATE TABLE members (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    membership_number TEXT NOT NULL UNIQUE,
    first_name TEXT NOT NULL,
    last_name TEXT NOT NULL,
    date_of_birth TEXT,
    phone TEXT,
    address TEXT,
    city TEXT,
    status TEXT NOT NULL DEFAULT 'subscribed'
        CHECK (status IN ('subscribed', 'active', 'non_active', 'unsubscribed')),
    photo_path TEXT,
    id_image_path TEXT,
    notes TEXT,
    registered_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);

INSERT INTO members (id, membership_number, first_name, last_name, date_of_birth, registered_at, updated_at)
VALUES
    (1, 'M-0001', 'Amina', 'Ben Salah', '1990-05-12', '2025-06-14', '2025-06-14'),
    (7, 'M-0007', 'Youssef', 'Trabelsi', '1984-11-03', '2025-06-14', '2025-06-14');

CREATE INDEX idx_members_name ON members(last_name, first_name);

CREATE TABLE authors (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    code TEXT,
    UNIQUE (name, code)
);

CREATE TABLE publishers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE
);

CREATE TABLE categories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    code TEXT NOT NULL UNIQUE,
    label TEXT
);

CREATE TABLE books (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    author_id INTEGER REFERENCES authors(id),
    publisher_id INTEGER REFERENCES publishers(id),
    category_id INTEGER REFERENCES categories(id),
    isbn TEXT,
    publication_date TEXT,
    place_of_publication TEXT,
    pages TEXT,
    dimensions TEXT,
    language TEXT NOT NULL,
    description TEXT,
    cover_image_path TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE (title, author_id, publisher_id, isbn, language)
);
