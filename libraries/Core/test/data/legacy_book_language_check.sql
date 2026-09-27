-- Shape 2: the catalog is normalised, but `books.language` is still pinned to
-- Arabic or French by a CHECK constraint.
--
-- migrateBookLanguageIfNeeded() detects this by looking for the literal text
-- `language IN ('ar', 'fr')` in the stored CREATE TABLE statement, so that
-- spelling has to be preserved exactly. It rebuilds `books` without the
-- constraint, which is a full table rebuild: ids, rows and the copies hanging
-- off them all have to come through unchanged.
--
-- `books` deliberately has no `description` column. The rebuild creates one,
-- so migrateBookDescriptionIfNeeded() runs next and finds nothing to do --
-- one migration per fixture.

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
    language TEXT NOT NULL CHECK (language IN ('ar', 'fr')),
    cover_image_path TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE (title, author_id, publisher_id, isbn, language)
);

-- Row ids are non-contiguous on purpose: a rebuild that renumbers rows instead
-- of carrying the ids over would break every book_copies.book_id below, and a
-- 1,2,3 sequence would let that pass unnoticed.
INSERT INTO books (id, title, author_id, publisher_id, category_id, isbn, publication_date, language, created_at, updated_at)
VALUES
    (4, 'Al-Muqaddima', 1, 1, 1, '9789953890128', '2014', 'ar', '2025-06-14 09:00:00', '2025-06-14 09:00:00'),
    (9, 'Histoire de la Tunisie', 1, 1, 1, '9782070360024', 'February 2001', 'fr', '2025-06-14 09:05:00', '2025-06-14 09:05:00');

CREATE INDEX idx_books_title ON books(title);
CREATE INDEX idx_books_category ON books(category_id);
CREATE INDEX idx_books_author ON books(author_id);

CREATE TABLE book_copies (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
    global_copy_id TEXT NOT NULL UNIQUE,
    source TEXT NOT NULL CHECK (source IN ('arabic', 'foreign')),
    local_id TEXT NOT NULL,
    central_id TEXT,
    classification TEXT,
    subject TEXT,
    notes TEXT,
    inventory_status TEXT,
    compensation TEXT,
    location TEXT,
    index_code TEXT,
    source_row INTEGER,
    UNIQUE (source, local_id)
);

INSERT INTO book_copies (id, book_id, global_copy_id, source, local_id)
VALUES
    (1, 4, 'arabic-1', 'arabic', '1'),
    (2, 4, 'arabic-2', 'arabic', '2'),
    (3, 9, 'foreign-1', 'foreign', '1');

CREATE TABLE loans (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id INTEGER NOT NULL REFERENCES members(id),
    book_copy_id INTEGER NOT NULL REFERENCES book_copies(id),
    borrowed_at TEXT NOT NULL DEFAULT (date('now')),
    due_at TEXT NOT NULL,
    returned_at TEXT,
    borrowed_by_employee_id INTEGER REFERENCES employees(id),
    returned_by_employee_id INTEGER REFERENCES employees(id),
    notes TEXT,
    CHECK (returned_at IS NULL OR returned_at >= borrowed_at)
);

INSERT INTO loans (id, member_id, book_copy_id, borrowed_at, due_at)
VALUES (1, 1, 1, '2025-06-20', '2025-07-04');
