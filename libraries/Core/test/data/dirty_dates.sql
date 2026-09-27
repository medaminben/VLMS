-- The rows that make D4's pre-flight refuse to migrate, one per constraint.
--
-- A fragment, not a database: it is applied on top of pre_constraint_dates.sql
-- so that the dirty data is a readable list rather than something to hunt for
-- in a hundred lines of CREATE TABLE. The schema it needs is the one that file
-- declares, and it is legal there precisely because the constraints are what
-- this fixture exists to be refused by.
--
-- Every value here is the kind of thing a real catalog contains: dates typed
-- in the local dd/mm/yyyy order, a year on its own, a month that has no 30th,
-- a duplicate loan created by a double click.

-- date('14/08/2020') is NULL: unreadable, and until D3a invisible to the
-- overdue filter, the is_overdue flag and the KPI at the same time.
INSERT INTO loans (id, member_id, book_copy_id, borrowed_at, due_at, returned_at)
VALUES (20, 3, 12, '2020-06-01', '14/08/2020', NULL);

-- date('2019') is '-4707-05-30', a Julian day. Not NULL, which is why a
-- `date(x) IS NOT NULL` guard would have accepted it.
INSERT INTO loans (id, member_id, book_copy_id, borrowed_at, due_at, returned_at)
VALUES (21, 8, 13, '2019', '2019-01-15', '2019-01-14');

-- February has no 30th. date() rolls it over to 2024-03-01 rather than
-- refusing it, so this too survives a NOT NULL test.
INSERT INTO loans (id, member_id, book_copy_id, borrowed_at, due_at, returned_at)
VALUES (22, 3, 13, '2024-02-30', '2024-03-15', NULL);

-- Borrowed and due the same day: a loan with no loan period.
INSERT INTO loans (id, member_id, book_copy_id, borrowed_at, due_at, returned_at)
VALUES (23, 8, 11, '2025-03-03', '2025-03-03', '2025-03-03');

-- Two open loans on copy 12, which is what the partial unique index forbids.
INSERT INTO loans (id, member_id, book_copy_id, borrowed_at, due_at, returned_at)
VALUES (24, 8, 12, '2025-04-01', '2025-04-15', NULL);

-- A member born on a date that does not exist.
INSERT INTO members (id, membership_number, first_name, last_name, sex, date_of_birth, status, registered_at, updated_at)
VALUES (12, 'M-0012', 'Salma', 'Gharbi', 'female', '1900-02-29', 'active', '2025-06-14', '2025-06-14');
