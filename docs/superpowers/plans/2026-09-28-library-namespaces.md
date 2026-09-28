# Library Namespaces Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put each library's code in its own `VLMS::<Library>` namespace
(`VLMS::Core`, `VLMS::Database`, `VLMS::Repositories`; Ocr is already
`VLMS::Ocr`), rename the `Database` class to `VLMS::Database::Connection`,
and move the test helpers from `VLMS::Test` to a top-level `Test` namespace.

**Architecture:** A pure rename, with no behaviour change. It runs top-down
through the dependency graph (tests → Repositories → Database → Core), so
every reference is edited exactly once. While Core is still flat `VLMS::`,
code already wrapped in `VLMS::Repositories` still finds `Status` through
the enclosing `VLMS`. When Core moves (Task 4), the compiler flags every
such use and it becomes `Core::Status`. The compiler is the search tool:
after each move, build, and fix each error with the spelling rules below.

**Tech Stack:** C++20, CMake (Unix Makefiles, Debug), GoogleTest via
FetchContent, Qt 6 Widgets (app only), SQLite 3.

**Spec:** `docs/superpowers/specs/2026-09-28-library-namespaces-design.md`

**Branch:** `split/namespaces` (already checked out, cut from
`split/backend-libraries`). Work directly on it, with no worktrees.

## Global Constraints

- **No behaviour change.** Allowed edits:
  - namespace blocks and their closing comments;
  - name qualification;
  - deleting using-declarations and namespace aliases that name library
    symbols;
  - forward declarations;
  - adding `using namespace VLMS;` / `using namespace Test;` to test `.cpp`
    files;
  - the `Database` → `Connection` rename in Task 3.

  Do not change function bodies beyond qualification, and do not reorder
  includes or reformat code.
- **The template.** Every library file wraps its code in exactly one block
  (nested names use the same form):

  ```cpp
  namespace VLMS::<Library> {

  ...

  }  // namespace VLMS::<Library>
  ```

  Anonymous namespaces (`namespace { … }  // namespace`) stay as they are,
  inside the library block. `#include` lines stay outside every block.
- **Spelling rules:**

  | Where the code is | Own library | Other library |
  |---|---|---|
  | Inside `namespace VLMS::<Lib>` (library code) | unqualified: `BookRecord` | `Core::Status`, `Database::SqliteSession` |
  | Inside `namespace VLMS` (app helper classes: `QtBridge.h`, `Theme`, `ListPageFrame`, `TablePager`, `FacetList`, `BookFacetFilters`, …) | — | `Core::Date`, `Repositories::BookRecord` |
  | Global scope (most app code: pages, dialogs, `Application`, `main.cpp`, `manual_capture/`; test support headers) | — | `VLMS::Core::Status`, `VLMS::Repositories::BookRecord` |
  | Test `.cpp` with `using namespace VLMS;` | — | `Core::Status`, `Repositories::BookRecord` |

  To tell whether a line is inside `namespace VLMS`, look for an enclosing
  `namespace VLMS {` block in that file. Many app headers open
  `namespace VLMS {` only to forward-declare one class and close it again;
  the class after that is global.
- **Headers never contain `using namespace` or namespace aliases**
  (`namespace X = …;`).
- **Using-declarations** that name a library symbol
  (`using VLMS::Status;`, `using VLMS::SqlText::nullableText;`) are deleted
  in the task that moves that library, and every use in the file body is
  qualified per the table.

  The app's own using-declarations stay untouched: `using VLMS::T;`,
  `using VLMS::qs;`, `ss`, `qsl`, `svl`, `qd`, `cd`, `using VLMS::Theme;`,
  `ThemeMode`, `TablePager`, `LanguageSelector`, `ClickableLabel`,
  `languageFlagIcon`, `applicationPalette` and `applicationStylesheet`.
- **Test helpers** are qualified as `::Test::X`, always with the leading
  `::`. Inside a `TEST`/`TEST_F` body, a bare `Test` resolves to gtest's
  `testing::Test`.
- **Test counts must not change:**
  - library binaries: core 31 + database 119 + repositories 424 = **574**
    cases;
  - full `ctest`: **291** entries;
  - core-only `ctest`: **7** entries.
- Every commit ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

### Build & test procedure (used by every task)

One-time setup, run once in Task 1 Step 1. Both directories are gitignored
by `/build-*/`:

```bash
cd /home/amin/Dokumente/dev/VLMS
cmake -S . -B build-ns -DCMAKE_BUILD_TYPE=Debug -DVLMS_MANUAL_CAPTURE=ON \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$PWD/build/_deps/googletest-src \
  -DVLMS_TEST_REAL_DB=$PWD/database/vlms.db.bak-20260814-232925
cmake -S . -B build-ns-core -DCMAKE_BUILD_TYPE=Debug -DBUILD_APPS=OFF \
  -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$PWD/build/_deps/googletest-src
```

`VLMS_MANUAL_CAPTURE=ON` matters because `applications/vlms/manual_capture/`
uses repository types and is off by default and in CI. It builds cleanly at
the start of this plan.

**Build**, and list the errors to fix (repeat until it prints nothing):

```bash
cmake --build build-ns -j8 2>&1 | grep -E 'error:|warning:' | sort -u | head -40
```

**Full check** (all four must hold):

```bash
cmake --build build-ns -j8 2>&1 | grep -cE 'error:|warning:'          # expect: 0
ctest --test-dir build-ns -j8 2>&1 | grep -E 'tests passed|tests failed'  # expect: 100% tests passed, 0 tests failed out of 291
s=0; for t in core database repositories; do n=$(build-ns/bin/test_vlms_$t --gtest_list_tests | grep -c '^  '); s=$((s+n)); done; echo $s   # expect: 574
cmake --build build-ns-core -j8 2>&1 | grep -cE 'error:|warning:'     # expect: 0
ctest --test-dir build-ns-core -j8 --no-tests=error -LE realdb 2>&1 | grep -E 'tests passed|tests failed'  # expect: 100% tests passed, 0 tests failed out of 7
```

---

### Task 1: Test helpers → top-level `Test`

**Files:**
- Modify (support):
  - `libraries/Core/test/support/TestEnv.h`, `TestEnv.cpp`
  - `libraries/Database/test/support/TestDatabase.h`, `TestDatabase.cpp`, `SqlValue.h`
  - `libraries/Repositories/test/support/TestSeed.h`, `TestSeed.cpp`
- Modify (tests): every `.cpp` under `libraries/*/test/src` and
  `applications/vlms/test/src` that contains `VLMS::Test`. Find them with
  `grep -rl 'VLMS::Test' libraries/*/test/src applications/vlms/test/src`
  (about 55 files).

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `::Test::TestEnv`, `::Test::TestDatabase`, `::Test::SqlValue`,
    `::Test::seed*` functions, `::Test::MemberSeed` and `::Test::unwrapResult`;
  - the macro `VLMS_UNWRAP(result)`, which expands to
    `(::Test::unwrapResult((result), __FILE__, __LINE__))`;
  - every test `.cpp` that names library code has `using namespace VLMS;`
    (Tasks 2–4 rely on it).

- [ ] **Step 1: Configure the two build directories and confirm a green baseline**

Run the one-time setup, then the **Full check**. Expected: 0 warnings, 291/291,
574, 0 warnings, 7/7. If the baseline is not green, stop and report.

- [ ] **Step 2: Record the failing guard**

```bash
grep -rn 'VLMS::Test' libraries applications | wc -l
```
Expected: about 70 (non-zero). This task makes it 0.

- [ ] **Step 3: Move the support namespaces**

In each support file, replace the namespace block:

```cpp
// before
namespace VLMS::Test {
...
}  // namespace VLMS::Test

// after
namespace Test {
...
}  // namespace Test
```

In `TestEnv.h`, change the macro:

```cpp
#define VLMS_UNWRAP(result) (::Test::unwrapResult((result), __FILE__, __LINE__))
```

The support code is now outside `VLMS`, so each library name it uses bare
(for example `Result<T>`, `Status`, `SqliteSession`, `Locale`) gets a
`VLMS::` prefix: `VLMS::Result<T>`, `VLMS::SqliteSession`. Names of the
global Repositories types (`BookRecord`, `MemberSeed` fields of those types)
and the global `Database` class stay unqualified for now. Forward
declarations such as `namespace VLMS { class SqliteSession; }` in
`TestDatabase.h` stay as they are.

- [ ] **Step 4: Update the test sources**

In every file from the list:

```cpp
// before
using namespace VLMS::Test;

// after
using namespace VLMS;
using namespace Test;
```

For files that have no `using namespace VLMS::Test;` but spell
`VLMS::Test::X` (for example `test_category_sort.cpp`,
`test_catalog_repository.cpp:762`), replace `VLMS::Test::` with `::Test::`
and add `using namespace VLMS;` after the last `#include`. Also add
`using namespace VLMS;` after the last `#include` in every other test
`.cpp` that contains `VLMS::`. Place it at global scope, never inside an
anonymous namespace.

- [ ] **Step 5: Build and fix**

Run **Build**. With `using namespace VLMS;` at global scope, an ambiguity
error ("reference to 'X' is ambiguous") means a test-local global name
collides with a `VLMS::X`. Resolve it by qualifying the *test-local* use as
`::X`. Don't rename anything.

- [ ] **Step 6: Guard and full check**

```bash
grep -rn 'VLMS::Test' libraries applications | wc -l     # expect: 0
grep -rn 'namespace VLMS::Test' libraries applications | wc -l   # expect: 0
```
Then run the **Full check**.

- [ ] **Step 7: Commit**

```bash
git add -A libraries/*/test applications/vlms/test
git commit -m "Move test helpers out of VLMS into a top-level Test namespace.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Repositories → `VLMS::Repositories`

**Files:**
- Modify (library): every file under `libraries/Repositories/include/VLMS/Repositories/`
  and `libraries/Repositories/src/`.
- Modify (consumers):
  - `libraries/Repositories/test/support/TestSeed.{h,cpp}`
  - `libraries/Repositories/test/src/*.cpp`
  - `applications/vlms/src/**` files that name a Repositories type
  - `applications/vlms/manual_capture/shots_screens.cpp` and `shots_tasks.cpp`
  - `applications/vlms/test/src/*.cpp`

  The compiler finds the full set; expect about 90 files.

**Interfaces:**
- Consumes: Task 1's `using namespace VLMS;` in test `.cpp` files.
- Produces:
  - `VLMS::Repositories::{CatalogRepository, CirculationRepository, MemberRepository, MetricsRepository}`;
  - every type from the `*Types.h` headers:
    `ArchiveScope, BookCopyInput, BookCopyRecord, BookInput, BookQuery, BookRecord, BookWrite, CategoryRecord, CopyQuery, CoverFilter, LanguageRecord, LibraryMetrics, LoanCopyOption, LoanInput, LoanMemberOption, LoanQuery, LoanRecord, MemberFacets, MemberInput, MemberQuery, MemberRecord, MemberWrite, MetricsCategoryCount, MetricsPeriodCounts`;
  - the constant groups
    `VLMS::Repositories::{BookSort, CopySort, LoanFilter, LoanSort, MemberStatus, MemberSex, MemberAgeGroup, MemberSort}`;
  - `VLMS::Repositories::LoanPolicy` (with its nested `Validation`);
  - internals `VLMS::Repositories::{BookSql, LoanSql, MemberSql, RepoSql, NamedEntityStore, CategoryStore, BookCopyStore}`.

- [ ] **Step 1: Record the failing guard**

```bash
grep -LE '^namespace VLMS::Repositories( |::)' libraries/Repositories/include/VLMS/Repositories/*.h libraries/Repositories/src/*.h libraries/Repositories/src/*.cpp
```
Expected: every file is listed. This task empties the list.

- [ ] **Step 2: Wrap the public headers**

For each header in `include/VLMS/Repositories/`, wrap everything after the
last `#include` (and after any `namespace VLMS { class SqliteSession; }`
forward-declaration block, which stays as it is until Task 3) in
`namespace VLMS::Repositories {` … `}  // namespace VLMS::Repositories`.

- Constant groups stay as nested namespaces. `namespace BookSort {` inside
  the block becomes `VLMS::Repositories::BookSort`. Keep their
  `}  // namespace BookSort` closers.
- `LoanPolicy.h`: `namespace VLMS::LoanPolicy {` → `namespace VLMS::Repositories::LoanPolicy {`,
  with the closer `}  // namespace VLMS::Repositories::LoanPolicy`.
- Global forward declarations of the Stores (`class BookCopyStore;` in
  `CatalogRepository.h`) move inside the block.

Example, `MetricsRepository.h`:

```cpp
namespace VLMS {
class SqliteSession;
}

namespace VLMS::Repositories {

class MetricsRepository {
public:
    explicit MetricsRepository(VLMS::SqliteSession& session);
    ...
};

}  // namespace VLMS::Repositories
```

Inside the block, `VLMS::Result`, `VLMS::Date` and `VLMS::SqliteSession`
still compile, so leave them. Tasks 3 and 4 rewrite them.

Example, `MetricsTypes.h`:

```cpp
#include <string>
#include <vector>

namespace VLMS::Repositories {

struct MetricsPeriodCounts {
    ...
};
...
struct LibraryMetrics {
    ...
};

}  // namespace VLMS::Repositories
```

- [ ] **Step 3: Wrap the private headers in `src/`**

- `RepoSql.h`, `BookSql.h`, `LoanSql.h`, `MemberSql.h`:
  `namespace VLMS::XSql {` → `namespace VLMS::Repositories::XSql {`, and the
  closer to `}  // namespace VLMS::Repositories::XSql`.
- Where those headers also have a `namespace VLMS { … }` block holding
  forward declarations of `SqliteSession`/`SqliteStatement`, leave that block.
- Where they declare anything global (a record type, a Store), move it
  inside `namespace VLMS::Repositories {`.
- `NamedEntityStore.h`, `CategoryStore.h` and `BookCopyStore.h` wrap their
  class the same way as in Step 2.

- [ ] **Step 4: Wrap the sources in `src/`**

For each `.cpp`, put `namespace VLMS::Repositories {` after the last
`#include` and after the file-top using-declarations. Close it at the end of
the file with `}  // namespace VLMS::Repositories`. The file's anonymous
namespace and all definitions end up inside.

- Files that already open `namespace VLMS::BookSql {` (and `LoanSql`,
  `MemberSql`, `LoanPolicy`): rename to
  `namespace VLMS::Repositories::BookSql {` and so on, and fix the closer.
- **Delete** these aliases, since the names are now reachable unqualified
  from inside `VLMS::Repositories`:

  ```cpp
  namespace BookSql = VLMS::BookSql;
  namespace LoanSql = VLMS::LoanSql;
  namespace MemberSql = VLMS::MemberSql;
  namespace RepoSql = VLMS::RepoSql;
  namespace LoanPolicy = VLMS::LoanPolicy;
  ```
- Delete any `using VLMS::LoanPolicy::…;` / `using VLMS::BookSql::…;`
  declarations (own library), and qualify the uses in the body relative to
  the library (`LoanPolicy::X`, `BookSql::X`).
- Keep the using-declarations of Core and Database names (`using VLMS::Status;`,
  `using VLMS::SqliteStatement;`); Tasks 3 and 4 remove them.

- [ ] **Step 5: Build the library and fix**

```bash
cmake --build build-ns-core --target vlms_repositories -j8 2>&1 | grep -E 'error:|warning:' | sort -u | head -40
```
Fix until it's empty. Typical cause: a definition left outside the block.

- [ ] **Step 6: Update consumers**

Run **Build** and fix every error with the spelling rules:

- Global app code (pages, dialogs, `Application.*`, `main.cpp`,
  `manual_capture/*`): `BookRecord` → `VLMS::Repositories::BookRecord`,
  `BookSort::kTitle` → `VLMS::Repositories::BookSort::kTitle`,
  `CatalogRepository` → `VLMS::Repositories::CatalogRepository`.
- App helper code inside `namespace VLMS` (for example `BookFacetFilters`,
  `MemberFacetFilters`, `ReuseNumberFlow`, and the helper parts of
  `CatalogPage.h`/`MembersPage.h`/`ArchivePage.h`/`CirculationPage.h`):
  `Repositories::BookRecord`.
- `LoanCheckoutDialog.cpp` and `LoanExtendDialog.cpp`: delete
  `namespace LoanPolicy = VLMS::LoanPolicy;` and qualify its uses as
  `VLMS::Repositories::LoanPolicy::…`.
- Forward declarations. In `Application.h`, replace lines 8–11:

  ```cpp
  class Database;

  namespace VLMS::Repositories {
  class CatalogRepository;
  class MemberRepository;
  class CirculationRepository;
  class MetricsRepository;
  }  // namespace VLMS::Repositories
  ```

  Qualify the uses in the class body
  (`VLMS::Repositories::CatalogRepository& catalog() const`, the four
  `m_*Repository` members). Apply the same treatment to
  `class CatalogRepository;` in `ui/archive/ReuseNumberFlow.h` and
  `ui/catalog/BookCopiesTable.h`, putting the forward declaration in the
  right namespace.
- `TestSeed.h`/`.cpp` (global scope, header): `VLMS::Repositories::BookRecord`.
- Test `.cpp` (with `using namespace VLMS;`): `Repositories::BookRecord`,
  `Repositories::CatalogRepository`, `Repositories::LoanPolicy::…`.

- [ ] **Step 7: Guards and full check**

```bash
# every Repositories file is wrapped
grep -LE '^namespace VLMS::Repositories( |::)' libraries/Repositories/include/VLMS/Repositories/*.h libraries/Repositories/src/*.h libraries/Repositories/src/*.cpp   # expect: empty
# no aliases left for Repositories internals
grep -rnE 'namespace (BookSql|LoanSql|MemberSql|RepoSql|LoanPolicy) = ' libraries applications   # expect: empty
```
Then run the **Full check**.

- [ ] **Step 8: Commit**

```bash
git add -A libraries applications
git commit -m "Put Repositories in VLMS::Repositories.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Database → `VLMS::Database`, `Database` class → `Connection`

**Files:**
- Rename:
  - `libraries/Database/include/VLMS/Database/Database.h` → `Connection.h`
  - `libraries/Database/src/Database.cpp` → `Connection.cpp`
- Modify:
  - `libraries/Database/CMakeLists.txt` (lines 7 and 15)
  - `libraries/Database/include/VLMS/Database/SqliteSession.h`, `SqlText.h`
  - `libraries/Database/src/SqliteSession.cpp`, `SqlText.cpp`
- Modify (consumers):
  - `libraries/Database/test/support/TestDatabase.{h,cpp}`
  - `libraries/Database/test/src/*.cpp`
  - the Repositories headers and sources that use `SqliteSession`,
    `SqliteStatement` or `SqlText`: `include/VLMS/Repositories/*Repository.h`
    and, in `src/`, `*Store.h/.cpp`, `*Sql.h/.cpp`, `*Repository.cpp`
  - `libraries/Repositories/test/support/TestSeed.cpp`
  - `applications/vlms/src/Application.{h,cpp}`
  - any app or test file the compiler flags

**Interfaces:**
- Consumes: Task 2's `VLMS::Repositories` blocks.
- Produces:
  - `VLMS::Database::Connection` in `<VLMS/Database/Connection.h>`.
    Its members are identical to today's `Database`, including
    `static constexpr int kSchemaVersion`, `explicit Connection(std::string dataDirectory)`
    and `bool open()`.
  - `VLMS::Database::SqliteSession` and `VLMS::Database::SqliteStatement`.
  - `VLMS::Database::SqlText::nullableText` and the rest of `SqlText`.

- [ ] **Step 1: Record the failing guard**

```bash
grep -rnE '\bclass Database\b|VLMS/Database/Database\.h|namespace VLMS::SqlText|VLMS::Sqlite(Session|Statement)' libraries applications | wc -l
```
Expected: non-zero. This task makes it 0.

- [ ] **Step 2: Rename the files**

```bash
git mv libraries/Database/include/VLMS/Database/Database.h libraries/Database/include/VLMS/Database/Connection.h
git mv libraries/Database/src/Database.cpp libraries/Database/src/Connection.cpp
```

In `libraries/Database/CMakeLists.txt`:
- `include/VLMS/Database/Database.h` → `include/VLMS/Database/Connection.h`
- `src/Database.cpp` → `src/Connection.cpp`

Everywhere, replace `#include <VLMS/Database/Database.h>` with
`#include <VLMS/Database/Connection.h>` in place:

```bash
grep -rl 'VLMS/Database/Database.h' libraries applications | xargs sed -i 's#VLMS/Database/Database\.h#VLMS/Database/Connection.h#'
```

- [ ] **Step 3: Rename the class and wrap Database**

In `Connection.h`, replace the forward-declaration block and the global class:

```cpp
namespace VLMS::Database {

class SqliteSession;

class Connection final {
public:
    static constexpr int kSchemaVersion = 7;

    explicit Connection(std::string dataDirectory);
    ~Connection();                                   // if declared today
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ...                                              // every other member unchanged
};

}  // namespace VLMS::Database
```

- Rename only the class's own name: constructor, destructor, copy/move
  members and `Database::` definition prefixes. Leave member functions,
  comments and strings alone, except comments that name the class itself
  (`Database::open()` → `Connection::open()`).
- In `Connection.cpp`:
  - delete `using VLMS::SqliteSession;` and `using VLMS::SqliteStatement;`
    (they're own-library now);
  - keep `using VLMS::Status;` (Core, handled in Task 4);
  - wrap from after the using-declarations to end of file in
    `namespace VLMS::Database {` … `}  // namespace VLMS::Database`;
  - rename `Database::X(` definitions to `Connection::X(`.
- `SqliteSession.h`/`.cpp`: `namespace VLMS {` → `namespace VLMS::Database {`,
  with the closer `}  // namespace VLMS::Database`.
- `SqlText.h`/`.cpp`: `namespace VLMS::SqlText {` → `namespace VLMS::Database::SqlText {`,
  and fix the closer.

- [ ] **Step 4: Update consumers**

Run **Build** and fix per the spelling rules:

- Repositories headers:
  - Replace
    ```cpp
    namespace VLMS {
    class SqliteSession;
    }
    ```
    with
    ```cpp
    namespace VLMS::Database {
    class SqliteSession;
    }  // namespace VLMS::Database
    ```
    (add `class SqliteStatement;` where the old block had it).
  - Inside `VLMS::Repositories`, `VLMS::SqliteSession` → `Database::SqliteSession`.
- Repositories sources:
  - Delete `using VLMS::SqliteStatement;`, `using VLMS::SqliteSession;` and
    `using VLMS::SqlText::nullableText;` (and the other `SqlText` names).
  - Qualify the uses in the bodies as `Database::SqliteStatement`,
    `Database::SqlText::nullableText(…)`.
- `Application.h`: `class Database;` → `namespace VLMS::Database { class Connection; }`,
  and the member and accessor become `VLMS::Database::Connection* m_database`
  and `VLMS::Database::Connection& database() const`. In `Application.cpp`,
  write `new VLMS::Database::Connection(…)`.
- `TestDatabase.h`/`.cpp` (global, header): `VLMS::Database::Connection`,
  `VLMS::Database::SqliteSession`. Replace the forward-declaration block the
  same way as in the Repositories headers.
- Test `.cpp`: `Database db(dir);` → `Database::Connection db(dir);`,
  `Database::kSchemaVersion` → `Database::Connection::kSchemaVersion`,
  `VLMS::SqliteSession` → `Database::SqliteSession`.

- [ ] **Step 5: Guards and full check**

```bash
grep -rnE '\bclass Database\b|VLMS/Database/Database\.h|namespace VLMS::SqlText|VLMS::Sqlite(Session|Statement)|using VLMS::(Sqlite|SqlText)' libraries applications   # expect: empty
grep -LE '^namespace VLMS::Database( |::)' libraries/Database/include/VLMS/Database/*.h libraries/Database/src/*.cpp   # expect: empty
```
Then run the **Full check**.

- [ ] **Step 6: Commit**

```bash
git add -A libraries applications
git commit -m "Put Database in VLMS::Database and rename the Database class to Connection.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Core → `VLMS::Core`, then final verification

**Files:**
- Modify (library): every header in `libraries/Core/include/VLMS/Core/` and
  every source in `libraries/Core/src/`.
- Modify (consumers):
  - Database and Repositories headers and sources
  - `libraries/*/test/support/*`
  - `libraries/*/test/src/*.cpp`
  - `applications/vlms/src/**` (including `QtBridge.h`)
  - `applications/vlms/manual_capture/*`
  - `applications/vlms/test/src/*`

**Interfaces:**
- Consumes: Tasks 2 and 3 (library blocks), Task 1 (`using namespace VLMS;` in tests).
- Produces:
  - `VLMS::Core::{Clock, ScopedClock, Date, DateTime, Locale, Paths, Result, Status, Error, ErrorKind, Strings}`;
  - the free functions of `Text.h` (`VLMS::Core::trim`, …);
  - `VLMS::Core::DateText`.

- [ ] **Step 1: Record the failing guard**

```bash
grep -rlE '^namespace VLMS \{' libraries | grep -v '/test/'
```
Expected: the Core headers and sources. After this task, nothing.

- [ ] **Step 2: Wrap Core**

In every Core header and source:
- `namespace VLMS {` → `namespace VLMS::Core {`, with the closer
  `}  // namespace VLMS::Core`;
- `namespace VLMS::DateText {` → `namespace VLMS::Core::DateText {`, with
  the closer `}  // namespace VLMS::Core::DateText`.

Within Core, uses of Core names stay unqualified. Any `VLMS::X` spelling
inside Core that names a Core symbol becomes plain `X`.

- [ ] **Step 3: Update consumers**

Run **Build** and fix per the spelling rules. The common rewrites:

| Before | Inside `VLMS::<Lib>` / `namespace VLMS` / test `.cpp` | Global scope and test support headers |
|---|---|---|
| `Status`, `VLMS::Status` | `Core::Status` | `VLMS::Core::Status` |
| `Result<T>`, `VLMS::Result<T>` | `Core::Result<T>` | `VLMS::Core::Result<T>` |
| `ErrorKind::Sql` | `Core::ErrorKind::Sql` | `VLMS::Core::ErrorKind::Sql` |
| `Clock::today()` | `Core::Clock::today()` | `VLMS::Core::Clock::today()` |
| `DateText::normalizePublicationDate` | `Core::DateText::normalizePublicationDate` | `VLMS::Core::DateText::…` |
| `trim(s)` | `Core::trim(s)` | `VLMS::Core::trim(s)` |
| `Strings::t(…)` | `Core::Strings::t(…)` | `VLMS::Core::Strings::t(…)` |

- Delete every using-declaration that names a Core symbol:
  `using VLMS::Status;`, `Result`, `ErrorKind`, `Clock`, `ScopedClock`,
  `Date`, `DateTime`, `Locale`, `Strings`, `trim`, `DateText`, and
  `using VLMS::DateText::…;`. Then qualify the uses in the body.
- Don't touch the app's own using-declarations (`T`, `qs`, `ss`, `qsl`,
  `svl`, `qd`, `cd`, `Theme`, `ThemeMode`, `TablePager`, `LanguageSelector`,
  `ClickableLabel`, `languageFlagIcon`, `applicationPalette`,
  `applicationStylesheet`).
- `QtBridge.h` is inside `namespace VLMS`: `Date` → `Core::Date`,
  `Strings::t` → `Core::Strings::t`.
- `test/support/TestEnv.h`/`.cpp` and the other support files (global):
  `VLMS::Result` → `VLMS::Core::Result`, and so on.
- `VLMS_UNWRAP` needs no change.

- [ ] **Step 4: Final guards (spec "Verification")**

```bash
# 1. nothing under libraries/ opens flat VLMS (forward-declaration blocks included:
#    they now name VLMS::<Lib>). App code is out of scope and keeps its blocks.
grep -rnE '^namespace VLMS \{' libraries                             # expect: empty
# 2. no using-directive or alias in any header
grep -rnE 'using namespace|^\s*namespace [A-Za-z]+ = ' --include=*.h libraries applications   # expect: empty
# 3. no global Repositories types: every public and private Repositories file is wrapped
grep -LE '^namespace VLMS::Repositories( |::)' libraries/Repositories/include/VLMS/Repositories/*.h libraries/Repositories/src/*.h libraries/Repositories/src/*.cpp   # expect: empty
# 4. no VLMS::Test left
grep -rn 'VLMS::Test' libraries applications                          # expect: empty
# 5. no library using-declarations left
grep -rnE '^using VLMS::(Status|Result|ErrorKind|Error|Clock|ScopedClock|Date|DateTime|Locale|Paths|Strings|trim|DateText|SqlText|Sqlite|LoanPolicy|BookSql|LoanSql|MemberSql|RepoSql)\b' libraries applications   # expect: empty
```

- [ ] **Step 5: Full check, release plan, and app start**

Run the **Full check**. Then:

```bash
python3 scripts/release/release.py plan 2>&1 | sed -n '1,2p'        # expect: "Next release: major -> 2.0.0"
python3 -m unittest discover -s scripts/release 2>&1 | tail -1      # expect: OK
QT_QPA_PLATFORM=offscreen timeout 5 build-ns/bin/vlms; echo "exit $?"   # expect: exit 124 (still running), no stderr
```

- [ ] **Step 6: Commit**

```bash
git add -A libraries applications
git commit -m "Put Core in VLMS::Core.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Clean up the build directories**

```bash
rm -rf build-ns build-ns-core
```
