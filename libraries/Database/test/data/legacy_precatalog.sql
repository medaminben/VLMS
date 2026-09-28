-- Shape 1: before the catalog rework.
--
-- There is no `authors` table, and books/categories/loans exist in a flat
-- pre-normalisation form where a book carries its author and publisher as
-- free text. This is what migrateCatalogIfNeeded() detects.
--
-- That migration DROPs loans, book_copies, books and categories and recreates
-- them empty. The rows below exist so the test can pin that data loss
-- explicitly: it is the behaviour of the shipped code, and a test that seeded
-- nothing would hide it.

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

-- Already carries `sex`, so migrateMemberSexIfNeeded() is a no-op here.
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

INSERT INTO members (id, membership_number, first_name, last_name, sex, date_of_birth, registered_at, updated_at)
VALUES
    (1, 'M-0001', 'Amina', 'Ben Salah', 'female', '1990-05-12', '2025-06-14', '2025-06-14'),
    (2, 'M-0002', 'Youssef', 'Trabelsi', 'male', '1984-11-03', '2025-06-14', '2025-06-14');

CREATE TABLE categories (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE
);

INSERT INTO categories (id, name) VALUES (1, 'History');

CREATE TABLE books (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    author TEXT,
    publisher TEXT,
    category_id INTEGER REFERENCES categories(id),
    isbn TEXT,
    publication_date TEXT,
    language TEXT NOT NULL
);

INSERT INTO books (id, title, author, publisher, category_id, isbn, publication_date, language)
VALUES (1, 'Al-Muqaddima', 'Ibn Khaldun', 'Dar al-Kutub', 1, '9789953890128', '2014', 'ar');

CREATE TABLE loans (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id INTEGER NOT NULL REFERENCES members(id),
    book_id INTEGER NOT NULL REFERENCES books(id),
    borrowed_at TEXT NOT NULL,
    due_at TEXT NOT NULL,
    returned_at TEXT
);

INSERT INTO loans (id, member_id, book_id, borrowed_at, due_at)
VALUES (1, 1, 1, '2025-06-20', '2025-07-04');
