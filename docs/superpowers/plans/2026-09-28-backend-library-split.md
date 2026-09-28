# Backend Library Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `libraries/Core` into three libraries: Core (std-only basics),
Database (SQLite and migrations) and Repositories (domain types and
repositories). Behaviour, APIs and namespaces stay as they are.

**Architecture:** Dependencies run one way only:
`vlms_ui → VLMS::Repositories → VLMS::Database → VLMS::Core`. Only Database
links sqlite3, and it links it `PRIVATE`. Ocr is untouched. This is a move and
nothing else: files move with `git mv`, include lines are rewritten, and
CMake targets are added. No function body changes.

**Tech Stack:** C++17, CMake ≥ 3.21 (Unix Makefiles, Debug), GoogleTest
v1.15.2 via FetchContent, Qt 6 Widgets (app only), SQLite 3.

**Spec:** `docs/superpowers/specs/2026-09-28-backend-library-split-design.md`

## Global Constraints

- No behaviour, API or namespace changes. The only edits allowed are file
  moves, `#include` lines, CMake files, comments that name moved paths,
  and `libraries/Database/test/data/README.md`.
- Move every file with `git mv`, never delete-and-recreate, so its history
  follows it.
- Targets:

  | Raw target | Alias | Include prefix |
  |---|---|---|
  | `vlms_core` | `VLMS::Core` | `VLMS/Core/` |
  | `vlms_database` | `VLMS::Database` | `VLMS/Database/` |
  | `vlms_repositories` | `VLMS::Repositories` | `VLMS/Repositories/` |

- Core links nothing (std only). Database links `VLMS::Core` `PUBLIC` and
  sqlite3 `PRIVATE`. Repositories links `VLMS::Database` `PUBLIC`.
- Test-support libraries:
  - `vlms_testsupport_core` (TestEnv)
  - `vlms_testsupport_database` (TestDatabase, SqlValue)
  - `vlms_testsupport` (TestSeed; the name is kept for the UI tests)
- gtest suite names (`test_core_*`) are **not** renamed.
- There are 574 gtest cases today. The sum over all Core, Database and
  Repositories test binaries must stay 574 after every task.
- Do not reorder include blocks or reformat code. Replace an include line
  in place.
- Every commit message ends with a blank line followed by
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Do not touch `.vscode/` (untracked, user's).

## Execution shape: one serial task, two in parallel, one closing task

```
Task 1 (serial, main checkout)
   ├──▶ Task 2  Database tests      (worktree, branch split/database-tests)     ┐ parallel
   └──▶ Task 3  Repositories tests  (worktree, branch split/repositories-tests) ┘
Controller merges 2, then 3, into main
Task 4 (serial, main checkout): closing
```

- **Task 1** moves all production code and test support in one step. The
  dependency chain allows no smaller step: moving Database out while the
  repositories are still in Core would create a Core ↔ Database cycle.
  Task 1 leaves every test in a single `test_vlms_core` binary.
- **Tasks 2 and 3** touch disjoint files, with one exception:
  `libraries/Core/test/CMakeLists.txt`. Each of them deletes only its own
  `set(...)` block there. Task 1 separates the blocks with comment lines
  that neither task touches, so the merges do not conflict.
- **Task 4** removes the leftover scaffolding and runs every check the
  spec asks for.

### Worktree setup for Tasks 2 and 3 (controller runs this after Task 1 is committed)

```bash
cd /home/amin/Dokumente/dev/VLMS
git worktree add ../VLMS-wt/database-tests -b split/database-tests
git worktree add ../VLMS-wt/repositories-tests -b split/repositories-tests
```

Each worktree configures its own `build/`. Worktrees have no network cache
and no copy of the gitignored real-DB backup, so the configure command is:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/amin/Dokumente/dev/VLMS/build/_deps/googletest-src \
  -DVLMS_TEST_REAL_DB=/home/amin/Dokumente/dev/VLMS/database/vlms.db.bak-20260814-232925
```

### Merge step (controller, after Tasks 2 and 3 are both approved)

```bash
cd /home/amin/Dokumente/dev/VLMS
git merge --no-ff split/database-tests -m "Merge the Database test split.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git merge --no-ff split/repositories-tests -m "Merge the Repositories test split.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git worktree remove ../VLMS-wt/database-tests
git worktree remove ../VLMS-wt/repositories-tests
git branch -d split/database-tests split/repositories-tests
```

A conflict can only happen in `libraries/Core/test/CMakeLists.txt`. If one
does, resolve it by keeping **both** deletions: neither
`DATABASE_TST_SOURCES` nor `REPOSITORIES_TST_SOURCES` remains, and neither
does the realdb block. Task 4 rewrites that file anyway.

---

### Task 1: Move production code and test support into three libraries

**Files:**
- Modify: `libraries/CMakeLists.txt`
- Modify: `libraries/Core/CMakeLists.txt`
- Modify: `libraries/Core/test/CMakeLists.txt`
- Create: `libraries/Database/CMakeLists.txt`
- Create: `libraries/Database/test/CMakeLists.txt`
- Create: `libraries/Repositories/CMakeLists.txt`
- Create: `libraries/Repositories/test/CMakeLists.txt`
- Modify: `cmake/Sqlite.cmake` (comment only)
- Modify: `applications/vlms/CMakeLists.txt` (link line)
- Move (git mv): the files listed in Step 1
- Modify (include lines only): every `.h`/`.cpp` under `libraries/` and
  `applications/` that includes a moved header

**Interfaces:**
- Consumes: nothing; this is the first task.
- Produces, for later tasks:
  - Targets `vlms_core`, `vlms_database`, `vlms_repositories`, with the
    aliases above.
  - Support targets `vlms_testsupport_core` (in `libraries/Core/test`),
    `vlms_testsupport_database` (in `libraries/Database/test`) and
    `vlms_testsupport` (in `libraries/Repositories/test`).
  - `libraries/Database/test/CMakeLists.txt` and
    `libraries/Repositories/test/CMakeLists.txt` contain **only** their
    support library. Tasks 2 and 3 append the test binaries.
  - `libraries/Core/test/CMakeLists.txt` defines three source lists,
    `CORE_TST_SOURCES`, `DATABASE_TST_SOURCES` and
    `REPOSITORIES_TST_SOURCES`, plus a trailing realdb block, exactly as
    in Step 6.
  - Public headers: `<VLMS/Core/Text.h>`,
    `<VLMS/Database/{Database,SqliteSession,SqlText}.h>`, and
    `<VLMS/Repositories/{ArchiveTypes,CatalogRepository,CatalogTypes,CirculationRepository,LoanPolicy,LoanTypes,MemberRepository,MemberTypes,MetricsRepository,MetricsTypes}.h>`.

This task has no new failing test to write. The behaviour under test does
not change. The safety net is the existing suite (289 ctest entries, 574
gtest cases), and it must stay green.

- [ ] **Step 1: Record the baseline**

Run:
```bash
cd /home/amin/Dokumente/dev/VLMS
build/bin/test_vlms_core --gtest_list_tests | grep -c '^  '
```
Expected: `574`

- [ ] **Step 2: Move the files**

```bash
cd /home/amin/Dokumente/dev/VLMS
mkdir -p libraries/Database/include/VLMS/Database libraries/Database/src \
         libraries/Database/test/support \
         libraries/Repositories/include/VLMS/Repositories libraries/Repositories/src \
         libraries/Repositories/test/support

# Core: Text.h becomes public
git mv libraries/Core/src/Text.h libraries/Core/include/VLMS/Core/Text.h

# Database
for h in Database SqlText; do
  git mv libraries/Core/include/VLMS/Core/$h.h libraries/Database/include/VLMS/Database/$h.h
done
git mv libraries/Core/src/SqliteSession.h libraries/Database/include/VLMS/Database/SqliteSession.h
for f in Database.cpp SqliteSession.cpp SqlText.cpp sqlite3_api.h; do
  git mv libraries/Core/src/$f libraries/Database/src/$f
done

# Repositories
for h in ArchiveTypes CatalogRepository CatalogTypes CirculationRepository LoanPolicy \
         LoanTypes MemberRepository MemberTypes MetricsRepository MetricsTypes; do
  git mv libraries/Core/include/VLMS/Core/$h.h libraries/Repositories/include/VLMS/Repositories/$h.h
done
for f in RepoSql.h BookSql.h BookSql.cpp MemberSql.h MemberSql.cpp LoanSql.h LoanSql.cpp \
         NamedEntityStore.h NamedEntityStore.cpp BookCopyStore.h BookCopyStore.cpp \
         CategoryStore.h CategoryStore.cpp LoanPolicy.cpp CatalogRepository.cpp \
         MemberRepository.cpp CirculationRepository.cpp MetricsRepository.cpp; do
  git mv libraries/Core/src/$f libraries/Repositories/src/$f
done

# Test support
for f in SqlValue.h TestDatabase.h TestDatabase.cpp; do
  git mv libraries/Core/test/support/$f libraries/Database/test/support/$f
done
for f in TestSeed.h TestSeed.cpp; do
  git mv libraries/Core/test/support/$f libraries/Repositories/test/support/$f
done
```

Then check what's left:
```bash
ls libraries/Core/src libraries/Core/include/VLMS/Core libraries/Core/test/support
```
Expected:
- `src`: `Clock.cpp Date.cpp DateText.cpp Locale.cpp Paths.cpp Strings.cpp`
- `include`: `Clock.h Date.h DateText.h Locale.h Paths.h Result.h Strings.h Text.h`
- `support`: `TestEnv.cpp TestEnv.h`

- [ ] **Step 3: Rewrite the include lines**

```bash
cd /home/amin/Dokumente/dev/VLMS
FILES=$(grep -rlE '<VLMS/Core/(ArchiveTypes|CatalogRepository|CatalogTypes|CirculationRepository|LoanPolicy|LoanTypes|MemberRepository|MemberTypes|MetricsRepository|MetricsTypes|Database|SqlText)\.h>|"SqliteSession\.h"|"Text\.h"' \
        libraries applications --include='*.h' --include='*.cpp')
sed -i -E \
  -e 's#<VLMS/Core/(ArchiveTypes|CatalogRepository|CatalogTypes|CirculationRepository|LoanPolicy|LoanTypes|MemberRepository|MemberTypes|MetricsRepository|MetricsTypes)\.h>#<VLMS/Repositories/\1.h>#g' \
  -e 's#<VLMS/Core/(Database|SqlText)\.h>#<VLMS/Database/\1.h>#g' \
  -e 's#"SqliteSession\.h"#<VLMS/Database/SqliteSession.h>#g' \
  -e 's#"Text\.h"#<VLMS/Core/Text.h>#g' \
  $FILES
```

Verify nothing is left:
```bash
grep -rnE '<VLMS/Core/(ArchiveTypes|CatalogRepository|CatalogTypes|CirculationRepository|LoanPolicy|LoanTypes|MemberRepository|MemberTypes|MetricsRepository|MetricsTypes|Database|SqlText|SqliteSession)\.h>|"SqliteSession\.h"|"Text\.h"' libraries applications
```
Expected: no output.

- [ ] **Step 4: Rewrite `libraries/CMakeLists.txt`**

Replace the whole file with:
```cmake
# Development builds resolve database/schema and the project root from the
# source tree. Core reads the root and Database the schema, so the option
# lives here where both see the same value.
option(VLMS_DEV_PATHS "Use source-tree paths for database and schema (development)" ON)
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set(VLMS_DEV_PATHS OFF)
endif()

add_subdirectory(Ocr)
add_subdirectory(Core)
add_subdirectory(Database)
add_subdirectory(Repositories)
```

- [ ] **Step 5: Write the three library CMake files**

Replace `libraries/Core/CMakeLists.txt` with:
```cmake
set(TARGET_NAME Core)
set(RAW_TARGET_NAME vlms_core)

set(CORE_HEADERS
    include/VLMS/Core/Clock.h
    include/VLMS/Core/Date.h
    include/VLMS/Core/DateText.h
    include/VLMS/Core/Locale.h
    include/VLMS/Core/Paths.h
    include/VLMS/Core/Result.h
    include/VLMS/Core/Strings.h
    include/VLMS/Core/Text.h
)

add_library(${RAW_TARGET_NAME}
    ${CORE_HEADERS}
    src/Clock.cpp
    src/Date.cpp
    src/DateText.cpp
    src/Locale.cpp
    src/Paths.cpp
    src/Strings.cpp
)
add_library(${CMAKE_ROOT_NAME}::${TARGET_NAME} ALIAS ${RAW_TARGET_NAME})

target_include_directories(${RAW_TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# No links, deliberately: Core is std only. SQLite belongs to VLMS::Database,
# and Qt to the application.

vlms_configure_target(${RAW_TARGET_NAME})

# QtSupport turns AUTOMOC on for the whole project. Core has no Q_OBJECT.
set_target_properties(${RAW_TARGET_NAME} PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

if(VLMS_DEV_PATHS)
    target_compile_definitions(${RAW_TARGET_NAME} PRIVATE
        VLMS_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    )
endif()

install(TARGETS ${RAW_TARGET_NAME}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
```

Create `libraries/Database/CMakeLists.txt`:
```cmake
set(TARGET_NAME Database)
set(RAW_TARGET_NAME vlms_database)

include(${CMAKE_SOURCE_DIR}/cmake/Sqlite.cmake)

set(DATABASE_HEADERS
    include/VLMS/Database/Database.h
    include/VLMS/Database/SqliteSession.h
    include/VLMS/Database/SqlText.h
)

add_library(${RAW_TARGET_NAME}
    ${DATABASE_HEADERS}
    src/sqlite3_api.h
    src/Database.cpp
    src/SqliteSession.cpp
    src/SqlText.cpp
)
add_library(${CMAKE_ROOT_NAME}::${TARGET_NAME} ALIAS ${RAW_TARGET_NAME})

target_include_directories(${RAW_TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# sqlite3 is PRIVATE: SqliteSession.h only forward-declares sqlite3, so no
# consumer of this library ever sees the C API.
target_link_libraries(${RAW_TARGET_NAME}
    PUBLIC
        ${CMAKE_ROOT_NAME}::Core
    PRIVATE
        ${VLMS_SQLITE3_TARGET}
)

if(VLMS_SQLITE3_USE_SYSTEM_HEADER)
    target_compile_definitions(${RAW_TARGET_NAME} PRIVATE VLMS_HAS_SQLITE3_H)
endif()

vlms_configure_target(${RAW_TARGET_NAME})

set_target_properties(${RAW_TARGET_NAME} PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

if(VLMS_DEV_PATHS)
    target_compile_definitions(${RAW_TARGET_NAME} PRIVATE
        VLMS_SCHEMA_PATH="${CMAKE_SOURCE_DIR}/database/schema.sql"
    )
endif()

install(TARGETS ${RAW_TARGET_NAME}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
```

Create `libraries/Repositories/CMakeLists.txt`:
```cmake
set(TARGET_NAME Repositories)
set(RAW_TARGET_NAME vlms_repositories)

set(REPOSITORIES_HEADERS
    include/VLMS/Repositories/ArchiveTypes.h
    include/VLMS/Repositories/CatalogRepository.h
    include/VLMS/Repositories/CatalogTypes.h
    include/VLMS/Repositories/CirculationRepository.h
    include/VLMS/Repositories/LoanPolicy.h
    include/VLMS/Repositories/LoanTypes.h
    include/VLMS/Repositories/MemberRepository.h
    include/VLMS/Repositories/MemberTypes.h
    include/VLMS/Repositories/MetricsRepository.h
    include/VLMS/Repositories/MetricsTypes.h
)

add_library(${RAW_TARGET_NAME}
    ${REPOSITORIES_HEADERS}
    src/RepoSql.h
    src/BookSql.h
    src/BookSql.cpp
    src/MemberSql.h
    src/MemberSql.cpp
    src/LoanSql.h
    src/LoanSql.cpp
    src/NamedEntityStore.h
    src/NamedEntityStore.cpp
    src/BookCopyStore.h
    src/BookCopyStore.cpp
    src/CategoryStore.h
    src/CategoryStore.cpp
    src/LoanPolicy.cpp
    src/CatalogRepository.cpp
    src/MemberRepository.cpp
    src/CirculationRepository.cpp
    src/MetricsRepository.cpp
)
add_library(${CMAKE_ROOT_NAME}::${TARGET_NAME} ALIAS ${RAW_TARGET_NAME})

target_include_directories(${RAW_TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${RAW_TARGET_NAME}
    PUBLIC
        ${CMAKE_ROOT_NAME}::Database
)

vlms_configure_target(${RAW_TARGET_NAME})

set_target_properties(${RAW_TARGET_NAME} PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

install(TARGETS ${RAW_TARGET_NAME}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
```

- [ ] **Step 6: Write the three test CMake files**

Replace `libraries/Core/test/CMakeLists.txt` with the file below. Keep
every `# ---` comment line exactly as written: Tasks 2 and 3 delete the
blocks between them, and the comment lines are what keep their merges
apart.
```cmake
add_library(vlms_testsupport_core STATIC
    support/TestEnv.cpp
    support/TestEnv.h
)

target_include_directories(vlms_testsupport_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/support
)

target_link_libraries(vlms_testsupport_core
    PUBLIC
        ${CMAKE_ROOT_NAME}::Core
        GTest::gtest
)

vlms_configure_target(vlms_testsupport_core)
set_target_properties(vlms_testsupport_core PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

set(CORE_TST_SOURCES
    src/test_result.cpp
    src/test_clock.cpp
    src/test_date_text.cpp
    src/test_licence_strings.cpp
)

# --- Database tests: moved to libraries/Database/test by the Database test task ---

set(DATABASE_TST_SOURCES
    src/test_sql_text.cpp
    src/test_sqlite_session.cpp
    src/test_sql_script.cpp
    src/test_database_schema.cpp
    src/test_database_migrations.cpp
    src/test_database_file.cpp
    src/test_database_realdb.cpp
)

# --- Repositories tests: moved to libraries/Repositories/test by the Repositories test task ---

set(REPOSITORIES_TST_SOURCES
    src/test_loan_policy.cpp
    src/test_strings_parity.cpp
    src/test_catalog_repository.cpp
    src/test_member_repository.cpp
    src/test_member_status_rule.cpp
    src/test_member_status_expiry.cpp
    src/test_circulation_repository.cpp
    src/test_circulation_dates.cpp
    src/test_loan_book_filter.cpp
    src/test_list_facets.cpp
    src/test_metrics_repository.cpp
    src/test_catalog_injection.cpp
    src/test_member_injection.cpp
    src/test_circulation_injection.cpp
    src/test_metrics_windows.cpp
    src/test_circulation_timezone.cpp
    src/test_archive_catalog.cpp
    src/test_archive_circulation.cpp
    src/test_archive_purge.cpp
    src/test_free_local_numbers.cpp
    src/test_archive_members.cpp
    src/test_archive_reuse.cpp
    src/test_can_remove.cpp
)

# --- Until both blocks above have moved, this binary runs all three sets. ---

build_gtest_executable(
    NAME test_vlms_core
    SRC ${CORE_TST_SOURCES} ${DATABASE_TST_SOURCES} ${REPOSITORIES_TST_SOURCES}
    DEPENDS vlms_testsupport
    DISCOVER OFF
    GTEST_FILTER -test_core_Timezone.*:test_core_RealDb.*
    LABELS core
    # One ctest entry runs every test in the binary. The default 60s is that
    # whole budget; a slow runner spent it inside the catalogue search tests.
    TIMEOUT 300
    TIMEZONES Pacific/Kiritimati Pacific/Midway
    TIMEZONE_FILTER test_core_Timezone.*
)

# --- Real database: moved to libraries/Database/test by the Database test task ---

set(VLMS_TEST_REAL_DB "${CMAKE_SOURCE_DIR}/database/vlms.db.bak-20260814-232925"
    CACHE FILEPATH "A real VLMS database to verify migrations against")

if(VLMS_TEST_REAL_DB AND EXISTS "${VLMS_TEST_REAL_DB}")
    set(_vlms_test_timeout 300)
    set(_vlms_test_labels "realdb;dates")
    set(_vlms_test_environment "VLMS_TEST_REAL_DB=${VLMS_TEST_REAL_DB}")
    _vlms_add_gtest_entry(test_vlms_core_realdb test_vlms_core "Africa/Tunis"
        --gtest_filter=test_core_RealDb.*)
else()
    message(STATUS
        "test_vlms_core RealDb: not registered, no database at '${VLMS_TEST_REAL_DB}'")
endif()
```

Create `libraries/Database/test/CMakeLists.txt`:
```cmake
add_library(vlms_testsupport_database STATIC
    support/SqlValue.h
    support/TestDatabase.cpp
    support/TestDatabase.h
)

target_include_directories(vlms_testsupport_database
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/support
)

target_link_libraries(vlms_testsupport_database
    PUBLIC
        vlms_testsupport_core
        ${CMAKE_ROOT_NAME}::Database
        GTest::gtest
)

vlms_configure_target(vlms_testsupport_database)
set_target_properties(vlms_testsupport_database PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
```

Create `libraries/Repositories/test/CMakeLists.txt`:
```cmake
# vlms_testsupport keeps its old name: applications/vlms/test links it.
add_library(vlms_testsupport STATIC
    support/TestSeed.cpp
    support/TestSeed.h
)

target_include_directories(vlms_testsupport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/support
)

target_link_libraries(vlms_testsupport
    PUBLIC
        vlms_testsupport_database
        ${CMAKE_ROOT_NAME}::Repositories
        GTest::gtest
)

vlms_configure_target(vlms_testsupport)
set_target_properties(vlms_testsupport PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)
```

- [ ] **Step 7: Point the app and the SQLite comment at the new layout**

In `applications/vlms/CMakeLists.txt`, in the `target_link_libraries(vlms_ui`
block, change:
```cmake
    PUBLIC
        ${CMAKE_ROOT_NAME}::Core
```
to:
```cmake
    PUBLIC
        ${CMAKE_ROOT_NAME}::Repositories
```

In `cmake/Sqlite.cmake`, replace the first three comment lines:
```cmake
# SQLite for Core. Prefers a development package (SQLite::SQLite3). Falls
# back to the runtime .so (libsqlite3.so.0) plus the declarations in
# libraries/Core/src/sqlite3_api.h, which is enough to call the C API without
```
with:
```cmake
# SQLite for Database. Prefers a development package (SQLite::SQLite3). Falls
# back to the runtime .so (libsqlite3.so.0) plus the declarations in
# libraries/Database/src/sqlite3_api.h, which is enough to call the C API without
```
and in the same file change `"SQLite 3 is required for Core. Install` to
`"SQLite 3 is required for Database. Install`.

- [ ] **Step 8: Configure, build, run the tests**

```bash
cd /home/amin/Dokumente/dev/VLMS
cmake -S . -B build 2>&1 | tail -3
cmake --build build -j8 2>&1 | grep -E "error|warning: .*#include|Error " ; echo "build exit: ${PIPESTATUS[0]}"
ctest --test-dir build -j8 2>&1 | grep -E "tests passed|tests failed"
build/bin/test_vlms_core --gtest_list_tests | grep -c '^  '
```
Expected:
- `build exit: 0`, with no error lines
- `100% tests passed, 0 tests failed out of 289`
- `574`

If the build fails with a missing header, a file still includes an old
path. Fix the include line, never the CMake visibility.

- [ ] **Step 9: Check the boundaries**

```bash
cd /home/amin/Dokumente/dev/VLMS
grep -n "target_link_libraries\|sqlite" libraries/Core/CMakeLists.txt
grep -rn "Core/src" --include=CMakeLists.txt libraries applications
grep -rn "VLMS/Repositories\|VLMS/Database" libraries/Core
grep -rn "VLMS/Repositories" libraries/Database
```
Expected: no output from any of the four. The Core CMake comment spells
it `SQLite`, which the case-sensitive `sqlite` does not match.

- [ ] **Step 10: Commit**

```bash
cd /home/amin/Dokumente/dev/VLMS
git add -A libraries applications cmake
git status --short | grep -v '^R\|^M\|^A' ; true
git commit -q -m "Split Core into Core, Database and Repositories libraries.

Files move and include lines follow; no code changes. Tests stay in the
single test_vlms_core binary for now.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git log --oneline -1
```
Expected: the `grep -v` line prints only `?? .vscode/`, or nothing.

---

### Task 2: Give Database its own test binary (parallel with Task 3)

**Runs in:** worktree `../VLMS-wt/database-tests`, branch
`split/database-tests`. Configure `build/` with the command in
"Worktree setup" above. Every path below is relative to the worktree
root.

**Files:**
- Move (git mv): 7 test sources from `libraries/Core/test/src/` to
  `libraries/Database/test/src/`
- Move (git mv): `libraries/Core/test/data/` → `libraries/Database/test/data/`
- Modify: `libraries/Database/test/CMakeLists.txt` (append)
- Modify: `libraries/Core/test/CMakeLists.txt` (delete two blocks only)
- Modify: `cmake/TestUtils.cmake` (two path strings)
- Modify: `libraries/Database/test/data/README.md`

**Interfaces:**
- Consumes: `vlms_testsupport_database` from Task 1, and the
  `DATABASE_TST_SOURCES` block and realdb block in
  `libraries/Core/test/CMakeLists.txt`.
- Produces: test target `test_vlms_database` (label `database`) and ctest
  entry `test_vlms_database_realdb` (labels `realdb;dates`).
- **Must not touch:** anything else in `libraries/Core/test/CMakeLists.txt`,
  including the `# ---` comment lines and the `build_gtest_executable`
  call. Task 3 edits the same file in parallel.

- [ ] **Step 1: Record the baseline in this worktree**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/amin/Dokumente/dev/VLMS/build/_deps/googletest-src \
  -DVLMS_TEST_REAL_DB=/home/amin/Dokumente/dev/VLMS/database/vlms.db.bak-20260814-232925
cmake --build build -j8 --target test_vlms_core
build/bin/test_vlms_core --gtest_list_tests | grep -c '^  '
```
Expected: `574`

- [ ] **Step 2: Move the sources and the fixtures**

```bash
mkdir -p libraries/Database/test/src
for f in test_sql_text test_sqlite_session test_sql_script test_database_schema \
         test_database_migrations test_database_file test_database_realdb; do
  git mv libraries/Core/test/src/$f.cpp libraries/Database/test/src/$f.cpp
done
git mv libraries/Core/test/data libraries/Database/test/data
```

- [ ] **Step 3: Remove the moved blocks from `libraries/Core/test/CMakeLists.txt`**

Delete exactly these lines and nothing else. Keep the `# ---` comment
lines and the blank lines around them.

The `DATABASE_TST_SOURCES` block:
```cmake
set(DATABASE_TST_SOURCES
    src/test_sql_text.cpp
    src/test_sqlite_session.cpp
    src/test_sql_script.cpp
    src/test_database_schema.cpp
    src/test_database_migrations.cpp
    src/test_database_file.cpp
    src/test_database_realdb.cpp
)
```

Everything **after** the line
`# --- Real database: moved to libraries/Database/test by the Database test task ---`,
from the `set(VLMS_TEST_REAL_DB` line to the final `endif()`.

`${DATABASE_TST_SOURCES}` in the `SRC` line now expands to nothing. Leave
that line alone; Task 4 removes it.

- [ ] **Step 4: Append the binary to `libraries/Database/test/CMakeLists.txt`**

Append after the existing support-library block:
```cmake

set(TST_SOURCES
    src/test_sql_text.cpp
    src/test_sqlite_session.cpp
    src/test_sql_script.cpp
    src/test_database_schema.cpp
    src/test_database_migrations.cpp
    src/test_database_file.cpp
    src/test_database_realdb.cpp
)

build_gtest_executable(
    NAME test_vlms_database
    SRC ${TST_SOURCES}
    DEPENDS vlms_testsupport_database
    DISCOVER OFF
    GTEST_FILTER -test_core_RealDb.*
    LABELS database
    TIMEOUT 300
)

set(VLMS_TEST_REAL_DB "${CMAKE_SOURCE_DIR}/database/vlms.db.bak-20260814-232925"
    CACHE FILEPATH "A real VLMS database to verify migrations against")

if(VLMS_TEST_REAL_DB AND EXISTS "${VLMS_TEST_REAL_DB}")
    set(_vlms_test_timeout 300)
    set(_vlms_test_labels "realdb;dates")
    set(_vlms_test_environment "VLMS_TEST_REAL_DB=${VLMS_TEST_REAL_DB}")
    _vlms_add_gtest_entry(test_vlms_database_realdb test_vlms_database "Africa/Tunis"
        --gtest_filter=test_core_RealDb.*)
else()
    message(STATUS
        "test_vlms_database RealDb: not registered, no database at '${VLMS_TEST_REAL_DB}'")
endif()
```

- [ ] **Step 5: Point the test environment at the moved fixtures**

In `cmake/TestUtils.cmake`, replace both occurrences of
`libraries/Core/test/data` with `libraries/Database/test/data`:
```bash
sed -i 's#libraries/Core/test/data#libraries/Database/test/data#g' cmake/TestUtils.cmake
grep -c "libraries/Database/test/data" cmake/TestUtils.cmake
```
Expected: `2`

- [ ] **Step 6: Update the fixtures README**

In `libraries/Database/test/data/README.md`:
- Change the first line `# libraries/Core/test/data` to
  `# libraries/Database/test/data`.
- Change `` `libraries/Core/src/Database.cpp` `` to
  `` `libraries/Database/src/Database.cpp` ``.

```bash
grep -n "libraries/" libraries/Database/test/data/README.md
```
Expected: two lines, both naming `libraries/Database/...`.

- [ ] **Step 7: Build and run everything**

```bash
cmake -S . -B build 2>&1 | grep -i "realdb"
cmake --build build -j8 2>&1 | grep -E " error |Error " ; echo "build exit: ${PIPESTATUS[0]}"
ctest --test-dir build -j8 2>&1 | grep -E "tests passed|tests failed"
ctest --test-dir build -N | grep -E "test_vlms_(core|database)"
echo $(( $(build/bin/test_vlms_core --gtest_list_tests | grep -c '^  ') + $(build/bin/test_vlms_database --gtest_list_tests | grep -c '^  ') ))
```
Expected:
- The configure prints no `RealDb: not registered` line.
- `build exit: 0`
- `100% tests passed, 0 tests failed out of 290`
- The ctest listing shows `test_vlms_core`,
  `test_vlms_core_pacific_kiritimati`, `test_vlms_core_pacific_midway`,
  `test_vlms_database` and `test_vlms_database_realdb`, and no
  `test_vlms_core_realdb`.
- `574`

- [ ] **Step 8: Commit on the branch**

```bash
git add -A libraries cmake
git commit -q -m "Give Database its own test binary and fixtures.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git log --oneline -1
```

---

### Task 3: Give Repositories their own test binary (parallel with Task 2)

**Runs in:** worktree `../VLMS-wt/repositories-tests`, branch
`split/repositories-tests`. Configure `build/` with the command in
"Worktree setup" above. Every path below is relative to the worktree
root.

**Files:**
- Move (git mv): 23 test sources from `libraries/Core/test/src/` to
  `libraries/Repositories/test/src/`
- Modify: `libraries/Repositories/test/CMakeLists.txt` (append)
- Modify: `libraries/Core/test/CMakeLists.txt` (delete one block only)

**Interfaces:**
- Consumes: `vlms_testsupport` from Task 1, and the
  `REPOSITORIES_TST_SOURCES` block in `libraries/Core/test/CMakeLists.txt`.
- Produces: test target `test_vlms_repositories` (label `repositories`),
  plus timezone entries `test_vlms_repositories_pacific_kiritimati` and
  `test_vlms_repositories_pacific_midway`.
- **Must not touch:** anything else in `libraries/Core/test/CMakeLists.txt`,
  including the `# ---` comment lines, the `build_gtest_executable` call
  and the realdb block. Task 2 edits the same file in parallel.
  `cmake/TestUtils.cmake` and `libraries/Core/test/data/` also belong to
  Task 2.

- [ ] **Step 1: Record the baseline in this worktree**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/amin/Dokumente/dev/VLMS/build/_deps/googletest-src \
  -DVLMS_TEST_REAL_DB=/home/amin/Dokumente/dev/VLMS/database/vlms.db.bak-20260814-232925
cmake --build build -j8 --target test_vlms_core
build/bin/test_vlms_core --gtest_list_tests | grep -c '^  '
```
Expected: `574`

- [ ] **Step 2: Move the sources**

```bash
mkdir -p libraries/Repositories/test/src
for f in test_loan_policy test_strings_parity test_catalog_repository test_member_repository \
         test_member_status_rule test_member_status_expiry test_circulation_repository \
         test_circulation_dates test_loan_book_filter test_list_facets test_metrics_repository \
         test_catalog_injection test_member_injection test_circulation_injection \
         test_metrics_windows test_circulation_timezone test_archive_catalog \
         test_archive_circulation test_archive_purge test_free_local_numbers \
         test_archive_members test_archive_reuse test_can_remove; do
  git mv libraries/Core/test/src/$f.cpp libraries/Repositories/test/src/$f.cpp
done
ls libraries/Repositories/test/src | wc -l
```
Expected: `23`

- [ ] **Step 3: Remove the moved block from `libraries/Core/test/CMakeLists.txt`**

Delete exactly this block and nothing else. Keep the `# ---` comment lines
and the blank lines around them.
```cmake
set(REPOSITORIES_TST_SOURCES
    src/test_loan_policy.cpp
    src/test_strings_parity.cpp
    src/test_catalog_repository.cpp
    src/test_member_repository.cpp
    src/test_member_status_rule.cpp
    src/test_member_status_expiry.cpp
    src/test_circulation_repository.cpp
    src/test_circulation_dates.cpp
    src/test_loan_book_filter.cpp
    src/test_list_facets.cpp
    src/test_metrics_repository.cpp
    src/test_catalog_injection.cpp
    src/test_member_injection.cpp
    src/test_circulation_injection.cpp
    src/test_metrics_windows.cpp
    src/test_circulation_timezone.cpp
    src/test_archive_catalog.cpp
    src/test_archive_circulation.cpp
    src/test_archive_purge.cpp
    src/test_free_local_numbers.cpp
    src/test_archive_members.cpp
    src/test_archive_reuse.cpp
    src/test_can_remove.cpp
)
```

`${REPOSITORIES_TST_SOURCES}` in the `SRC` line now expands to nothing,
and the core timezone entries now select zero tests (gtest v1.15.2 exits
0 on an empty filter). Leave both alone; Task 4 removes them.

- [ ] **Step 4: Append the binary to `libraries/Repositories/test/CMakeLists.txt`**

Append after the existing support-library block:
```cmake

set(TST_SOURCES
    src/test_loan_policy.cpp
    src/test_strings_parity.cpp
    src/test_catalog_repository.cpp
    src/test_member_repository.cpp
    src/test_member_status_rule.cpp
    src/test_member_status_expiry.cpp
    src/test_circulation_repository.cpp
    src/test_circulation_dates.cpp
    src/test_loan_book_filter.cpp
    src/test_list_facets.cpp
    src/test_metrics_repository.cpp
    src/test_catalog_injection.cpp
    src/test_member_injection.cpp
    src/test_circulation_injection.cpp
    src/test_metrics_windows.cpp
    src/test_circulation_timezone.cpp
    src/test_archive_catalog.cpp
    src/test_archive_circulation.cpp
    src/test_archive_purge.cpp
    src/test_free_local_numbers.cpp
    src/test_archive_members.cpp
    src/test_archive_reuse.cpp
    src/test_can_remove.cpp
)

build_gtest_executable(
    NAME test_vlms_repositories
    SRC ${TST_SOURCES}
    DEPENDS vlms_testsupport
    DISCOVER OFF
    GTEST_FILTER -test_core_Timezone.*
    LABELS repositories
    # One ctest entry runs every test in the binary. The default 60s is that
    # whole budget; a slow runner spent it inside the catalogue search tests.
    TIMEOUT 300
    TIMEZONES Pacific/Kiritimati Pacific/Midway
    TIMEZONE_FILTER test_core_Timezone.*
)
```

- [ ] **Step 5: Build and run everything**

```bash
cmake -S . -B build > /dev/null
cmake --build build -j8 2>&1 | grep -E " error |Error " ; echo "build exit: ${PIPESTATUS[0]}"
ctest --test-dir build -j8 2>&1 | grep -E "tests passed|tests failed"
ctest --test-dir build -N | grep -E "test_vlms_(core|repositories)"
echo $(( $(build/bin/test_vlms_core --gtest_list_tests | grep -c '^  ') + $(build/bin/test_vlms_repositories --gtest_list_tests | grep -c '^  ') ))
build/bin/test_vlms_repositories --gtest_filter='test_core_Timezone.*' --gtest_list_tests | grep -c '^  '
```
Expected:
- `build exit: 0`
- `100% tests passed, 0 tests failed out of 292`
- The listing shows `test_vlms_core`, `test_vlms_core_pacific_kiritimati`,
  `test_vlms_core_pacific_midway`, `test_vlms_core_realdb`,
  `test_vlms_repositories`, `test_vlms_repositories_pacific_kiritimati`
  and `test_vlms_repositories_pacific_midway`.
- `574`
- A non-zero count: the timezone suite now lives in this binary.

- [ ] **Step 6: Commit on the branch**

```bash
git add -A libraries
git commit -q -m "Give Repositories their own test binary.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git log --oneline -1
```

---

### Task 4: Closing: remove the scaffolding and verify the spec

**Runs in:** the main checkout `/home/amin/Dokumente/dev/VLMS`, after the
merge step above.

**Files:**
- Modify: `libraries/Core/test/CMakeLists.txt` (final form)

**Interfaces:**
- Consumes: the merged result of Tasks 1–3.
- Produces: the finished layout the spec describes, with no leftover
  transition variables.

- [ ] **Step 1: Confirm the merge left only core tests in Core**

```bash
cd /home/amin/Dokumente/dev/VLMS
ls libraries/Core/test/src
ls libraries/Core/test
```
Expected:
- `src`: `test_clock.cpp test_date_text.cpp test_licence_strings.cpp test_result.cpp`
- `test`: `CMakeLists.txt src support` (there is no `data` directory any
  more)

- [ ] **Step 2: Write the final `libraries/Core/test/CMakeLists.txt`**

Replace the whole file with:
```cmake
add_library(vlms_testsupport_core STATIC
    support/TestEnv.cpp
    support/TestEnv.h
)

target_include_directories(vlms_testsupport_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/support
)

target_link_libraries(vlms_testsupport_core
    PUBLIC
        ${CMAKE_ROOT_NAME}::Core
        GTest::gtest
)

vlms_configure_target(vlms_testsupport_core)
set_target_properties(vlms_testsupport_core PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

set(TST_SOURCES
    src/test_result.cpp
    src/test_clock.cpp
    src/test_date_text.cpp
    src/test_licence_strings.cpp
)

build_gtest_executable(
    NAME test_vlms_core
    SRC ${TST_SOURCES}
    DEPENDS vlms_testsupport_core
    DISCOVER OFF
    LABELS core
)
```
`test_vlms_core` now links only Core's own support. If it fails to link,
a core test is using something from Database or Repositories: move that
test to the right library instead of adding a dependency.

- [ ] **Step 3: Rebuild from a clean configure and run everything**

```bash
cd /home/amin/Dokumente/dev/VLMS
cmake -S . -B build > /dev/null
cmake --build build -j8 2>&1 | grep -E " error |Error " ; echo "build exit: ${PIPESTATUS[0]}"
ctest --test-dir build -j8 2>&1 | grep -E "tests passed|tests failed"
ctest --test-dir build -N | grep -E "test_vlms_(core|database|repositories)"
echo $(( $(build/bin/test_vlms_core --gtest_list_tests | grep -c '^  ') \
       + $(build/bin/test_vlms_database --gtest_list_tests | grep -c '^  ') \
       + $(build/bin/test_vlms_repositories --gtest_list_tests | grep -c '^  ') ))
```
Expected:
- `build exit: 0`
- `100% tests passed, 0 tests failed out of 291`. That is 283 ui + 1
  cmake_GitVersion + 1 ocr + 1 core + 2 database + 3 repositories.
- The listing shows exactly `test_vlms_core`, `test_vlms_database`,
  `test_vlms_database_realdb`, `test_vlms_repositories`,
  `test_vlms_repositories_pacific_kiritimati` and
  `test_vlms_repositories_pacific_midway`.
- `574`

- [ ] **Step 4: Run the spec's boundary checks**

```bash
cd /home/amin/Dokumente/dev/VLMS
grep -rnE "VLMS/Core/(Database|SqliteSession|SqlText|[A-Za-z]*Repository|[A-Za-z]*Types|LoanPolicy)\.h" libraries applications
grep -rn "libraries/Core/src\|libraries/Core/test/data" --include=CMakeLists.txt --include='*.cmake' --include='*.md' libraries applications cmake
grep -n "sqlite" build/libraries/Core/CMakeFiles/vlms_core.dir/flags.make
grep -rn "VLMS/Repositories\|VLMS/Database" libraries/Core
```
Expected: no output from any of the four.

- [ ] **Step 5: Smoke-test the app against the development database**

```bash
cd /home/amin/Dokumente/dev/VLMS
QT_QPA_PLATFORM=offscreen timeout 5 build/bin/vlms; echo "exit: $?"
```
Expected: `exit: 124`, meaning the app was still running when the timeout
stopped it, with nothing on stderr about the database or the schema. An
immediate exit, or an error dialog message about the database, means
`VLMS_SCHEMA_PATH` or `VLMS_PROJECT_ROOT` did not reach its library.
Check `grep -rn "VLMS_SCHEMA_PATH\|VLMS_PROJECT_ROOT" build/libraries/*/CMakeFiles/*/flags.make`.

- [ ] **Step 6: Commit**

```bash
cd /home/amin/Dokumente/dev/VLMS
git add libraries/Core/test/CMakeLists.txt
git commit -q -m "Core tests link only Core; drop the split scaffolding.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git log --oneline -5
```
