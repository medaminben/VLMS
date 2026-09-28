# libraries/Database/test/data

Database shapes that VLMS has actually shipped, one file per shape, plus
the dirty-data fixtures the Phase D migration tests need.

These are not snapshots taken from git: `database/schema.sql` has only ever had
one commit, so the historical shapes are reconstructed from what the
`migrateXIfNeeded` detectors in `libraries/Database/src/Connection.cpp` look for. Each
file is named for the single migration it is meant to trigger and is written so
that the other three detectors find nothing to do — that is what makes a failure
in `tst_database_migrations` point at one migration rather than at the chain.

Two consequences of `Connection::applySchema()` worth knowing before adding a
fixture:

- It gates on the `members` table existing, so any fixture containing `members`
  causes `schema.sql` to be skipped entirely. A fixture therefore has to declare
  every table its test touches; nothing is filled in for it.
- Because of that gate, a table missing from a legacy database is never added by
  opening it. That is pre-existing behaviour, pinned by
  `legacyDatabaseDoesNotGainTablesItNeverHad`, not something Phase D changes.

`TestDatabase::Mode::FromSqlFile` runs these through a throwaway connection
before `Connection::open()` takes the file over, so a fixture must be plain SQL
with no semicolons inside string literals.

One file is not a database: `dirty_dates.sql` is a fragment of INSERTs applied
on top of `pre_constraint_dates.sql`, composed by the test through
`fixtureWith()`. The rows are the subject of those tests, and keeping them in
their own file means they can be read as a list instead of being hunted for
inside a second copy of the schema.
