# VLMS — Test Suite + Security & Date Hardening

> **Historical record.** Links point at where each file lives now (after the
> Core / Database / Repositories split), but line numbers and the prose describe
> the code as it was when this plan ran.

> **Status — all four phases complete.**
> Work lives on branch `Beta` (19 commits, one per step). Current suite:
> **980 cases across 20 CTest entries, 0 failures, 0 XFAILs**, green in all three
> CI configurations (default, `BUILD_APPS=OFF`, ASan/UBSan), plus a 21st entry
> (`tst_database_realdb`, 30 cases) that runs against a real catalog and is excluded
> from CI by label.
>
> **Every XFAIL is gone.** The defect list `ctest -V | grep XFAIL` produced is now
> empty; each of the 8 that survived Phase C was closed by a Phase D commit, and every
> characterization test that carried one is now an ordinary passing test that still
> describes the defect it was written for.
>
> Residual-risk greps over the repository layer all return **zero**: no `date('now')`
> or `datetime('now')`, no `QDate::currentDate()` outside `Clock.cpp`, no `exec()`
> taking a variable, no `.arg()` feeding a prepared statement.
>
> Verified against the live catalog: 3067 books, 2145 authors, 972 categories,
> 13 members and 45 loans migrate with every row count and every `MAX(id)` unchanged,
> `integrity_check = ok`, `foreign_key_check` empty, `user_version` at 1, and the source
> file's SHA-256 unchanged. The publication-date split measured **3043 normalized /
> 24 verbatim** (the plan estimated 3040/27 before the parser existed).
>
> Remaining gap: the **manual smoke test** called for after C5/C6 has still not been run.
> Everything it would cover is under automated test, but nobody has clicked through a
> checkout, an extension and a return in the running app.
>
> Everything below is the plan as approved. Sections carry inline corrections made while
> implementing; the **Deviations** section at the end records where the delivered code
> departs from the plan text.

## Context

VLMS is a Qt6 Widgets desktop library-management app (C++20, SQLite via QtSql) for a single-site library in Ksour Essef, Tunisia (UTC+1). It has **zero tests** — no `tests/` directory, no CTest, no `enable_testing()`. The only CI workflow builds the Windows installer and never compiles a test.

An audit of the data layer produced two conclusions:

**SQL injection: nothing exploitable.** Every user-controlled value reaches SQL through `bindValue()`. Dynamic `IN` clauses build *placeholder names only* (`":category_code_%1".arg(i)` where `i` is a loop index) then bind the values. `escapeLike()` escapes `\`, `%`, `_` in the correct order. `ORDER BY` is never variable; `LIMIT`/`OFFSET` are always bound. Across the repositories: 44 `prepare()`, 120 `bindValue()`, 23 `exec(<string>)` — of which 21 are inline literals and 0 involve user input. **Two latent holes** remain that are safe only by call-site discipline, and both should be closed by type.

**Date handling: a real defect cluster.** Loan dates are produced in **local** time (`QDate::currentDate()`) but every SQL predicate compares them against **UTC** (`date('now')`). At UTC+1 a loan due today isn't flagged overdue until 01:00 the next day, and "today's checkouts" can miss a loan just created. On top of that, `createLoan` has no date-parse guard, malformed dates make rows silently *vanish* from overdue queries rather than error, metrics windows have no upper bound so future-dated rows count in all three periods forever, and the loan-period policy lives in the dialogs rather than in Core.

The outcome: a green, CI-gated test suite that pins current correct behavior as regression armor, documents every known defect as an XFAIL, and then removes each XFAIL with a targeted fix.

---

## Decisions

| # | Decision | Source |
|---|---|---|
| 1 | **Local time everywhere.** SQL `date('now')` → a bound date from a Core `Clock`. | user |
| 2 | **Test scope = Core + UI dialog rules**, headless via `QT_QPA_PLATFORM=offscreen`. | user |
| 3 | **Code guards + DB CHECK constraints**, migration transactional and pre-flighted. | user |
| 4 | **`publication_date` normalized to ISO where parseable.** | user |

Resolved from the production database (`database/vlms.db.bak-20260814-232925`, 9.7 MB, `integrity_check = ok`, `user_version = 0`):

- **`loans` is empty (0 rows).** The loan CHECK-constraint migration is zero-risk. No pre-flight failure is possible.
- **`members`: 13 rows, every `date_of_birth` valid ISO.** Safe to constrain strictly.
- **`registered_at` stores bare dates** (`'2025-06-14'`), not the schema's `datetime('now')` — rows were imported, not app-created. The UTC-vs-local backfill question is therefore **moot**: there is no time component to shift. No data migration on that column.
- **`books`: 3067 rows, 30 distinct `publication_date` shapes**, 0 NULL and 0 empty.

### ⚠️ Two data facts that constrain the design

**`date()` is not a validity test.** SQLite parses a bare number as a *Julian day*:

```
date('2014')       → '-4707-05-30'      ← NOT NULL, silently garbage
date('2003-03-18') → '2003-03-18'
date('1999-09')    → NULL
date('2024-02-30') → '2024-03-01'       ← NOT NULL, silently rewritten
date('1900-02-29') → '1900-03-01'       ← 1900 was not a leap year
```

So `CHECK (date(borrowed_at) IS NOT NULL)` would **accept `'2014'`** as a loan date.

> **Corrected during Phase B** (`tst_circulation_dates::roundTripIsTheOnlySoundDateGuard`, 10 data rows). The originally planned pair — `GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]' AND date(x) IS NOT NULL` — is **not sufficient**. `date()` does not reject an impossible calendar date; within a month it **rolls over**, so `'2024-02-30'` and `'1900-02-29'` are ISO-shaped, survive `date() IS NOT NULL`, and get stored verbatim. It rejects only a month above 12 or a day above 31, which is what makes the rollover easy to miss.

The guard that actually works is a **round trip**:

```sql
CHECK (date(borrowed_at) IS borrowed_at)
```

Two things make it correct where the alternatives are not:

- It compares `date(x)` **back to `x`**, so anything SQLite silently rewrote — a Julian day, a rolled-over 30 February — cannot pass.
- It uses **`IS`, not `=`**. A SQLite CHECK is violated only by a FALSE result, and `date('1999-09') = '1999-09'` is NULL — which **passes**. `IS` yields 0 there, which fails as intended.

It is never weaker than the GLOB pair on any actual text, so it fully replaces it; the GLOB is redundant. It also accepts a genuine NULL, which is exactly what a nullable date column wants: `returned_at` and `date_of_birth` need no `IS NULL OR ...` wrapper, and a NOT NULL column is already covered by its own NOT NULL.

This same trap invalidates any C++ guard that tests only `QDate::fromString(...).isValid()` without checking length — though `QDate` differs from SQLite here and does reject `2024-02-30`.

**`publication_date` normalization target = ISO 8601 reduced precision**, not full dates. Forcing `'February 2001'` into `2001-02-01` invents a day that isn't in the source. Three legal shapes: `YYYY`, `YYYY-MM`, `YYYY-MM-DD`. All three sort correctly lexicographically and preserve the precision actually known.

Coverage of a parser handling the observed formats:

| Form | Count | → |
|---|---:|---|
| bare year (`2014`) | 2267 | unchanged |
| month-day-year (`Nov 06, 1996`, `February 6, 2007`) | 545 | `1996-11-06` |
| month-year (`February 2001`, `May 1998`) | 109 | `2001-02` |
| already ISO | 62 | unchanged |
| year-month (`1999-09`) | 40 | unchanged |
| day-month-year (`20 May 2013`, `30-Jun-2009`) | 9 | `2013-05-20` |
| year + month name/number (`2011 March`, `2000 03`) | 8 | `2011-03` |
| **not normalizable** | **27** | **left verbatim** |

The 27 are not corruption: `201u`, `200u`, `197u`, `195?` are **MARC cataloging notation** for an uncertain decade; `1432هجرياً` is a **Hijri year**; the rest are genuinely ambiguous (`1/7/2021` — Jan 7 or 1 July? `Jan-21` — Jan 2021 or 21 Jan?). All are legitimate bibliographic values.

> **Concern, stated once.** This is a one-way transform over 3067 rows that discards the original wording. Because the ambiguous cases can't be resolved from the data, the migration writes originals into a `publication_date_original TEXT` column before rewriting, making the whole transform reversible with one `UPDATE`. Proceeding with normalization as decided.

---

## Phase A — Harness (no behavior change; green from day one)

### A1. CMake wiring

**`cmake/BuildUtils.cmake`** — add `create_test()` beside the existing `create_application()`, same `cmake_parse_arguments(PARSE_ARGV 0 ...)` style, reusing `vlms_configure_target()`:

```cmake
function(create_test)
    set(options GUILESS)
    set(single_value_args NAME TIMEOUT)
    set(list_args SOURCES HEADERS DEPENDENCIES LABELS ENVIRONMENT)
    # add_executable → link Qt::Test + DEPENDENCIES → vlms_configure_target
    # add_test → set_tests_properties(LABELS/TIMEOUT/ENVIRONMENT)
endfunction()
```

`GUILESS` → `QTEST_GUILESS_MAIN` (all Core tests). Default → `QTEST_MAIN` (dialog tests). AUTOMOC already propagates from `cmake/QtSupport.cmake` via `include(Common)` at root, so `#include "tst_x.moc"` needs no extra plumbing.

Baseline environment on **every** test:

| Var | Value | Why |
|---|---|---|
| `QT_QPA_PLATFORM` | `offscreen` | headless |
| `VLMS_SCHEMA_PATH` | `${CMAKE_SOURCE_DIR}/database/schema.sql` | decouples tests from `VLMS_DEV_PATHS`, which is forced OFF for Windows Release |
| `TZ` | `Africa/Tunis` | machine-independent date tests |
| `XDG_CONFIG_HOME` | `<binary_dir>/test-config/<name>` | keeps `QSettings` out of `~/.config/VLMS` |

Set env **per-test in CMake**, not job-level in CI, so a local `ctest` behaves identically. Default `TIMEOUT` 60s. Labels: `core`, `ui`, `injection`, `dates`, `tz`, `realdb`.

**Root `CMakeLists.txt`** — `option(VLMS_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})` after `include(Common)`; `enable_testing()` + `add_subdirectory(tests)` **after** `add_subdirectory(applications)` (UI tests link a library extracted from it). Call `enable_testing()` in the top-level file so `ctest --test-dir build` finds the suite. Do **not** `include(CTest)` — it drags in CDash targets.

**`applications/vlms/CMakeLists.txt`** — split into a testable library:
- `add_library(vlms_ui STATIC ...)` with every current source/header **except `src/main.cpp`**; `target_include_directories(... PUBLIC src)`; links `VLMS::Core` + `Qt::Widgets`.
- `create_application(NAME vlms ENTRY QT_ui SOURCES src/main.cpp RESOURCES ... DEPENDENCIES vlms_ui)`.
- Keep `.qrc` on the **executable** — static-lib Qt resources need `Q_INIT_RESOURCE` and the dialog tests never call `Theme::setupFonts`. The `install()`/`OcrBundle`/`WindowsPackaging` blocks stay on `vlms`.

### A2. Three Core changes required by the fixture

Each is justified independently of testing.

| Change | File | Why |
|---|---|---|
| **`Database::~Database()`** — close + `removeDatabase(m_connectionName)` via a scoped copy | [Database.h](libraries/Database/include/VLMS/Database/Database.h), [Database.cpp](libraries/Database/src/Database.cpp) | The class has **no destructor** today and leaks its UUID connection. Fixes a real leak and reduces fixture teardown to `m_db.reset()`. |
| **Env override in `bundledSchemaPath()`** — `qEnvironmentVariable("VLMS_SCHEMA_PATH")` as the *first* candidate | [Database.cpp:13](libraries/Database/src/Database.cpp:13) | 3 lines. Also useful for debugging installer path resolution. |
| **`Strings::knownKeys(localeCode)`** | [Strings.h](libraries/Core/include/VLMS/Core/Strings.h), [Strings.cpp](libraries/Core/src/Strings.cpp) | The ar/fr/en tables are in an anonymous namespace ([Strings.cpp:8](libraries/Core/src/Strings.cpp:8)). Without an accessor the trilingual parity test **cannot be written**. |

No `:memory:` mode — `Database::databasePath()` hardcodes `dataDirectory + "/vlms.db"`, and a temp file on tmpfs is fast enough that a test-only production code path isn't worth it.

### A3. Test support library — `tests/support/`

```
tests/
  CMakeLists.txt          support/  core/  ui/  data/
```

`vlms_testsupport` links `VLMS::Core`, `Qt::Test`, and **explicitly `Qt::Sql`** (needed because C15 flips Core's `Qt::Sql` to PRIVATE).

**`TestDatabase`** — `QTemporaryDir`, three modes: `FreshSchema` (empty dir, `Database::open()` applies the real `database/schema.sql`), `FromSqlFile` (seed from `tests/data/*.sql` first), `FromCopyOf` (copy an existing `.db`). Exposes `database()`, `connectionName()`, `dataDirectory()`, `resourcesDirectory()`, plus raw escape hatches `exec()`, `execBound()`, `scalar()`, `tableExists()`, `userVersion()`.

Construction deliberately drives the **real** production path — `applySchema` → the four `migrateXIfNeeded` steps → `ensureDefaultEmployee` — so the migration chain is under test in every single test, not just the migration suite. Do **not** call `Paths::ensureLayout()`: `Paths::projectRoot()` is baked to `VLMS_PROJECT_ROOT` at compile time and would write into the real source tree.

**Granularity: per test case** (`init()`/`cleanup()`). Qt Test runs slots in declaration order; a per-class fixture creates ordering dependencies that break the moment someone reorders. Cost is negligible on tmpfs. Pure-logic suites take no fixture.

**`TestSeed`** — two deliberately separate levels. *Repository level* (`seedMember`, `seedBook`, `seedCategory`) goes through validation for happy-path tests. *Raw SQL level* (`rawInsertLoan`, `rawSetRegisteredAt`) **bypasses** validation — essential, because half the date tests need rows the guards would reject. Seeds need auto-unique defaults: `membership_number` is UNIQUE and `books` has `UNIQUE (title, author_id, publisher_id, isbn, language)`.

**`TestEnv`** — `QSettings`/XDG sandbox. `Locale::save()`/`loadSaved()` ([Locale.cpp:49](libraries/Core/src/Locale.cpp:49)) write real settings under org "VLMS" and would clobber the developer's saved UI language.

### A4. Tests that pass on current code — regression armor

These are the value of Phase A: they are **not** bug reports, they are the safety net for Phase C.

- **`tst_strings_parity`** — every key present in all three tables, both directions; no empty values; **`{param}` token sets identical across locales** (catches a translator writing `{annee}` for `{year}`, which renders a literal brace). Failure messages must name the missing keys, not count them.
- **`tst_database_schema`** — parse `CREATE TABLE`/`CREATE INDEX` names out of `schema.sql` and assert each exists (this is what catches the `;`-splitter shredding a statement); `foreign_keys` pragma on after open; `open()` idempotent; `ensureDefaultEmployee` exactly-once; graceful failure on missing schema file; `destructorRemovesTheSqlConnection`.
- **`tst_database_migrations`** — one test per historical shape from `tests/data/*.sql`, covering all four `migrateXIfNeeded` steps, row-count and row-id preservation, chain idempotency across two opens.
- **`tst_*_repository`** (catalog / member / circulation / metrics) — CRUD, validation rejections, transaction rollback, `lastError()` set on **every** failure path (data-driven over every reachable `setError` site — 14 in `CatalogRepository` alone).
- **Pagination correctness** — `listX`/`countX` agreement across every filter combination, and `listBooksPagesCoverEveryRowExactlyOnce` (walk offsets, assert the union equals `countBooks` with no duplicates). Note `ORDER BY b.title COLLATE NOCASE` ([CatalogRepository.cpp:139](libraries/Repositories/src/CatalogRepository.cpp:139)) is **not a total order**, so this may legitimately expose a real paging bug on duplicate titles. The list/count divergence risk is real: `listBooks` joins `publishers`/`book_copies`/`loans`, `countBooks` joins none.

### A5. Injection resistance suite — the security deliverable

Shape of every test: run with a hostile string, assert **(a)** the call returns the *correct* result set (not merely "doesn't crash"), and **(b)** the schema and every row count are unchanged. Put (b) in a shared `assertSchemaIntact()` called from `cleanup()` in all three suites.

Shared `QTest::addColumn` payload corpus in `TestSeed.h`:

```
'; DROP TABLE books;--      ' UNION SELECT 1,2,3--      ' OR '1'='1
admin'--                    " OR ""="                   ; DROP TABLE loans;--
%      _      \      \%      100\%_x
كتاب' OR 1=1--              L'Étranger  (legitimate apostrophe — must MATCH)
QString("a\0b", 3)          10000 × '        📚        U+202E
```

Applied to **every string field of every Query struct** and every `*Input` string:

- **`tst_catalog_injection`** — `BookQuery::{search, categoryCodes, languages}`, plus title/author/publisher/isbn/category-code round-trips. Critical cases: `searchWithPercentDoesNotMatchEverything` (seed `"abc"` and `"a%c"`; searching `%` must return only `a%c` — the `escapeLike` proof), `multipleCategoryCodesWithMixedPayloads` (3+ entries, one hostile — proves the placeholder loop indexes correctly), `categoryCodeWithLegitimateApostropheMatchesExactly`.
- **`tst_member_injection`** — `MemberQuery::{search, statuses}` (note `listMembers` does **not** whitelist `statuses`; it binds them, so a hostile status must return zero rows), plus every `MemberInput` string.
- **`tst_circulation_injection`** — `LoanQuery::{search, filters}`. Key case: `filterWithHostileSqlFragmentIsSilentlyDropped` — feed `"open) OR 1=1 --"`; the whitelist at [CirculationRepository.cpp:93](libraries/Repositories/src/CirculationRepository.cpp:93) must ignore it, so results equal the **no-filter** case, not the open-filter case.

### A6. CI — new `.github/workflows/ci.yml`

Do **not** extend `windows-installer.yml`: it's release tooling (`workflow_dispatch` + tag push, 90-min timeout, MSYS2 + Inno Setup + OCR staging). Tests need `push`/`pull_request` and a 5-minute loop.

```yaml
name: CI
on: { push: { branches: [main] }, pull_request:, workflow_dispatch: }
concurrency: { group: ci-${{ github.ref }}, cancel-in-progress: true }
jobs:
  linux-tests:
    runs-on: ubuntu-24.04
    timeout-minutes: 20
    strategy:
      fail-fast: false
      matrix:
        include:
          - { name: default,    cmake_args: "" }
          - { name: core-only,  cmake_args: "-DBUILD_APPS=OFF" }
          - { name: sanitizers, cmake_args: "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer" }
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y --no-install-recommends
               qt6-base-dev qt6-base-dev-tools libqt6sql6-sqlite libgl1-mesa-dev ninja-build
      - run: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVLMS_BUILD_TESTS=ON ${{ matrix.cmake_args }}
      - run: cmake --build build --parallel
      - run: ctest --test-dir build --output-on-failure --no-tests=error -LE realdb
```

Things that bite if missed:
- **`libqt6sql6-sqlite` is mandatory** — `qt6-base-dev` alone does not ship the QSQLITE driver plugin; without it every DB test dies with "Driver not loaded".
- **`libgl1-mesa-dev`** is needed to link `Qt6::Gui` (Core links it PUBLIC) even for offscreen.
- **`--no-tests=error`** so a broken `create_test()` that registers nothing fails loudly instead of a green zero-test run.
- `core-only` (`BUILD_APPS=OFF`) proves Core has no upward dependency on the UI.
- `sanitizers` is worth it: the code is raw `new` throughout relying on Qt parent ownership, with mixed parents in the dialogs.
- Make `ctest -L injection` a required status check.

---

## Phase B — Characterization tests, all XFAIL

Add `Clock` + `ScopedClock` to Core (nothing uses them yet), then write every bug test with `QEXPECT_FAIL("", "finding N: ...", Abort)`.

**Why XFAIL rather than "pin current behavior then flip the assertion":** Qt Test reports an **XPASS as a failure**. The moment a bug is fixed, the suite mechanically forces removal of the marker. The alternative leaves a commit in history whose test asserts that a bug is correct, with nothing forcing the flip. Suite stays green throughout; `ctest` output becomes a live list of known defects by name.

### B1. `Clock` — the time seam

New `libraries/Core/include/VLMS/Core/Clock.h` + `src/Clock.cpp`:

```cpp
namespace VLMS {
class Clock {
public:
    static QDate   today();       // local calendar date
    static QString todayIso();    // binds to :today
    static QString nowIso();      // binds to :updated_at
    static void setFixedDate(const QDate&);
    static void reset();
    static bool isOverridden();
};
class ScopedClock { public: explicit ScopedClock(const QDate&); ~ScopedClock(); };
}
```

Storage is a plain `static std::optional<QDateTime>` — **not** `thread_local`, which would silently fail to affect a worker thread if one is ever added.

**Static clock, not repository DI.** DI means changing 4 repository constructors, `Application.cpp:29-35`, and 3 dialog constructors on a codebase with zero tests. The dialogs call `QDate::currentDate()` directly ([LoanCheckoutDialog.cpp:74](applications/vlms/src/ui/circulation/LoanCheckoutDialog.cpp:74), [LoanReturnDialog.cpp:43](applications/vlms/src/ui/circulation/LoanReturnDialog.cpp:43), [LoanExtendDialog.cpp:48](applications/vlms/src/ui/circulation/LoanExtendDialog.cpp:48)) and are constructed inline from page code — a static is a drop-in. Crucially, **one clock feeds both worlds**: C++ guards use `Clock::today()`, SQL binds `Clock::todayIso()`. Global mutable state is mitigated by `ScopedClock` RAII plus a `cleanup()` assertion that `isOverridden() == false`.

Rejected: `date('now','localtime')`. It fixes UTC→local in one word but SQLite would still read the *real* system clock, leaving overdue-boundary tests time-of-day dependent and the C++ guards on a separate clock. (It **is** the right answer for schema column DEFAULTs — see D2 — since those fire for rows created outside the app.)

### B2. The defect inventory (each an XFAIL)

| # | Defect | Test | Site |
|---|---|---|---|
| 1 | UTC/local mismatch | `tst_circulation_timezone` (see B3) | 6 sites, below |
| 2 | `createLoan` has no parse guard — invalid `QDate` has `jd = min()`, so garbage `borrowedAt` + valid `dueAt` → `valid < invalid` is false → **persisted** | `createLoanRejectsUnparseableBorrowedAt` | [CirculationRepository.cpp:322](libraries/Repositories/src/CirculationRepository.cpp:322) |
| 2b | `returnLoan` rejects garbage but with the **wrong message**, and *compounds* bad data when the stored `borrowedAt` is itself unparseable | `returnLoanRejectsWhenStoredBorrowedAtIsUnparseable` | [:371-384](libraries/Repositories/src/CirculationRepository.cpp:371) |
| 3 | `date('garbage')` → NULL → `CASE` falls to `ELSE 0`: bad rows **silently vanish** from the overdue filter, the KPI, and all metrics windows | `loanWithUnparseableDueDateIsNotSilentlyExcludedFromOverdueFilter` | [:82](libraries/Repositories/src/CirculationRepository.cpp:82) |
| 4 | No upper bound on any metrics window; future `borrowed_at` is creatable (no `setMaximumDate`) | `futureDatedLoanIsNotCountedInAllThreeWindowsSimultaneously` | [MetricsRepository.cpp:136](libraries/Repositories/src/MetricsRepository.cpp:136), [LoanCheckoutDialog.cpp:71](applications/vlms/src/ui/circulation/LoanCheckoutDialog.cpp:71) |
| 5 | `extendLoan` UI/Core divergence — UI clamps the floor to today, Core doesn't; `extendLoan(id,"2020-01-02")` on a loan due `2020-01-01` succeeds | `extendLoanRejectsNewDueBeforeToday` | [:433](libraries/Repositories/src/CirculationRepository.cpp:433) |
| 5b | The `currentDueDate.isValid() &&` short-circuit **disables** the monotonicity check when the stored due date is garbage | `extendLoanRejectsWhenStoredDueAtIsUnparseable` | [:433](libraries/Repositories/src/CirculationRepository.cpp:433) |
| 6 | Loan-period policy lives in the dialogs | *no XFAIL — see below* | [LoanCheckoutDialog.cpp:80](applications/vlms/src/ui/circulation/LoanCheckoutDialog.cpp:80) |
| 6b | `LoanExtendDialog` has **two policies in one widget**: `minimumDue.addDays(defaultLoanDays()-1)` gives `currentDue+14` for a current loan but `today+13` for an overdue one | `extendDialogSuggestsFourteenDaysForAnOverdueLoan` | [LoanExtendDialog.cpp:54](applications/vlms/src/ui/circulation/LoanExtendDialog.cpp:54) |
| 7 | Zero-day loans allowed — guard is strict `<` | `createLoanRejectsDueEqualToBorrowed` | [:322](libraries/Repositories/src/CirculationRepository.cpp:322) |
| 8 | `createLoan` has no transaction despite check-then-act; no UNIQUE constraint prevents two open loans on one copy | `twoOpenLoansOnOneCopyAreImpossible` | [:327-347](libraries/Repositories/src/CirculationRepository.cpp:327) |
| **9** | **Silent failure paths — found by Phase A.** Six sites return `false` with an **empty** `lastError()`, and the pages render that straight into `QMessageBox::warning(..., repo.lastError())`, so the librarian gets an error dialog with a blank body. Four are `return X.numRowsAffected() > 0` with no `setError()`; two are `getLoan()` reporting "not found" via `setError(q.lastError().text())`, which is empty when the query succeeded but matched no rows. | `lastErrorIsSetOnEveryFailurePath` (5 rows) | `CatalogRepository.cpp:676,871,893`; `MemberRepository.cpp:491`; `CirculationRepository::{returnLoan,extendLoan}` |
| **11** | **`migrateBookLanguageIfNeeded` never worked — found by Phase D.** The detector reads books' `CREATE TABLE` out of `sqlite_master` and was still sitting on that row when the rebuild reached `DROP TABLE books`. SQLite refuses a DROP while a read on the schema is open, so `open()` returned false and the app would not start at all against a database that still had the old `ar`/`fr` constraint. No XFAIL: it was found and fixed in the same commit, and mutation-tested by unscoping the detector (7 cases red). | `bookLanguageMigrationIsNotBlockedByItsOwnDetectorQuery` | [Database.cpp](libraries/Database/src/Database.cpp) `migrateBookLanguageIfNeeded` |
| **10** | **`createBook` accepts `initialCopyCount = 0`** — found by Phase A. `addCopies()` returns `true` early when `count <= 0`, producing a catalog entry with no copies that can never be borrowed. `BookEditorDialog`'s spin box is `setRange(1, 999)` so the UI cannot produce it, but Core accepts it and an importer would. | `createBookRejectsCopyCountBelowOne` | [CatalogRepository.cpp](libraries/Repositories/src/CatalogRepository.cpp) `addCopies` |

Plus the four `;`-splitter cases (see D1) and boundary tests that pass today and must keep passing: `loanDueTodayIsNotOverdue`, `weekWindowIncludesRowExactlySixDaysAgo`, `monthWindowIsCalendarMonthNotRollingThirtyDays`, `returnLoanRejectsFutureReturnDate`.

**Finding 6 gets no XFAIL, deliberately.** Its real form is structural — the dialogs reach for `CirculationRepository::defaultLoanDays()` and do the arithmetic themselves — and "this symbol should not exist" is a compile-time claim that no runtime assertion can honestly express. Writing one would mean inventing a proxy that tests something other than the defect. What is testable is the *consequence*, so the agreement tests pin every date the dialogs compute against what Core produces for the same input: `checkoutDialogDefaultDueMatchesWhatCoreProducesForABlankInput` (blank `LoanInput` → Core's own defaults must equal the dialog's initial fields) and `extendDialogMinimumIsAcceptedByCore` (whatever the calendar offers as its earliest selectable date, `extendLoan` must accept — otherwise a librarian picks the first allowed date and is told it is invalid). Both pass today; they are the armor that keeps the C5 extraction from moving a date the librarian sees.

**Boundary tests use comfortable offsets on purpose.** A "due yesterday is overdue" test in the general date suite would be red for one hour a night under `TZ=Africa/Tunis` — that razor edge belongs to `tst_circulation_timezone`, which owns it deliberately. One case does need the exact boundary (`monthWindowIsCalendarMonthNotRollingThirtyDays`, where excluding the previous month is the half that distinguishes calendar from rolling), and there the fragile assertion is guarded by `!localDateDiffersFromUtcDate()` with a comment naming finding 1; C4 removes the guard.

Also pin the *semantics* so the refactor can't change them by accident: `weekWindowIsRollingSevenDaysNotCalendarWeek` (it's `'-6 days'`) and `monthWindowIsCalendarMonth` (`'start of month'`) — these are inconsistent with each other but intentionally so.

### B3. The local-vs-UTC proof — `tst_circulation_timezone`

One executable, registered **twice** in CTest with different `TZ`, and deliberately **not** clock-pinned:

```cmake
if(UNIX)
  add_test(NAME tst_tz_utc_plus_14  COMMAND tst_circulation_timezone)  # TZ=Pacific/Kiritimati
  add_test(NAME tst_tz_utc_minus_11 COMMAND tst_circulation_timezone)  # TZ=Pacific/Midway
endif()
```

Kiritimati is UTC+14, Midway UTC−11 — **25 hours apart**, so at any real instant the two processes are on different calendar dates, guaranteeing at least one differs from UTC. Both zones hold a constant offset all year.

**Neither entry alone proves anything — put that in a comment**, or someone will "fix" the flaky-looking duplicate by deleting one. Gate on `if(UNIX)`: Qt 6 on Windows resolves the time zone from the Win32 API and ignores `TZ`, making these silently vacuous. Label `tz` so non-Linux runners can `-LE tz`.

#### Three corrections from writing it

**Registration.** Rather than hand-rolled `add_test` calls, `create_test()` grew a `TIMEZONES` list argument: given one, it registers the same executable once per zone as `<NAME>_<zone>` instead of once under the default `Africa/Tunis`, and on a non-UNIX host it builds the executable and registers nothing, with a `message(STATUS)` saying so. Keeps the baseline environment in one place.

**The XFAILs must be armed on a *direction*, not on "the dates differ".** A plain `QEXPECT_FAIL` XPASSes on whichever entry currently agrees with UTC, and Qt Test scores an XPASS as a failure — so both entries would be red pre-fix *and* post-fix, destroying the signal the pair exists to give. A positive and a negative offset also break *different* rules: `loanDueYesterdayIsOverdue` fails only when UTC lags local, `loanDueTodayIsNotOverdue` / `todaysCheckouts` / `todaysReturns` only when UTC leads. Each XFAIL is therefore guarded by `utcIsBehindLocal()` or `utcIsAheadOfLocal()`. Both entries are green at every hour, and the defect shows up as XFAIL lines naming finding 1 on whichever entry currently spans the boundary — consistent with the rest of Phase B, and it keeps CI green without excluding the `tz` label. Verified in both directions (an artificial UTC+20 fires the lagging-UTC branch and its two XFAILs).

**`hasDaylightTime()` is the wrong question.** It reports whether a zone has *ever* observed daylight saving, and Pacific/Midway did historically, so it answers true for a zone that has been a flat UTC−11 for years. The precondition test samples `offsetFromUtc()` at ±6 months instead. There is also an `atLeastOneRegisteredZoneDiffersFromUtcRightNow` case that computes both zones' dates in-process, so swapping in a zone only two hours from UTC fails immediately rather than going green for the wrong reason.

**A fourth finding-1 site.** `returnLoan` stamps a local date and the returns window compares it against UTC, so handing a book back over the desk can leave today's returns counter unmoved. The acceptance guard itself is pure C++ and already local-consistent; it is only the metrics half that breaks.

### B4. UI dialog tests

**Constraint:** accept-path validations live in lambdas on `QDialogButtonBox::accepted` and pop a **modal** `QMessageBox`. Clicking OK down a rejection path will **hang the test**. Therefore:

1. Assert **widget constraints** (`minimumDate`, `maximumDate`, initial `date()`) rather than clicking OK — that's where the divergences actually live.
2. Move the *rules* into `LoanPolicy` (C5) and test them as pure functions.
3. Only where a rejection path must genuinely run, arm `QTimer::singleShot(0, ...)` to close the active modal. Use sparingly — it's the only flaky pattern in the suite.

Do **not** instantiate `Application` or `MainWindow`: `MainWindow` does `qobject_cast<Application*>(qApp)` and `Application`'s constructor opens the real source-tree database. The five dialogs take repos/records by ctor reference and never touch `qApp` — they are the entire UI test surface.

---

## Phase C — Fixes, one finding per commit, each removing exactly one XFAIL

Order matters; later steps depend on earlier ones.

| # | Change | Files |
|---|---|---|
| **C1** | `QDate::currentDate()` → `Clock::today()` everywhere. Pure refactor, removes no XFAIL — but makes every subsequent date test deterministic. | [CirculationRepository.cpp:316,319,372,381](libraries/Repositories/src/CirculationRepository.cpp:316); the 3 loan dialogs; [MainWindow.cpp:254](applications/vlms/src/ui/MainWindow.cpp:254) |
| **C2** | Hoist `escapeLike` (**triplicated verbatim** at CatalogRepository/MemberRepository/CirculationRepository `.cpp:13`) and `nullableText` into new `VLMS/Core/SqlText.h`. Enables direct testing instead of inference through three repositories. | new `SqlText.{h,cpp}`; delete 4 anon-namespace copies |
| **C3** | **`MetricsRepository`: enum windows + bound dates.** Replace `fetchPeriodCounts(QString startDateSql)` with `fetchPeriodCounts(MetricsWindow)`; compute `{start, endInclusive}` in C++ from `Clock::today()`; SQL becomes fully static with `BETWEEN :start AND :end`. Change `scalarCount(QString)` → `scalarCount(QString, QVariantMap binds = {})` using `prepare`/`bindValue`. | [MetricsRepository.cpp:35-47,120-128,136-138](libraries/Repositories/src/MetricsRepository.cpp:35) |
| | *One change closes four things:* latent injection hole #2, the UTC windows, the missing upper bound (defect 4), and the last `exec(<QString variable>)` in any repository — taking the repository-layer injection surface to **zero by construction**. | |
| **C4** | **`date('now')` → `:today`** bound from `Clock::todayIso()` at all 6 sites, and dedupe the predicate into one shared constant (`LoanSql.h`). | [CirculationRepository.cpp:82,99,126,176,239](libraries/Repositories/src/CirculationRepository.cpp:82) + [MetricsRepository.cpp:127](libraries/Repositories/src/MetricsRepository.cpp:127) |
| | ⚠️ In `listLoans`/`countLoans`, bind `:today` **unconditionally**, not inside the `if (filterClauses)` branch — `:today` also appears in the SELECT `CASE` and `ORDER BY`. QSQLITE binds an unmentioned placeholder as NULL **without erroring**, silently turning every `is_overdue` into 0. `isOverdueFlagIsCorrectWithNoFiltersApplied` exists to catch exactly this. | |
| **C5** | **`LoanPolicy` extraction** — new `VLMS/Core/LoanPolicy.h`: `defaultLoanDays()`, `suggestedDueDate(borrowedOn)`, `minimumExtensionDate(currentDue, today)`, `suggestedExtensionDate(...)`, `validateLoanDates(...)`, `validateReturnDate(...)`. **Delete** `CirculationRepository::defaultLoanDays()` — it exists *only* so the dialogs could do arithmetic, which is the defect. Update both dialogs and `createLoan`/`extendLoan` to call it. Closes 5, 6, 6b. | new `LoanPolicy.{h,cpp}`; [CirculationRepository.h:31](libraries/Repositories/include/VLMS/Repositories/CirculationRepository.h:31); 2 dialogs |
| **C6** | **Parse guards.** `createLoan`: parse both dates first, reject each with its own message if `!isValid()`, compare with `<=` (closes 7), reject `borrowed > Clock::today()` (closes 4). `returnLoan`: explicit invalid-date message; reject when the *stored* `borrowedAt` is unparseable (2b). `extendLoan`: drop the `isValid() &&` short-circuit (5b) and add the floor. Closes 2, 2b, 4, 5b, 7. | [CirculationRepository.cpp:315-325,371-384,427-436](libraries/Repositories/src/CirculationRepository.cpp:315) |
| **C7** | **`storeMemberImage` column enum.** Replace the `const QString& columnName` parameter with a private `enum class ImageSlot { Photo, IdCard }`; a `switch` selects one of two fully-static `QStringLiteral` UPDATE statements. Removes the only column-name interpolation in the codebase — and a future caller **cannot** pass a variable, because the type won't allow it. | [MemberRepository.cpp:538-560](libraries/Repositories/src/MemberRepository.cpp:538), [MemberRepository.h:39](libraries/Repositories/include/VLMS/Repositories/MemberRepository.h:39) |
| | *Note honestly:* this guard is compile-time, not runtime-testable. The behavioral tests are the pair `setPhotoImageDoesNotTouchIdImagePath` / `setIdImageDoesNotTouchPhotoPath`. | |
| **C8** | **`Qt::Sql` PUBLIC → PRIVATE.** Verified safe: no public Core header includes a QtSql header; the three repositories only forward-declare `class QSqlQuery` in private sections. Makes "no SQL in the UI layer" a **compile error** rather than a convention. `Qt::Gui` must stay PUBLIC (`Locale::applyToApplication(QGuiApplication*)` is in a public header). | [libraries/Core/CMakeLists.txt:31](libraries/Core/CMakeLists.txt:31) |
| | ⚠️ Must land **after** `vlms_testsupport` exists with its own `PRIVATE Qt::Sql` link, or the fixture breaks in the same commit. | |
| **C9** | `updated_at = datetime('now')` (4 sites) → `:updated_at` bound from `Clock::nowIso()`; bind `registered_at` explicitly in `createMember`. No backfill (resolved: existing values are bare dates). | [CatalogRepository.cpp:604,720](libraries/Repositories/src/CatalogRepository.cpp:604), [MemberRepository.cpp:420,539](libraries/Repositories/src/MemberRepository.cpp:420) |
| **C10** | `createLoan` transaction wrapping check-then-act, matching the pattern already in `CatalogRepository::createBook`. Closes 8's first half. | [CirculationRepository.cpp:327-353](libraries/Repositories/src/CirculationRepository.cpp:327) |
| **C11a** | **Defect 9** — give every failure path a message. Replace the four `return X.numRowsAffected() > 0` returns with an explicit not-found `setError()`, and make `getLoan()` distinguish "query failed" from "no such row" instead of reporting an empty driver error. Removes 5 XFAILs. | `CatalogRepository.cpp:676,871,893`; `MemberRepository.cpp:491`; `CirculationRepository.cpp` `getLoan` |
| **C11b** | **Defect 10** — reject `initialCopyCount < 1` in `createBook` (or clamp to 1), matching the dialog's `setRange(1, 999)`. | [CatalogRepository.cpp](libraries/Repositories/src/CatalogRepository.cpp) |
| **C11** | Unify the blank convention: `publicationDate` binds raw (→ `''`) while `dateOfBirth` uses `nullableText` (→ NULL). Route both through `SqlText::nullableText`. Strict ISO validation for `date_of_birth` (all 13 production rows already conform) next to the existing `isValidSex` check. | [CatalogRepository.cpp:537,614](libraries/Repositories/src/CatalogRepository.cpp:537), [MemberRepository.cpp:293,379](libraries/Repositories/src/MemberRepository.cpp:293) |

Page-level orchestration (`CatalogPage::addBook` → `createBook` + `setCoverImage`; `MembersPage::addMember` → 3 writes) is **out of scope** for this pass — note it, don't fix it. Wrapping those properly means moving image handling into the repository create call, a larger refactor.

---

## Phase D — Schema, last

Deliberately last: the constraints must not land until C5/C6/C11 are in place, or the app keeps writing rows that violate them and every subsequent `open()` fails its own pre-flight.

> **Delivered in seven commits.** One step was added ahead of the planned work and one was
> split out of it:
>
> - **D0**, not in the plan text, closes the plan-A4 gap that blocked everything else:
>   `tests/data/` fixtures and `tst_database_migrations`. Writing it immediately found
>   finding 11 — a migration that has never worked on any database that needed it.
> - **D3a** was split out of D3. Finding 3's three XFAILs are a *query* defect, not a
>   constraint one, and the constraints cannot close them: the pre-flight deliberately
>   skips any database that holds such a row, so those are precisely the databases that
>   keep them. Fixing the predicate first also kept the three tests writable against a
>   fresh database, before D3 made that row impossible to plant.
> - The synthetic migration tests the plan listed under D6 landed with D3/D4, which is
>   what they test. D6 is the real-database verification alone.

### D1. Quote-aware statement splitter

[Database.cpp:35](libraries/Database/src/Database.cpp:35) is `script.split(QChar(';'), Qt::SkipEmptyParts)` — it shreds any `;` inside a string literal or a `CREATE TRIGGER ... BEGIN ... END;`. Not exploitable (all three inputs are trusted) but a landmine for the constraint work. Replace with a ~30-line scanner tracking `'...'` (with `''` escapes), `"..."`, `--` to EOL, and `/* */`, splitting only on top-level `;`.

`tst_sql_script` is written and green, and found **four** failure modes rather than the two anticipated. Each drives the real path — the case is appended to the genuine `database/schema.sql`, `VLMS_SCHEMA_PATH` is pointed at the result, and the test asks whether `Database::open()` succeeds — with the exact SQLite error recorded:

| Case | SQLite says |
|---|---|
| `DEFAULT 'first; second'` | `unrecognized token: "'first"` |
| `CREATE TRIGGER … BEGIN … END;` | `incomplete input` |
| trailing `-- see ticket 12; harmless` | ``near "harmless": syntax error`` |
| `/* replaces the old index; keep until v2 */` | `No query` |

The last two were not in the original inventory and both come from the *comment* handling rather than from quoting: comments are stripped **after** splitting and only when they begin a line, so a trailing `--` comment holding a `;` is cut into a comment-only fragment (correctly skipped) plus a bare `harmless` that is then executed as SQL, and `/* */` is not recognised at all. The scanner must therefore consume comments **during** the scan, not in a second pass.

> **As built.** The scanner lives in `SqlText::splitStatements`, not in `Database.cpp`'s
> anonymous namespace as planned. It moved there during D3/D4 for a reason worth
> recording: the test harness's *own* fixture runner still split on a bare `;`, on the
> grounds that "fixture scripts are ours" — and then a semicolon inside a comment in a
> fixture cut a sentence in half and ran the second half as SQL. The harness had kept
> the exact bug it exists to catch. Both paths now use one scanner, and it has direct
> unit tests in `tst_sql_text` on top of the end-to-end ones.
>
> Trigger bodies needed **nesting, not a flag**: `BEGIN` opens and `END` closes, and a
> `CASE` expression inside a body has its own `END`, so a boolean would close the trigger
> early. `[bracket]` and `` `backtick` `` identifier quoting are not handled, and are
> documented as not handled; nothing in this project has ever used them.

### D2. `PRAGMA user_version` — add it now

Four ad-hoc detectors is already one too many, and each re-runs `PRAGMA table_info` on every launch.

- Append `PRAGMA user_version = 1;` to `database/schema.sql`, so a fresh DB starts at 1 and skips the legacy chain entirely.
- In `Database::open()`, read `user_version` after `applySchema()`. If `0` (legacy — **confirmed** for the production DB), run the four existing detectors as today (they're idempotent and needed for DBs in the wild), then the new step, then set `user_version = 1`. If `>= 1`, dispatch on version only. If `> kSchemaVersion`, `qWarning` and refuse to write.
- `static constexpr int kSchemaVersion = 1;` in `Database.h`, with a test parsing the PRAGMA out of `schema.sql` to assert file and code agree.
- Schema column DEFAULTs move to `datetime('now','localtime')` / `date('now','localtime')` — those fire for rows created outside the app, where no `Clock` exists.

> **A consequence the plan did not anticipate.** "Refuse to write" needs somewhere to
> refuse *to*. `open()` returning false was the only enforcement available — all four
> repositories share one connection, so there is no read-only half-measure — and
> `Application` was **discarding `open()`'s return value entirely**. A failed open
> carried straight on into a window where every page renders empty and every action
> fails with a blank dialog. `main()` now reports it once, in all three languages, and
> stops. That was already possible before D2 (a missing schema file), but D2 is what
> gave `open()` a reason to refuse on purpose.
>
> A second test greps `schema.sql` for a bare `date('now')` so a column added later
> cannot quietly reintroduce the UTC default.

### D3. `migrateDateConstraintsIfNeeded()`

Constraints on `loans` (rebuild required — SQLite has no `ALTER TABLE ADD CONSTRAINT`). **Every date CHECK is a `date(x) IS x` round trip** — see the corrected note above; the GLOB pair the plan originally called for lets `'2024-02-30'` through:

```sql
CHECK (date(borrowed_at) IS borrowed_at)
CHECK (date(due_at)      IS due_at)
CHECK (date(returned_at) IS returned_at)   -- NULL round-trips to NULL, so this
                                           -- needs no IS NULL OR wrapper
CHECK (date(due_at) > date(borrowed_at))                                 -- kills zero-day loans
CHECK (returned_at IS NULL OR date(returned_at) >= date(borrowed_at))    -- replaces the raw
                                                                          -- string compare at schema.sql:133
```

On `members`: `CHECK (date(date_of_birth) IS date_of_birth)`.

On `books`: **no date CHECK on `publication_date`** — the normalized target is reduced-precision ISO (`YYYY` / `YYYY-MM` / `YYYY-MM-DD`) plus 27 verbatim MARC/Hijri values, and the round trip rejects every shape but the full date.

Plus the partial unique index closing defect 8:

```sql
CREATE UNIQUE INDEX IF NOT EXISTS idx_loans_one_open_per_copy
    ON loans(book_copy_id) WHERE returned_at IS NULL;
```

**Procedure** — `PRAGMA foreign_keys` is a **no-op inside a transaction**, so ordering matters:

1. `PRAGMA foreign_keys = OFF` (outside any transaction)
2. Pre-flight (D4) — bail here if any violations
3. `db.transaction()`
4. `CREATE TABLE loans_new (... CHECKs ...)` → `INSERT ... SELECT` → `DROP TABLE loans` → `ALTER TABLE loans_new RENAME TO loans`
5. Recreate the four `idx_loans_*` indexes + the new partial unique index
6. Repeat for `members` (referenced by `loans` and `member_status_history`)
7. `PRAGMA foreign_key_check` — must return **zero rows**, else `rollback()`
8. `commit()`, then `PRAGMA foreign_keys = ON`

Run steps 4–6 through a new `execAll(db, QStringList, context)` helper, **not** `execSqlScript` — sidesteps the splitter entirely and gives per-statement error reporting.

> **Why foreign keys being off matters more than it looks.** `member_status_history`
> references `members` **ON DELETE CASCADE**, and step 6 DROPs `members`. With foreign
> keys left on, the migration would silently delete every status-history row and still
> report success. `constraintMigrationDoesNotCascadeAwayStatusHistory` is the assertion
> that the PRAGMA before the transaction is doing something.
>
> **Failure is not the same as violation.** A pre-flight violation leaves the database
> untouched and the app starts (D4). A *rebuild* failure — the test forces one by leaving
> a stale `loans_new` table behind, as a crashed earlier attempt would — rolls back and
> **fails the open**, because no amount of correcting rows fixes it and starting on a
> half-migrated database is worse than not starting.
>
> The rebuild's `CREATE TABLE` is written in C++ and `schema.sql`'s in SQL, and a
> database can arrive at version 1 by either route.
> `constraintMigrationMatchesWhatSchemaSqlDeclares` compares the two stored CHECK
> clauses so they cannot drift into two different shapes with the same version number.

### D4. Pre-flight — never brick a production database

Count rows violating each new CHECK before rebuilding. **Already run against the production copy: every count is 0** (loans is empty; all 13 `date_of_birth` values are valid ISO; no duplicate open loans). Keep the check in code anyway for other installations.

If any count is non-zero: `qWarning` the offending ids (capped at ~50), **skip the migration**, leave `user_version` at 0, and **return `true`** so the app still starts. A librarian must not get a dead app because a 2019 row has a typo'd date.

> **The skip is a retry, not a surrender**, and that has a consequence the plan did not
> spell out: a skipped database runs the *whole* legacy chain again on every launch,
> D5's publication-date migration included. That migration is therefore gated on its own
> `publication_date_original` column existing, because a second backfill would overwrite
> every original with the already-normalized value and quietly destroy the one thing
> making the transform reversible. `publicationDateMigrationDoesNotRunTwice` opens such a
> database three times and checks the originals are still there.
>
> Each pre-flight query is written as `WHERE NOT (expr)` to mirror CHECK semantics
> exactly: a CHECK is violated only by a FALSE result, and `WHERE NOT (expr)` does not
> select a row whose expr is NULL, just as the CHECK does not fail on one.

### D5. `publication_date` normalization

New `VLMS::DateText::normalizePublicationDate(QString) → QString` in Core, targeting **reduced-precision ISO**: `YYYY`, `YYYY-MM`, `YYYY-MM-DD`. Handles the eight observed shapes (bare year, ISO, year-month, month-day-year, month-year, day-month-year, year + month name, year + month number) with an English month-name table covering both abbreviated and full forms.

Unparseable input returns the original **unchanged** — never a guess. Called from `createBook`/`updateBook` on write, and once by the migration:

1. `ALTER TABLE books ADD COLUMN publication_date_original TEXT`
2. `UPDATE books SET publication_date_original = publication_date`
3. Rewrite `publication_date` row-by-row through the normalizer

Expected: **3040 of 3067 normalized, 27 left verbatim.** The original column makes the transform reversible with one `UPDATE`.

> **Measured: 3043 conforming, 24 verbatim.** The estimate above predates the parser.
> The 24 are `201u` / `200u` / `197u` / `202z` / `195?` / `xxxx` (MARC notation for an
> uncertain decade), `1432هجرياً` (Hijri), `Marzo 2009` and `20 de março de 2021`
> (month names in languages the table does not read), `Jan-21`, and seven values written
> with slashes, dots or a numeric `dd-mm-yyyy` where nothing in the data says whether
> January or July was meant.
>
> The rule that produces that split is worth stating, because it is a decision about a
> librarian's data rather than a parsing detail: **a value with a month *name* is
> unambiguous however it is written** — `8Jan2009`, `23rd Dec 2010`, `October , 18 2022`
> and `2011 March` all normalize — **and a purely numeric value is accepted only in the
> three ISO shapes plus `YYYY MM`**, where the four-digit part can only be a year.
> Impossible dates are refused rather than rolled over: `2024-02-30` comes back
> unchanged, which is precisely what SQLite's own `date()` would not do.
>
> `publication_date_original` was added to **`schema.sql` as well as by the migration**,
> and `createBook`/`updateBook` fill it. Two reasons: a fresh database and an upgraded one
> both report version 1 and must therefore be the same shape, and without normalizing on
> write the next book a librarian typed would put an unnormalized value straight back
> into the column the migration had just tidied.

`BookEditorDialog` keeps its free-text `QLineEdit` (the field legitimately holds MARC notation), but normalizes on accept and shows the normalized value back to the user.

### D6. Real-database verification

`tst_database_realdb`, label `realdb`, registered only when `VLMS_TEST_REAL_DB` names a file. **Copies** the file into a `QTemporaryDir` and never touches the original. Asserts: `open()` true; `PRAGMA integrity_check = ok`; `foreign_key_check` empty; per-table `COUNT(*)` identical before/after; `MAX(id)` per table unchanged; `user_version` advanced to 1; exactly 27 `publication_date` values unchanged and 3040 conforming to one of the three ISO shapes.

Point it at `database/vlms.db.bak-20260814-232925` — already a copy. Excluded from CI via `-LE realdb`.

Synthetic equivalents run in CI from `tests/data/dirty_dates.sql`: `constraintMigrationIsSkippedWhenViolatingRowsExist`, `constraintMigrationPreservesEveryRow`, `constraintMigrationIsRolledBackWhenAStepFails` (pre-create a conflicting `loans_new`; assert the original `loans` and `user_version` are untouched), `constraintMigrationLeavesForeignKeysEnabled`, `newLoansViolatingCheckAreRejectedAfterMigration`, `bareYearIsRejectedAsALoanDate` (the Julian-day regression).

> **As built — 30 cases, all green, against both the backup and the live catalog.**
>
> The `VLMS_TEST_REAL_DB` path is a **CMake cache variable** defaulting to that backup,
> with the entry registered only when the file exists (it is `.gitignore`d, so CI simply
> does not build it). Override with `cmake -DVLMS_TEST_REAL_DB=/path/to/db`.
>
> Three additions to the assertion list. The publication-date claim is checked as an
> **invariant** first — every stored value must equal what the normalizer produces from
> that row's own `publication_date_original`, which is what says nothing was rewritten by
> hand, by accident, or twice — with the 3043/24 split asserted on top, guarded on the row
> total so another catalog still checks the invariant. The constraints are proved **live**
> rather than merely present: a bare year is refused as a borrow date, an impossible date
> of birth is refused as an update. And the test finishes by comparing the source file's
> **SHA-256** against what it was before, because a test that damaged the catalog while
> verifying that nothing damages the catalog would be a poor joke.
>
> Run against the live `database/vlms.db` as well as the backup — 3067 books and
> **45 real loans**, where the backup has none — every pre-flight count zero, every row
> count and `MAX(id)` unchanged, same 3043/24 split.
>
> `tests/data/dirty_dates.sql` is a **fragment**, not a database: a list of INSERTs
> applied on top of `pre_constraint_dates.sql`. The rows are the subject of those tests,
> and keeping them in their own file means they read as a list rather than being hunted
> for inside a second copy of the schema.

---

## Phase E — OCR off the GUI thread

Opened by a support report: on a customer machine "the UI froze completely" while running
OCR. It was not a hardware shortfall. `BookEditorDialog::readDescriptionFromImage()` called
`OcrService::recognize()` **inline on the GUI thread** between a `setOverrideCursor` and a
`restoreOverrideCursor`, so the event loop stopped until Tesseract returned — no repaint, no
cancel, and Windows painting its "Not responding" ghost window after five seconds. There was
no threading anywhere in the codebase; `grep` for `QThread`/`QtConcurrent`/`std::thread` over
`applications/` and `libraries/` returned nothing. The freeze was the expected behaviour of
that code on **any** machine. Three things pushed it past the threshold where it shows:
`Init()` re-read all three LSTM models on every button press, `PSM_AUTO` ran full layout
analysis, and the image went in at native resolution — a 12 MP phone photo as a 36 MB RGB888
buffer that Tesseract gains nothing from.

| | Change | Files |
|---|---|---|
| **E1** | **New `libraries/Ocr`** — `vlms_ocr`, a target that links **std and Tesseract and nothing else**. No Qt, no Core. "The OCR module does not depend on Qt" is now a link error rather than a comment, the same device as `Qt::Sql` being PRIVATE to Core (C8). `OcrService.{h,cpp}` deleted from Core. | new `libraries/Ocr/**`; [libraries/CMakeLists.txt](libraries/CMakeLists.txt) |
| **E2** | **Qt-free tessdata discovery** — `std::filesystem` plus one platform call for "where is my own executable" (`GetModuleFileNameW` / `/proc/self/exe`), replacing `QCoreApplication::applicationDirPath()` and `QDir`. Search order unchanged. | [libraries/Ocr/src/Tessdata.cpp](libraries/Ocr/src/Tessdata.cpp) |
| **E3** | **Availability relaxed to "at least one language"**, from "all three present". A box that shipped with only `eng.traineddata` can read English books; switching the whole feature off because Arabic is missing helps nobody. `osd` is filtered out — it initialises happily and then recognises nothing. | [libraries/Ocr/src/Ocr.cpp](libraries/Ocr/src/Ocr.cpp) |
| **E4** | **`Job`** — one `recognize()` on a `std::thread`, polled by its owner. Cancellation and progress both come from Tesseract's `ETEXT_DESC` monitor passed to `Recognize()`. **`~Job` cancels then joins**, so the dialog can simply close mid-page. Polling rather than a completion callback is deliberate: a callback would fire on the worker thread, and every caller is a UI that may only touch widgets from the main thread. | [libraries/Ocr/src/Job.cpp](libraries/Ocr/src/Job.cpp) |
| **E5** | **Downscale to a 2600 px long edge** before recognition, and **keep the engine alive between runs** instead of re-`Init`-ing. The engine is *not* self-managing — `releaseCachedEngine()` is called from `~BookEditorDialog`, because holding tens of megabytes of language models is the right trade while a librarian works through a book and the wrong one afterwards. | [libraries/Ocr/src/Ocr.cpp](libraries/Ocr/src/Ocr.cpp) |
| **E6** | **Language from the book, overridable per run.** `books.language` already holds ISO 639-1, so the mapping to Tesseract's 639-2/T names is a table. All ten catalogue codes are mapped, including the seven with no bundled model: the table says what the book *is*, `availableLanguages()` says what can be run, and keeping them apart means dropping `spa.traineddata` into tessdata is enough to make Spanish books work with no code change. The button is a `QToolButton` whose menu lists the installed languages under their catalogue names; the librarian's choice outranks the book's for the rest of the dialog. | [BookEditorDialog.cpp:562](applications/vlms/src/ui/catalog/BookEditorDialog.cpp:562) |
| **E7** | **Cancellable progress.** `QProgressDialog` driven by an 80 ms `QTimer`, starting **indeterminate** and becoming a real bar only once Tesseract reports — a determinate bar sitting at zero through decode and `Init` reads as "hung", which is the impression this whole phase exists to remove. Cancel asks and keeps polling; it never waits on the worker from the GUI thread. | same |
| **E8** | New strings in **all three locales** (`ocr.working`, `ocr.cancelling`, `ocr.languageInUse`, `ocr.imageFilter`, `common.cancel`), as `tst_strings_parity` requires. `ocr.readFromImageTip` lost its hardcoded "(Arabic / French / English)", which was already a lie on a two-language install. | [Strings.cpp](libraries/Core/src/Strings.cpp) |

### Behavioral consequences worth knowing

- **A cancelled run says nothing.** The librarian asked for it; a dialog telling them it
  happened is noise. Every other non-`Ok` status gets its own message.
- **The last checked language cannot be turned off.** Unchecking everything would leave the
  button enabled with nothing to run, which reports "OCR unavailable" and reads as a broken
  install.
- **A book in a language with no model still gets a runnable default** (`eng`, or the first
  installed language). The alternative — disabling the button for Spanish books — gives the
  librarian nothing where imperfect Latin-script text is still a useful starting point.
- **Free-text languages are never guessed.** The catalogue's "other" field accepts anything;
  running Arabic models over a Berber title page and pasting the result into the description
  is worse than doing nothing.

### Verification method

Measured on the development machine against the real engine, not estimated:

| | Before | After |
|---|---|---|
| GUI thread blocked during OCR | for the whole run | never |
| Cancel mid-recognition | not possible | returns in **157 ms** |
| Second run on the same dialog | full `Init()` again | **7.5× faster** (127 ms → 17 ms) |
| Dense A4 page, downscale off → on | 1231 ms | **670 ms**, 1658 vs 1660 characters |

`tst_ocr` (17 cases) and `tst_book_editor_ocr` (9 cases) both green, and `tst_ocr` is
**clean under ThreadSanitizer** — the check that matters most for E4, since the result is
published across threads by a single release/acquire on `finished` rather than a mutex.

The fixture `tests/data/ocr_sample_eng.png` is checked in rather than rendered with
QPainter at test time: drawing text would make the assertions depend on whichever fonts the
build machine has, and a test that fails because a font changed tells you nothing about OCR.

The language-mapping and Job-lifecycle halves of `tst_ocr` run with **no engine installed** —
that is the point of them. The paths a customer hits when Tesseract is missing are exactly
the paths nobody exercises by hand. `cmake/OcrBundle.cmake` moved from the application to
`libraries/Ocr` so that a `BUILD_APPS=OFF` build links a real engine instead of silently
testing only the unavailable path, and gained a `VLMS_DISABLE_OCR` option — without it the
no-engine configuration cannot be built on a development machine at all, because clearing
the `find_library` cache entries just makes CMake find the system copy again next configure.

---

## Verification

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVLMS_BUILD_TESTS=ON && cmake --build build --parallel && ctest --test-dir build --output-on-failure --no-tests=error -LE realdb
```

Per-phase gates:

- **A** — suite is green with zero XFAILs and zero behavior change. `ctest -L injection` passes. `git diff` on `libraries/Core/src/*Repository.cpp` is empty.
- **B** — ✅ **done.** Green in all three CI configurations (default, `BUILD_APPS=OFF`, ASan/UBSan): 16 CTest entries, 0 failures, **33 XFAILs**. `ctest -V | grep XFAIL` is the live defect list, every line naming its finding number. `ctest -L tz` is green with both entries registered, and the entry currently spanning the UTC boundary carries finding 1 as XFAILs — see the direction-arming note in B3 for why the two entries are *not* expected to be red.
- **C** — each commit removes exactly one XFAIL and no other test changes state. After C4, **both** timezone entries pass at any hour of day.
- **D** — ✅ **done.** 980 cases across 20 CTest entries, 0 failures, **0 XFAILs**, green
  in all three CI configurations. `ctest -L realdb` green against both the production
  backup and the live catalog: row counts and `MAX(id)` identical on every table,
  `integrity_check = ok`, `foreign_key_check` empty, `user_version` at 1, source file
  digest unchanged. Normalization split **3043/24**, measured rather than estimated.
- **E** — ✅ **done.** 23 CTest entries, 0 failures. `ctest -L ocr` green both with the
  engine installed (17 + 9 cases) and under `-DVLMS_DISABLE_OCR=ON` (9 pass / 8 skip and
  5 pass / 4 skip), which is the configuration a customer without language packs runs.
  `tst_ocr` clean under ThreadSanitizer.

Manual smoke after C5/C6, since the loan dialogs change — **still outstanding**, and now
also worth doing because Phase D changes what the dialogs write into:

```bash
cmake --build build --target vlms && ./build/bin/vlms
```

Check out a book (due date defaults to borrow + 14, borrow date has a today maximum), extend it (floor is current due + 1, suggestion is +14 from there on both the current and overdue branches), return it (max is today), and confirm the Metrics page counts move as expected.

## Out of scope (noted, not fixed)

- Page-level multi-step write orchestration without transactions (`CatalogPage::addBook`, `MembersPage::addMember`).
- Table models — `QTableWidget` population is manual throughout; `QAbstractTableModel` would remove the hand-rolled paging, but that's a UI refactor.
- The i18n mechanism (compiled-in `QHash` rather than Qt `.ts`) — the parity test makes it safe, replacing it is separate work.
- `employees.last_login_at` (declared, never written or read) and `member_status_history` (written, never queried).

---

## Deviations from the plan, as built

Places where the delivered code differs from the plan text. Each was forced by a
test, not chosen for convenience.

**Phase D added a step the plan did not have, and split one it did.** D0 (fixtures plus
`tst_database_migrations`) had to come first — the plan itself flagged the missing
`tests/data/` as blocking — and D3a had to come out of D3, because finding 3 is a query
defect that constraints cannot close for the databases that actually have it. Both are
described at the head of Phase D.

**Finding 11 got no XFAIL, unlike findings 9 and 10.** Those were found by Phase A with
Phase C still ahead of them, so an XFAIL had somewhere to point. Finding 11 was found in
the last phase, and a commit whose test asserts that a migration is correctly broken,
followed ten minutes later by the fix, is archaeology rather than evidence. It was fixed
in the same commit and mutation-tested instead — unscoping the detector turns seven
cases red — which the Verification-method section below already establishes as this
project's alternative to an XFAIL.

**The 3040/27 normalization split is 3043/24.** The plan's numbers were estimated from a
shape survey before the parser existed; these are what the parser produces. The realdb
test asserts the measured pair and, more importantly, the invariant underneath it.

**`Application` no longer discards `Database::open()`'s result.** D2's "refuse to write"
had nothing to refuse to until it did. Out of the plan's scope as written, and required
for the plan's own decision to mean anything in the shipped app.

**`publication_date_original` is in `schema.sql`, not only in the migration.** The plan
only had the migration add it, which would have left a fresh database and an upgraded one
reporting the same version with different columns.

**C11b — `initialCopyCount < 1` is rejected, not clamped.** The plan allowed either
("reject ... or clamp to 1"); the Phase B characterization test assumed clamping and had
to be rewritten. Clamping invents a physical copy that is not on any shelf, and the
librarian discovers it by going to look for the book. `createBook` now fails with
"A book needs at least one copy." and leaves `outId` at 0.

**C11 — `isbn` is exempt from the blank-field unification, deliberately.** `isbn` is part
of `UNIQUE (title, author_id, publisher_id, isbn, language)`, and SQLite treats NULLs in a
unique index as *distinct*. Routing blank ISBNs through `nullableText` would have silently
switched off duplicate detection for exactly the books where a duplicate is hardest to
spot. Worse, the pre-existing behavior was already undecided: `QString().trimmed()` is
still a *null* QString and binds as NULL, while `QLineEdit::text()` on an empty field binds
as `''` — so a blank ISBN became `''` through the dialog and NULL from a default-constructed
`BookInput`. There is now a `blankIsbnSentinel()` helper and a test
(`twoBooksWithoutAnIsbnStillCollideOnTheUniqueConstraint`) that stops someone "finishing"
the unification later.

**Finding 5b — the characterization test was passing for the wrong reason.** After C5,
`extendLoanRejectsWhenStoredDueAtIsUnparseable` XPASSed, but not because 5b was fixed: it
extended to a date in the *past*, which C5's new floor rejects whatever is stored. The real
5b exposure is extending *forward* from an unreadable stored due date, where the repository
has nothing to compare against. The test was corrected to use a future date, and the
past-date half was split into `extendLoanRejectsAPastDateEvenWhenTheStoredDueAtIsUnparseable`.

### Behavioral consequences worth knowing

C6 makes `extendLoan` refuse a loan whose stored `due_at` is unparseable, and `returnLoan`
refuse one whose stored `borrowed_at` is. That is the correct fix for 2b/5b, but it means
any such row already in a database becomes **un-returnable and un-extendable** until it is
corrected; the error messages say so explicitly. `loans` is empty in production, so nothing
is affected today — but this is a real behavior change, not pure hardening.

D3a makes a loan whose due date cannot be read show up as **overdue** rather than as fine.
On a database the pre-flight declined to migrate, rows that were invisible will start
appearing in red on the circulation page and in the overdue KPI. That is the point — they
had been hidden from the one number that would have found them — but a librarian will
experience it as a count that went up on its own.

D5 rewrites `publication_date` across the whole catalog on first launch. Reversible with
`UPDATE books SET publication_date = publication_date_original`, and 24 values are left
exactly as they were, but 674 of the 3067 rows in the production copy change text.

D2 makes a database written by a **newer build** refuse to open, with a message instead of
a window. There is no way to run VLMS against a database from a later version, by
design.

### Verification method

Three fixes were mutation-tested rather than trusted green: C1 (reverting the repository
half turned exactly the three new tests red), C4 (making `bindTodayIfPresent` a no-op turns
four suites red while **SQLite reports nothing at all** — it just returns `is_overdue = 0`
for every row, exactly as the plan warned), and C8 (adding `#include <QSqlDatabase>` to
`MainWindow.cpp` becomes a fatal compile error).

Phase D added two more. **Finding 11** was mutation-tested by unscoping the detector in
`migrateBookLanguageIfNeeded`, which turns seven cases in `tst_database_migrations` red
with SQLite's own words, `database table is locked` — that is how it was found in the
first place. And the **splitter move** into `SqlText` was itself prompted by a live
failure rather than by tidiness: a semicolon inside a comment in a new fixture was cut in
half by the harness's own naive splitter and the remainder executed as SQL.

One environment note: there is **no Ninja on the development machine**, so the verification
command at the top of the Verification section only works in CI. Locally, drop `-G Ninja`
and use the default Unix Makefiles generator.

### Commit history (branch `Beta`)

```
8916c2a D6: verify Phase D against the library's actual catalog
ac0f2ef D5: publication dates in reduced-precision ISO, originals kept
cbdc16c D3 + D4: date constraints in the database, and a pre-flight that never bricks one
3fc082c D3a: a loan whose due date cannot be read is overdue, not fine
86d5bd6 D2: one schema version instead of four shape detectors
5de723d D1: split SQL scripts with a scanner instead of split(';')
45be8a8 D0: the migration chain gets its own tests, and one of them was failing
0928f69 C8: link Qt::Sql PRIVATE into Core
db4aa7f C11: one convention for blank fields, and real dates in date_of_birth
36b2e63 C10 + C11a + C11b: transaction, messages on every failure, no bookless books
457731e C7 + C9: an enum for the image column, a bound value for every timestamp
a1789b7 C6: parse loan dates before comparing them
a20973c C5: extract LoanPolicy and delete CirculationRepository::defaultLoanDays
965a59c C4: compare due dates against a bound :today, not SQLite's UTC date('now')
f70dc23 C3: resolve the metrics windows in C++ from the Clock, as bound dates
f9d7552 C2: hoist escapeLike and nullableText into Core/SqlText
bd9b8c4 C1: route every "today" through Clock
0e542e4 Phase B: Clock seam plus 26 characterization XFAILs for the date defects
52ac9c0 Phase A: test harness, monorepo split, and injection/regression suites
```
