# Library namespaces: one `VLMS::<Library>` per library

Approved 2026-09-28. Second step of the Core refactor, following
[the backend library split](2026-09-28-backend-library-split-design.md).
It renames namespaces (and one class) and changes nothing else: no file
moves apart from renaming `Database.h`/`.cpp`, no API shape changes, no behaviour
changes.

## Why

After the split, each library has its own include folder, but the code does
not say which library a name comes from:

- Core and Database declare everything in flat `VLMS::`.
- Repositories declares its classes in `VLMS::`, but its record and query
  types (`BookRecord`, `LoanQuery`, `ArchiveScope`, …) and its constant
  groups (`BookSort`, `MemberStatus`, …) are global.
- Only Ocr has its own namespace, `VLMS::Ocr`.

After this change every library follows one template: the namespace mirrors
the include folder. `<VLMS/Core/Clock.h>` declares `VLMS::Core::Clock`,
`<VLMS/Repositories/CatalogRepository.h>` declares
`VLMS::Repositories::CatalogRepository`. Two libraries can no longer collide
on a name, and every cross-library reference shows its origin.

## The template

Every declaration and definition in a library is wrapped as:

```cpp
namespace VLMS::<Library> {

...

}  // namespace VLMS::<Library>
```

Nested namespaces use the same form (`namespace VLMS::Core::DateText {`).
Anonymous namespaces for file-local helpers stay as they are, inside the
library namespace.

## Namespace map

| Code | Today | After |
|---|---|---|
| Core public: `Clock`, `ScopedClock`, `Date`, `DateTime`, `Locale`, `Paths`, `Result`, `Status`, `Error`, `ErrorKind`, `Strings`, the free functions in `Text.h` | `VLMS::` | `VLMS::Core::` |
| `DateText` | `VLMS::DateText` | `VLMS::Core::DateText` |
| `Database` class | `VLMS::Database` in `<VLMS/Database/Database.h>` | `VLMS::Database::Connection` in `<VLMS/Database/Connection.h>` |
| `SqliteSession`, `SqliteStatement` | `VLMS::` | `VLMS::Database::` |
| `SqlText` | `VLMS::SqlText` | `VLMS::Database::SqlText` |
| `CatalogRepository`, `CirculationRepository`, `MemberRepository`, `MetricsRepository` | `VLMS::` | `VLMS::Repositories::` |
| Record and query types in `*Types.h` | global | `VLMS::Repositories::` |
| Constant groups (`BookSort`, `CopySort`, `LoanFilter`, `LoanSort`, `MemberStatus`, `MemberSex`, `MemberAgeGroup`, `MemberSort`) | global | `VLMS::Repositories::<Group>` |
| `LoanPolicy` | `VLMS::LoanPolicy` | `VLMS::Repositories::LoanPolicy` |
| Internals in `Repositories/src` (`BookSql`, `LoanSql`, `MemberSql`, `RepoSql`, `NamedEntityStore`, `CategoryStore`, `BookCopyStore`) | `VLMS::` | `VLMS::Repositories::`, names unchanged |
| Ocr (`VLMS::Ocr`, `VLMS::Ocr::Detail`) | | unchanged |
| App UI classes (`Theme`, `ListPageFrame`, dialogs, …) | `VLMS::` | unchanged |
| Test helpers (`TestEnv`, `TestDatabase`, `TestSeed`, `SqlValue`) | `VLMS::Test` | `Test::`, outside `VLMS` |

### The `Database` → `Connection` rename

`VLMS::Database::Database` would stutter and make `Database` ambiguous
between the class and the namespace. The class opens the database file,
applies the schema and runs the migrations, so it becomes
`VLMS::Database::Connection`. The header is renamed with `git mv` to
`<VLMS/Database/Connection.h>` and `Database.cpp` to `Connection.cpp`.
Members and behaviour are unchanged.

### Test helpers leave `VLMS`

Test helpers are building blocks of the `test_*` executables, not library
source, so they do not live in the library namespaces. They move from
`VLMS::Test` to a top-level `Test` namespace. Their files stay where they are
(`libraries/<Library>/test/support`).

## How code spells names

- **Own library:** unqualified. Code inside `VLMS::Repositories` writes
  `BookRecord`, `BookSql::…`.
- **Other libraries:** qualified by library name, without aliases:
  `Core::Status`, `Core::Clock::today()`, `Database::SqliteSession`,
  `Repositories::BookRecord`. This works unchanged from inside any
  `VLMS::…` namespace, including the app, because the app's code sits in
  `namespace VLMS`.
- **Outside any `VLMS` namespace** (for example `main.cpp`, file-scope code):
  `VLMS::Core::…`.
- **Tests:** each test `.cpp` may add `using namespace VLMS;` and then spells
  names as above. `using namespace VLMS::Test;` becomes
  `using namespace Test;`.
- **Headers** never contain `using namespace` or namespace aliases.
- **Aliases that become unnecessary are removed:**
  `namespace RepoSql = VLMS::RepoSql;` and similar in the Repositories
  sources, `namespace LoanPolicy = VLMS::LoanPolicy;` in `LoanCheckoutDialog.cpp`
  and `LoanExtendDialog.cpp`, and `namespace Ocr = VLMS::Ocr;` in
  `test_book_editor_ocr.cpp`.
- **Forward declarations** move into the namespace that owns the type, for
  example `namespace VLMS::Database { class SqliteSession; }` in the
  Repositories headers and
  `namespace VLMS::Database { class Connection; }` plus
  `namespace VLMS::Repositories { class CatalogRepository; … }` in
  `Application.h`.

## Delivery

- Branch `split/namespaces`, cut from `split/backend-libraries`. Both land on
  `main` together as one major release (v2.0.0). The merge of the split stays
  on hold until then.
- One commit per step, each building with 0 warnings and passing all tests on
  its own:
  1. Core → `VLMS::Core`.
  2. Database → `VLMS::Database`, with the `Connection` rename.
  3. Repositories → `VLMS::Repositories`, including the global types.
  4. Test helpers → `Test::`.
- Consumers (other libraries, the app, all tests) are updated in the same
  commit as the library they reference.

## Verification

For each step:

- Full Debug build: exit 0, 0 warnings; `ctest` all green (283 UI entries plus
  the library binaries; 574 library test cases in total).
- Core-only build (`-DBUILD_APPS=OFF`), as CI runs it.
- After the last step, greps that return nothing:
  - library code declaring anything in flat `VLMS` (a `namespace VLMS {`
    block under `libraries/`);
  - `using namespace` or a namespace alias in any header;
  - a declaration of a `*Types.h` type outside `VLMS::Repositories`;
  - `VLMS::Test` anywhere.
- `python3 scripts/release/release.py plan` still reports a single
  `major -> 2.0.0`.

## Out of scope

- Renaming the `test_core_*` suites in the Database and Repositories test
  binaries.
- Per-library test-data environment variables in place of the global
  `VLMS_TEST_DATA_DIR`.
- `release.py` not detecting `add_library(${RAW_TARGET_NAME} …)` as a new
  library.
- Moving the app's UI classes out of flat `VLMS`.
