# Backend library split: Core, Database, Repositories

Approved 2026-09-28. First step of the Core refactor. It moves files between
libraries and changes nothing else: no namespace changes, no API changes, no
behaviour changes. Namespace clean-up and splitting the large repositories
follow as separate specs, inside this layout.

## Why

`vlms_core` holds four unrelated layers in one target: std-only basics,
SQLite access and migrations, the domain types and repositories, and the
UI string tables. Nothing stops a basic utility from reaching into SQLite,
or a migration from reaching into a repository. After the split, each of
those boundaries is enforced by the build: a wrong include fails to compile
or link.

## Dependency graph

```
vlms_ui ──▶ VLMS::Repositories ──▶ VLMS::Database ──▶ VLMS::Core
   └──(PRIVATE)──▶ VLMS::Ocr                 └──(PRIVATE)──▶ sqlite3
```

- Each library depends only on the ones to its right, as `PUBLIC` links.
- **Core** links only std. It stops linking sqlite3.
- **Database** is the only library that links sqlite3, and links it
  `PRIVATE`. `sqlite3_api.h` stays in `Database/src`.
- **Repositories** see SQLite only through `SqliteSession` /
  `SqliteStatement`.
- **Ocr** is unchanged and still depends on nothing in the project.

## Targets and layout

| Library | Directory | Raw target | Alias | Include prefix |
|---|---|---|---|---|
| Core | `libraries/Core` | `vlms_core` | `VLMS::Core` | `VLMS/Core/` |
| Database | `libraries/Database` | `vlms_database` | `VLMS::Database` | `VLMS/Database/` |
| Repositories | `libraries/Repositories` | `vlms_repositories` | `VLMS::Repositories` | `VLMS/Repositories/` |
| Ocr | `libraries/Ocr` | `vlms_ocr` | `VLMS::Ocr` | `VLMS/Ocr/` |

Each new `CMakeLists.txt` follows the Core/Ocr pattern it replaces:

- `TARGET_NAME` / `RAW_TARGET_NAME`
- an `ALIAS` target
- `BUILD_INTERFACE` / `INSTALL_INTERFACE` include dirs
- `vlms_configure_target`
- AUTOMOC/UIC/RCC off
- `install()`
- `add_subdirectory(test)` under `BUILD_TESTING`

The `*_HEADERS` list names every public header. The current Core list is
missing about eight headers; the new lists are complete.

`libraries/CMakeLists.txt` adds the subdirectories in dependency order:
Ocr, Core, Database, Repositories.

## File placement

Moves use `git mv`, so each file's history follows it.

### Core (stays in `libraries/Core`)

Public (`include/VLMS/Core/`): `Result.h`, `Date.h`, `Clock.h`,
`DateText.h`, `Paths.h`, `Locale.h`, `Strings.h`, **`Text.h`**.

Sources: `Date.cpp`, `Clock.cpp`, `DateText.cpp`, `Paths.cpp`,
`Locale.cpp`, `Strings.cpp`.

`Text.h` moves from `src/` to `include/VLMS/Core/` because every library
uses `trim` / `replaceAll`. Includes change from `"Text.h"` to
`<VLMS/Core/Text.h>`.

`Strings` and `Locale` stay in Core. `BookCopyStore` writes translated
copy notes into the database through `Strings::t()`, so Repositories need
them. Moving the string tables out is a separate decision.

### Database (new, `libraries/Database`)

Public (`include/VLMS/Database/`):

- `Database.h`
- `SqliteSession.h`: becomes public, because the repositories take a
  `SqliteSession&`
- `SqlText.h`

Private (`src/`): `sqlite3_api.h`.

Sources: `Database.cpp`, `SqliteSession.cpp`, `SqlText.cpp`.

`cmake/Sqlite.cmake` is included from here, not from Core. Its comment
that points at `libraries/Core/src/sqlite3_api.h` is updated.

### Repositories (new, `libraries/Repositories`)

Public (`include/VLMS/Repositories/`):

- `CatalogRepository.h`, `MemberRepository.h`,
  `CirculationRepository.h`, `MetricsRepository.h`
- `CatalogTypes.h`, `MemberTypes.h`, `LoanTypes.h`, `MetricsTypes.h`,
  `ArchiveTypes.h`
- `LoanPolicy.h`

Private (`src/`): `RepoSql.h`, `BookSql.*`, `MemberSql.*`, `LoanSql.*`,
`NamedEntityStore.*`, `BookCopyStore.*`, `CategoryStore.*`.

Sources: the four `*Repository.cpp` files and `LoanPolicy.cpp`, plus the
private sources above.

### Compile definitions

Development builds bake two paths in today. Each one moves to the library
that reads it:

- `VLMS_PROJECT_ROOT` stays on **Core** (`Paths.cpp` reads it).
- `VLMS_SCHEMA_PATH` moves to **Database** (`Database.cpp` reads it).

The `VLMS_DEV_PATHS` option and its Windows-Release override are defined
once, at the top level or in a shared cmake file, so both libraries see
the same value.

## Tests

Each library gets its own `test/` directory and binary, built with
`build_gtest_executable` as today. Suite names (`test_core_*`) are not
renamed, so the existing `--gtest_filter` values keep working.

| Binary | Label | Tests |
|---|---|---|
| `test_vlms_core` | `core` | result, clock, date_text, licence_strings |
| `test_vlms_database` | `database` | sqlite_session, sql_text, sql_script, database_schema, database_migrations, database_file; realdb entry |
| `test_vlms_repositories` | `repositories` | every other current Core test, including strings_parity (it uses `MemberRepository`) and circulation_timezone (keeps its timezone entries) |

The real-database entry (`test_vlms_core_realdb`, labels
`realdb;dates`) moves to the Database binary and becomes
`test_vlms_database_realdb`. The timezone entries move with
`test_circulation_timezone` to the Repositories binary. Each binary gets
its own `GTEST_FILTER` that excludes only the suites it actually contains.

### Test support, split by dependency

| Library | Directory | Contents | Links |
|---|---|---|---|
| `vlms_testsupport_core` | `libraries/Core/test/support` | `TestEnv` | `VLMS::Core`, GTest |
| `vlms_testsupport_database` | `libraries/Database/test/support` | `TestDatabase`, `SqlValue.h` | the one above, `VLMS::Database` |
| `vlms_testsupport` | `libraries/Repositories/test/support` | `TestSeed` | the one above, `VLMS::Repositories` |

`vlms_testsupport` keeps its name, so the UI test target still links it
without changes. No support library adds a private `src/` include path any
more, because `SqliteSession.h` is now public.

### Test data

`libraries/Core/test/data/` holds migration fixtures, so it moves to
`libraries/Database/test/data/`. `VLMS_TEST_DATA_DIR` in
`cmake/TestUtils.cmake` (both places) is updated to match.

## Application

- `vlms_ui` links `VLMS::Repositories` instead of `VLMS::Core`, and gets
  Database and Core through it. Its `PRIVATE` link to Ocr is unchanged.
- About 200 `#include <VLMS/Core/X.h>` lines are rewritten to the new
  prefix, following the placement tables above. This is mechanical: every
  header name is unique across the three libraries.
- `Application` is unchanged apart from its includes.
- `applications/vlms/manual_capture` gets the same include rewrite.

## Other references

Everything outside the build that names `libraries/Core/...` is updated:

- `cmake/Sqlite.cmake`
- `cmake/TestUtils.cmake`
- `libraries/Core/test/data/README.md`, which moves with the data. Its
  title and its pointer to `Database.cpp` are updated.

`scripts/build_manual.py` and `scripts/release/test_release.py` name
`libraries/Core/src/Strings.cpp`. That file stays where it is, so they are
unchanged.

`docs/architecture/beta-uml.md` describes an older snapshot, so it is not
rewritten here.

## Out of scope

- Namespace changes: global types move to `VLMS::` in the next spec.
- Splitting `CatalogRepository` / `MemberRepository`, and removing the
  duplicate `create*` / `saveNew*` APIs.
- Versioned migrations in `Database`.
- Moving `Strings` / `Locale` out of Core.

## Done when

- A clean configure and build succeeds with the Ninja/Make generator in
  `build/`.
- `ctest` passes with no failures.
- No gtest case is lost: `--gtest_list_tests` summed over
  `test_vlms_core`, `test_vlms_database` and `test_vlms_repositories`
  gives 574, today's `test_vlms_core` count.
- `vlms_core` does not link sqlite3, and has no include path to any other
  library.
- `grep -rn "VLMS/Core/\(Database\|SqliteSession\|SqlText\|.*Repository\|.*Types\|LoanPolicy\)"`
  over `libraries` and `applications` returns nothing.
- The app starts and opens the development database.
