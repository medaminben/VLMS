-- Shape 5: current in every respect except that the date CHECK constraints
-- have not been applied yet, and every date in it is clean.
--
-- This is what the production database looked like going into Phase D: past
-- the four legacy migrations, no PRAGMA user_version, `loans` still guarded by
-- nothing but a raw string comparison of returned_at against borrowed_at.
--
-- All four legacy detectors are quiet here on purpose -- authors exists, books
-- has description and no language CHECK, members has sex -- so anything this
-- fixture proves is about migrateDateConstraintsIfNeeded and nothing else.

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

-- Non-contiguous ids, so a rebuild that renumbered would break the loans below.
INSERT INTO members (id, membership_number, first_name, last_name, sex, date_of_birth, status, registered_at, updated_at)
VALUES
    (3, 'M-0003', 'Amina', 'Ben Salah', 'female', '1990-05-12', 'active', '2025-06-14', '2025-06-14'),
    (8, 'M-0008', 'Youssef', 'Trabelsi', 'male', NULL, 'active', '2025-06-14', '2025-06-14');

CREATE TABLE member_status_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    member_id INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
    old_status TEXT,
    new_status TEXT NOT NULL,
    changed_at TEXT NOT NULL DEFAULT (datetime('now')),
    changed_by_employee_id INTEGER REFERENCES employees(id),
    note TEXT
);

-- ON DELETE CASCADE from members. The rebuild drops `members`, so if foreign
-- keys were left on for it these rows would go with it and nothing would say
-- so; constraintMigrationDoesNotCascadeAwayStatusHistory watches this.
INSERT INTO member_status_history (id, member_id, old_status, new_status, changed_at)
VALUES
    (1, 3, 'subscribed', 'active', '2025-06-15 10:00:00'),
    (2, 8, 'subscribed', 'active', '2025-06-15 10:05:00');

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
    description TEXT,
    cover_image_path TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now')),
    UNIQUE (title, author_id, publisher_id, isbn, language)
);

INSERT INTO books (id, title, author_id, publisher_id, category_id, isbn, publication_date, language)
VALUES
    (4, 'Al-Muqaddima', 1, 1, 1, '9789953890128', '2014', 'ar'),
    (9, 'Histoire de la Tunisie', 1, 1, 1, '9782070360024', 'February 2001', 'fr');

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
    (11, 4, 'arabic-1', 'arabic', '1'),
    (12, 4, 'arabic-2', 'arabic', '2'),
    (13, 9, 'foreign-1', 'foreign', '1');

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

-- One closed loan, one open loan, and a second closed loan on the copy that is
-- currently out: legal under the partial unique index, and the row that proves
-- the index is partial rather than plain.
INSERT INTO loans (id, member_id, book_copy_id, borrowed_at, due_at, returned_at, notes)
VALUES
    (2, 3, 11, '2025-06-20', '2025-07-04', '2025-06-30', 'returned early'),
    (5, 3, 11, '2025-07-10', '2025-07-24', NULL, NULL),
    (6, 8, 13, '2025-05-01', '2025-05-15', '2025-05-15', NULL);

CREATE INDEX idx_loans_member ON loans(member_id);
CREATE INDEX idx_loans_copy ON loans(book_copy_id);
CREATE INDEX idx_loans_borrowed_at ON loans(borrowed_at);
CREATE INDEX idx_loans_open ON loans(returned_at);
