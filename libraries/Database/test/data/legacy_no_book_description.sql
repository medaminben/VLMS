-- Shape 3: everything is current except that `books` predates the description
-- field.
--
-- migrateBookDescriptionIfNeeded() detects this with PRAGMA table_info and
-- fixes it with ALTER TABLE ADD COLUMN, so unlike shape 2 there is no table
-- rebuild and the existing rows are never rewritten. The test asserts the
-- column arrives, the rows are untouched, and every existing description reads
-- back as NULL rather than an empty string.

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
    sex TEXT CHECK (sex IS NULL OR sex IN ('male', 'female')),
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

INSERT INTO members (id, membership_number, first_name, last_name, sex, registered_at, updated_at)
VALUES (1, 'M-0001', 'Amina', 'Ben Salah', 'female', '2025-06-14', '2025-06-14');

CREATE TABLE authors (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    code TEXT,
    UNIQUE (name, code)
);

INSERT INTO authors (id, name, code) VALUES (1, 'Ibn Khaldun', 'IK');

CREATE TABLE publishers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE
);

INSERT INTO publishers (id, name) VALUES (1, 'Dar al-Kutub');

CREATE TABLE categories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    code TEXT NOT NULL UNIQUE,
    label TEXT
);

INSERT INTO categories (id, code, label) VALUES (1, '900', 'History');

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
    cover_image_path TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE (title, author_id, publisher_id, isbn, language)
);

INSERT INTO books (id, title, author_id, publisher_id, category_id, isbn, publication_date, language, created_at, updated_at)
VALUES
    (4, 'Al-Muqaddima', 1, 1, 1, '9789953890128', '2014', 'ar', '2025-06-14 09:00:00', '2025-06-14 09:00:00'),
    (9, 'Histoire de la Tunisie', 1, 1, 1, '9782070360024', 'February 2001', 'fr', '2025-06-14 09:05:00', '2025-06-14 09:05:00');

CREATE INDEX idx_books_title ON books(title);
