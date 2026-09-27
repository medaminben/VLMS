# Clickable Table Column Sort Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Clicking a table column caption sorts that list by the column (first click ascending, next click descending, other column starts ascending), including every table in the app.

**Architecture:** Paginated list pages (Catalog, Members, Circulation) send a whitelisted `sortColumn` + `sortAscending` on the existing query structs; repositories append `orderClause()` and expose `rankOf(id, query)` so a selected row stays selected and the pager follows it. Dialogs, Metrics, and book copies already hold every row and sort in the widget through a shared `TableHeaderSort` helper.

**Tech Stack:** C++17, Qt Widgets (`QTableWidget` / `QHeaderView`), Qt-free Core (`SqliteSession`, `Result<T>`), GoogleTest.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-09-06-table-column-sort-design.md`
- Core stays Qt-free (`std::string`, `int64_t`, `Result<T>`). Never concatenate header captions into SQL.
- Sort keys are stable English identifiers (`number`, `name`, …), not `Strings::t` captions.
- Empty or unknown `sortColumn` → today’s default `ORDER BY`. Hostile values are unknown.
- Every `ORDER BY` ends with `id` so paging and `rankOf` are a total order.
- Membership numbers: `CAST(membership_number AS INTEGER)` then the full string (`2` before `10`, `123b` beside `123`).
- Session-only sort. Search and filters stay as they are.
- No selection on a list page → page 1. A selected row → page of that id after the new order; keep it selected.
- Tests: GoogleTest. Core binary `test_vlms_core`. UI binary `test_vlms_ui`.
- Do not commit unrelated uncommitted work (pager, member filters, `.vscode/`, architecture UML).

---

## File map

| File | Role |
|---|---|
| `libraries/Core/include/VLMS/Core/MemberTypes.h` | `MemberSort` keys; `MemberQuery.sortColumn` / `sortAscending` |
| `libraries/Core/include/VLMS/Core/CatalogTypes.h` | `BookSort` keys; `BookQuery` sort fields |
| `libraries/Core/include/VLMS/Core/LoanTypes.h` | `LoanSort` keys; `LoanQuery` sort fields |
| `libraries/Core/src/MemberSql.h` / `.cpp` | `orderExpressions` / `orderClause` |
| `libraries/Core/src/BookSql.h` / `.cpp` | same |
| `libraries/Core/src/LoanSql.h` / `.cpp` | same |
| `libraries/Core/include/VLMS/Core/MemberRepository.h` + `.cpp` | `listMembers` uses `orderClause`; `rankOfMember` |
| `libraries/Core/include/VLMS/Core/CatalogRepository.h` + `.cpp` | `listBooks` + `rankOfBook` |
| `libraries/Core/include/VLMS/Core/CirculationRepository.h` + `.cpp` | `listLoans` + `rankOfLoan` |
| `applications/vlms/src/ui/TableHeaderSort.h` / `.cpp` | header click, toggle, arrow; optional in-widget sort |
| `applications/vlms/src/ui/TablePager.h` / `.cpp` | `setCurrentPage(int)` (no signal, like `resetToFirstPage`) |
| `applications/vlms/CMakeLists.txt` | add `TableHeaderSort.cpp` / `.h` to `vlms_ui` |
| List pages + dialogs + Metrics + `BookCopiesTable` | wire helper; list pages pass sort into the query |
| `libraries/Core/test/src/test_member_repository.cpp` (etc.) | sort + rank tests |
| `libraries/Core/test/src/test_member_injection.cpp` (etc.) | hostile `sortColumn` |
| `applications/vlms/test/src/test_member_sort.cpp` | header click, page 1, follow row |
| `applications/vlms/test/src/test_table_header_sort.cpp` | helper toggle |
| `applications/vlms/test/CMakeLists.txt` | register the new UI tests |

**Build / run** (from repo root; `build/` already exists locally):

```bash
cmake --build build --target test_vlms_core test_vlms_ui --parallel
./build/libraries/Core/test/test_vlms_core --gtest_filter='Name'
QT_QPA_PLATFORM=offscreen ./build/applications/vlms/test/test_vlms_ui --gtest_filter='Name'
```

---

### Task 1: Member SQL sort

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/MemberTypes.h`
- Modify: `libraries/Core/src/MemberSql.h`
- Modify: `libraries/Core/src/MemberSql.cpp`
- Modify: `libraries/Core/src/MemberRepository.cpp` (`listMembers` `ORDER BY` block)
- Test: `libraries/Core/test/src/test_member_repository.cpp`
- Test: `libraries/Core/test/src/test_member_injection.cpp`

**Interfaces:**
- Consumes: existing `MemberQuery`, `MemberSql::filterClause` / `bindFilters`, `listMembers`
- Produces: `namespace MemberSort` string keys; `MemberQuery::sortColumn` (`std::string`, default empty) and `sortAscending` (`bool`, default `true`); `VLMS::MemberSql::orderExpressions(const MemberQuery&)` → SQL fragment without `ORDER BY`; `orderClause(const MemberQuery&)` → `" ORDER BY " + expressions`

- [ ] **Step 1: Write the failing tests**

Append to `test_member_repository.cpp` (same fixture `test_core_MemberRepository`):

```cpp
TEST_F(test_core_MemberRepository, ListMembersSortsByNumberNumerically)
{
    MemberSeed ten = uniqueMemberSeed(401);
    ten.membershipNumber = "10";
    ten.lastName = "Aaa";
    const std::int64_t id10 = seedMember(*m_db, ten);
    MemberSeed two = uniqueMemberSeed(402);
    two.membershipNumber = "2";
    two.lastName = "Zzz";
    const std::int64_t id2 = seedMember(*m_db, two);
    MemberSeed twin = uniqueMemberSeed(403);
    twin.membershipNumber = "2b";
    twin.lastName = "Mmm";
    const std::int64_t id2b = seedMember(*m_db, twin);
    ASSERT_GT(id10, 0);
    ASSERT_GT(id2, 0);
    ASSERT_GT(id2b, 0);

    MemberQuery query;
    query.sortColumn = MemberSort::kNumber;
    query.sortAscending = true;
    const auto asc = VLMS_UNWRAP(m_repository->listMembers(query));
    ASSERT_EQ(asc.size(), 3u);
    EXPECT_EQ(asc.at(0).id, id2);
    EXPECT_EQ(asc.at(1).id, id2b);
    EXPECT_EQ(asc.at(2).id, id10);

    query.sortAscending = false;
    const auto desc = VLMS_UNWRAP(m_repository->listMembers(query));
    ASSERT_EQ(desc.size(), 3u);
    EXPECT_EQ(desc.at(0).id, id10);
    EXPECT_EQ(desc.at(1).id, id2b);
    EXPECT_EQ(desc.at(2).id, id2);
}

TEST_F(test_core_MemberRepository, ListMembersSortsByNameCaseInsensitive)
{
    MemberSeed zeta = uniqueMemberSeed(411);
    zeta.lastName = "zeta";
    zeta.firstName = "A";
    const std::int64_t idZ = seedMember(*m_db, zeta);
    MemberSeed alpha = uniqueMemberSeed(412);
    alpha.lastName = "Alpha";
    alpha.firstName = "B";
    const std::int64_t idA = seedMember(*m_db, alpha);
    ASSERT_GT(idZ, 0);
    ASSERT_GT(idA, 0);

    MemberQuery query;
    query.sortColumn = MemberSort::kName;
    query.sortAscending = true;
    const auto rows = VLMS_UNWRAP(m_repository->listMembers(query));
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows.at(0).id, idA);
    EXPECT_EQ(rows.at(1).id, idZ);

    query.sortColumn.clear();
    const auto byDefault = VLMS_UNWRAP(m_repository->listMembers(query));
    ASSERT_EQ(byDefault.size(), 2u);
    EXPECT_EQ(byDefault.at(0).id, idA);
    EXPECT_EQ(byDefault.at(1).id, idZ);
}
```

Append to `test_member_injection.cpp`:

```cpp
TEST_F(test_core_MemberInjection, HostileSortColumnUsesDefaultOrder)
{
    MemberQuery safe;
    const auto expected = VLMS_UNWRAP(m_repository->listMembers(safe));
    ASSERT_FALSE(expected.empty());

    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);
        MemberQuery query;
        query.sortColumn = entry.value;
        const auto rows = VLMS_UNWRAP(m_repository->listMembers(query));
        ASSERT_EQ(rows.size(), expected.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            EXPECT_EQ(rows.at(i).id, expected.at(i).id);
        }
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
cmake --build build --target test_vlms_core --parallel
./build/libraries/Core/test/test_vlms_core --gtest_filter='test_core_MemberRepository.ListMembersSorts*:test_core_MemberInjection.HostileSortColumnUsesDefaultOrder'
```

Expected: FAIL to compile (`MemberSort` / `sortColumn` unknown) or FAIL assertions (order still last-name default, so `10` before `2`).

- [ ] **Step 3: Write minimal implementation**

In `MemberTypes.h`, after `MemberAgeGroup`:

```cpp
namespace MemberSort {
inline constexpr auto kNumber = "number";
inline constexpr auto kName = "name";
inline constexpr auto kPhone = "phone";
inline constexpr auto kCity = "city";
inline constexpr auto kStatus = "status";
inline constexpr auto kLoans = "loans";
}  // namespace MemberSort
```

In `MemberQuery`:

```cpp
    std::string sortColumn;
    bool sortAscending = true;
```

In `MemberSql.h`:

```cpp
[[nodiscard]] std::string orderExpressions(const MemberQuery& query);
[[nodiscard]] std::string orderClause(const MemberQuery& query);
```

In `MemberSql.cpp`, add:

```cpp
namespace {

const char* direction(const bool ascending)
{
    return ascending ? " ASC" : " DESC";
}

std::string withDirection(const std::string& expression, const bool ascending)
{
    return expression + direction(ascending);
}

}  // namespace

std::string orderExpressions(const MemberQuery& query)
{
    const bool asc = query.sortAscending;
    const std::string& column = query.sortColumn;
    if (column == MemberSort::kNumber) {
        return withDirection("CAST(m.membership_number AS INTEGER)", asc) + ", "
            + withDirection("m.membership_number COLLATE NOCASE", asc) + ", "
            + withDirection("m.id", asc);
    }
    if (column == MemberSort::kName) {
        return withDirection("m.last_name COLLATE NOCASE", asc) + ", "
            + withDirection("m.first_name COLLATE NOCASE", asc) + ", "
            + withDirection("m.id", asc);
    }
    if (column == MemberSort::kPhone) {
        return withDirection("m.phone COLLATE NOCASE", asc) + ", " + withDirection("m.id", asc);
    }
    if (column == MemberSort::kCity) {
        return withDirection("m.city COLLATE NOCASE", asc) + ", " + withDirection("m.id", asc);
    }
    if (column == MemberSort::kStatus) {
        return withDirection("m.status COLLATE NOCASE", asc) + ", " + withDirection("m.id", asc);
    }
    if (column == MemberSort::kLoans) {
        return withDirection("("
                             "SELECT COUNT(*) FROM loans l "
                             "WHERE l.member_id = m.id AND l.returned_at IS NULL"
                             ")",
                             asc)
            + ", " + withDirection("m.id", asc);
    }
    return "m.last_name COLLATE NOCASE ASC, m.first_name COLLATE NOCASE ASC, m.id ASC";
}

std::string orderClause(const MemberQuery& query)
{
    return " ORDER BY " + orderExpressions(query);
}
```

In `MemberRepository.cpp` `listMembers`, replace the hardcoded `ORDER BY` with:

```cpp
    sql += MemberSql::orderClause(query);
    sql += R"SQL(
        LIMIT :limit OFFSET :offset
    )SQL";
```

- [ ] **Step 4: Run tests to verify they pass**

Run the same filter as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libraries/Core/include/VLMS/Core/MemberTypes.h \
        libraries/Core/src/MemberSql.h libraries/Core/src/MemberSql.cpp \
        libraries/Core/src/MemberRepository.cpp \
        libraries/Core/test/src/test_member_repository.cpp \
        libraries/Core/test/src/test_member_injection.cpp
git commit -m "$(cat <<'EOF'
Sort member lists from a whitelisted query column.

EOF
)"
```

---

### Task 2: Member rankOf

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/MemberRepository.h`
- Modify: `libraries/Core/src/MemberRepository.cpp`
- Test: `libraries/Core/test/src/test_member_repository.cpp`

**Interfaces:**
- Consumes: `MemberSql::orderExpressions`, `MemberSql::filterClause` / `bindFilters`
- Produces: `Result<int> MemberRepository::rankOfMember(std::int64_t id, const MemberQuery& query) const` — 0-based index in the filtered, sorted full list. Ignores `query.limit` and `query.offset`. Missing id → `ErrorKind::NotFound` / `"error.member.notFound"`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_F(test_core_MemberRepository, RankOfMemberFollowsNumberSort)
{
    MemberSeed first = uniqueMemberSeed(421);
    first.membershipNumber = "1";
    first.lastName = "Zed";
    const std::int64_t id1 = seedMember(*m_db, first);
    MemberSeed second = uniqueMemberSeed(422);
    second.membershipNumber = "2";
    second.lastName = "Amy";
    const std::int64_t id2 = seedMember(*m_db, second);
    MemberSeed third = uniqueMemberSeed(423);
    third.membershipNumber = "3";
    third.lastName = "Mia";
    const std::int64_t id3 = seedMember(*m_db, third);
    ASSERT_GT(id1, 0);
    ASSERT_GT(id2, 0);
    ASSERT_GT(id3, 0);

    MemberQuery query;
    query.sortColumn = MemberSort::kNumber;
    query.sortAscending = true;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfMember(id1, query)), 0);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfMember(id3, query)), 2);

    query.sortAscending = false;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfMember(id1, query)), 2);

    const auto missing = m_repository->rankOfMember(999999, query);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.kind(), VLMS::ErrorKind::NotFound);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target test_vlms_core --parallel
./build/libraries/Core/test/test_vlms_core --gtest_filter='test_core_MemberRepository.RankOfMemberFollowsNumberSort'
```

Expected: compile fail (`rankOfMember` missing).

- [ ] **Step 3: Write minimal implementation**

Declare on `MemberRepository`:

```cpp
    [[nodiscard]] VLMS::Result<int> rankOfMember(std::int64_t id,
                                                       const MemberQuery& query) const;
```

Implement (after `listMembers`):

```cpp
Result<int> MemberRepository::rankOfMember(const std::int64_t id, const MemberQuery& query) const
{
    std::string sql =
        "SELECT ranked.rank FROM (\n"
        "    SELECT m.id, (ROW_NUMBER() OVER (ORDER BY "
        + MemberSql::orderExpressions(query) + ")) - 1 AS rank\n"
        "    FROM members m\n"
        "    WHERE m.archived_at IS NULL\n"
        + MemberSql::filterClause(query)
        + ") ranked WHERE ranked.id = :id\n";

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<int>(q.error().detail);
    }
    MemberSql::bindFilters(*q, query);
    if (!q->bind(":id", id)) {
        return RepoSql::sqlResult<int>(m_session.lastError());
    }
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlResult<int>(m_session.lastError());
        }
        return RepoSql::notFoundResult<int>("error.member.notFound");
    }
    return Result<int>::ok(q->integer(0));
}
```

- [ ] **Step 4: Run test to verify it passes**

Same filter as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add libraries/Core/include/VLMS/Core/MemberRepository.h \
        libraries/Core/src/MemberRepository.cpp \
        libraries/Core/test/src/test_member_repository.cpp
git commit -m "$(cat <<'EOF'
Return a member's rank under the current list sort.

EOF
)"
```

---

### Task 3: Book SQL sort and rank

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/CatalogTypes.h`
- Modify: `libraries/Core/include/VLMS/Core/CatalogRepository.h`
- Modify: `libraries/Core/src/BookSql.h`
- Modify: `libraries/Core/src/BookSql.cpp`
- Modify: `libraries/Core/src/CatalogRepository.cpp` (`listBooks` `ORDER BY`; add `rankOfBook`)
- Test: `libraries/Core/test/src/test_catalog_repository.cpp`
- Test: `libraries/Core/test/src/test_catalog_injection.cpp`

**Interfaces:**
- Consumes: `BookSql::filterClause` / `bindFilters`, `kBookSelect` / `kBookFrom`
- Produces: `namespace BookSort` (`title`, `author`, `category`, `isbn`, `language`, `copies`, `available`); `BookQuery::sortColumn` / `sortAscending`; `BookSql::orderExpressions` / `orderClause`; `Result<int> CatalogRepository::rankOfBook(std::int64_t id, const BookQuery& query) const`. Default order: `b.title COLLATE NOCASE ASC, b.id ASC` (id tiebreak also fixes `ListBooksPagesAreStableWithDuplicateTitles`).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_F(test_core_CatalogRepository, ListBooksSortsByTitleAndCopies)
{
    const std::int64_t cat = seedCategory(*m_db, "SRT", "Sort");
    ASSERT_GT(cat, 0);

    BookSeed zebra = uniqueBookSeed(501);
    zebra.title = "zebra";
    zebra.categoryId = cat;
    zebra.initialCopyCount = 1;
    const std::int64_t idZ = seedBook(*m_db, zebra);
    BookSeed apple = uniqueBookSeed(502);
    apple.title = "Apple";
    apple.categoryId = cat;
    apple.initialCopyCount = 3;
    const std::int64_t idA = seedBook(*m_db, apple);
    ASSERT_GT(idZ, 0);
    ASSERT_GT(idA, 0);

    BookQuery byTitle;
    byTitle.sortColumn = BookSort::kTitle;
    byTitle.sortAscending = true;
    const auto titles = VLMS_UNWRAP(m_repository->listBooks(byTitle));
    ASSERT_EQ(titles.size(), 2u);
    EXPECT_EQ(titles.at(0).id, idA);
    EXPECT_EQ(titles.at(1).id, idZ);

    BookQuery byCopies;
    byCopies.sortColumn = BookSort::kCopies;
    byCopies.sortAscending = false;
    const auto copies = VLMS_UNWRAP(m_repository->listBooks(byCopies));
    ASSERT_EQ(copies.size(), 2u);
    EXPECT_EQ(copies.at(0).id, idA);
    EXPECT_EQ(copies.at(1).id, idZ);

    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfBook(idA, byTitle)), 0);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfBook(idZ, byTitle)), 1);
}

TEST_F(test_core_CatalogInjection, HostileSortColumnUsesDefaultOrder)
{
    BookQuery safe;
    const auto expected = VLMS_UNWRAP(m_repository->listBooks(safe));
    ASSERT_FALSE(expected.empty());

    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);
        BookQuery query;
        query.sortColumn = entry.value;
        const auto rows = VLMS_UNWRAP(m_repository->listBooks(query));
        ASSERT_EQ(rows.size(), expected.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            EXPECT_EQ(rows.at(i).id, expected.at(i).id);
        }
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target test_vlms_core --parallel
./build/libraries/Core/test/test_vlms_core --gtest_filter='test_core_CatalogRepository.ListBooksSortsByTitleAndCopies:test_core_CatalogInjection.HostileSortColumnUsesDefaultOrder'
```

Expected: compile fail.

- [ ] **Step 3: Write minimal implementation**

`CatalogTypes.h` after `CoverFilter`:

```cpp
namespace BookSort {
inline constexpr auto kTitle = "title";
inline constexpr auto kAuthor = "author";
inline constexpr auto kCategory = "category";
inline constexpr auto kIsbn = "isbn";
inline constexpr auto kLanguage = "language";
inline constexpr auto kCopies = "copies";
inline constexpr auto kAvailable = "available";
}  // namespace BookSort
```

Add to `BookQuery`:

```cpp
    std::string sortColumn;
    bool sortAscending = true;
```

`BookSql.h` / `.cpp` — same `direction` / `withDirection` helpers as MemberSql (duplicate them in this file; do not share a new Core header). Expressions:

- `title` → `b.title COLLATE NOCASE`, `b.id`
- `author` → `COALESCE(a.name, '') COLLATE NOCASE`, `b.id`
- `category` → `COALESCE(NULLIF(TRIM(c.label), ''), c.code, '') COLLATE NOCASE`, `b.id`
- `isbn` → `COALESCE(b.isbn, '') COLLATE NOCASE`, `b.id`
- `language` → `b.language COLLATE NOCASE`, `b.id`
- `copies` → `COUNT(bc.id)`, `b.id`
- `available` → `COALESCE(SUM(CASE WHEN bc.id IS NOT NULL AND active_loan.id IS NULL THEN 1 ELSE 0 END), 0)`, `b.id`
- default → `b.title COLLATE NOCASE ASC, b.id ASC`

`listBooks`: replace `ORDER BY b.title COLLATE NOCASE` with `BookSql::orderClause(query)`.

`rankOfBook`:

```cpp
Result<int> CatalogRepository::rankOfBook(const std::int64_t id, const BookQuery& query) const
{
    std::string sql =
        "SELECT ranked.rank FROM (\n"
        "    SELECT b.id, (ROW_NUMBER() OVER (ORDER BY "
        + BookSql::orderExpressions(query) + ")) - 1 AS rank\n"
        + kBookFrom
        + " WHERE LENGTH(TRIM(b.title)) > 0"
        + BookSql::filterClause(query)
        + " GROUP BY b.id\n"
          ") ranked WHERE ranked.id = :id\n";
    // prepare, BookSql::bindFilters, bind :id
    // empty → RepoSql::notFoundResult<int>("error.book.notFound")
}
```

Use the same prepare/bind/next pattern as `rankOfMember`. Missing id → `RepoSql::notFoundResult<int>("error.book.notFound")`.

- [ ] **Step 4: Run tests to verify they pass**

Same filter as Step 2. Also run `test_core_CatalogRepository.ListBooksPagesAreStableWithDuplicateTitles` — it must stay green (default order now includes `b.id`).

- [ ] **Step 5: Commit**

```bash
git add libraries/Core/include/VLMS/Core/CatalogTypes.h \
        libraries/Core/include/VLMS/Core/CatalogRepository.h \
        libraries/Core/src/BookSql.h libraries/Core/src/BookSql.cpp \
        libraries/Core/src/CatalogRepository.cpp \
        libraries/Core/test/src/test_catalog_repository.cpp \
        libraries/Core/test/src/test_catalog_injection.cpp
git commit -m "$(cat <<'EOF'
Sort catalog lists from a whitelisted query column.

EOF
)"
```

---

### Task 4: Loan SQL sort and rank

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/LoanTypes.h`
- Modify: `libraries/Core/include/VLMS/Core/CirculationRepository.h`
- Modify: `libraries/Core/src/LoanSql.h`
- Modify: `libraries/Core/src/LoanSql.cpp`
- Modify: `libraries/Core/src/CirculationRepository.cpp`
- Test: `libraries/Core/test/src/test_circulation_repository.cpp`
- Test: `libraries/Core/test/src/test_circulation_injection.cpp`

**Interfaces:**
- Consumes: `LoanSql::filterClause` / `bindFilters` / `isOverdue` / `bindTodayIfPresent`
- Produces: `namespace LoanSort` (`member`, `number`, `title`, `borrowed`, `due`, `status`); `LoanQuery::sortColumn` / `sortAscending`; `LoanSql::orderExpressions` / `orderClause`; `Result<int> CirculationRepository::rankOfLoan(std::int64_t id, const LoanQuery& query) const`. Default expressions stay: open first, overdue first, `l.due_at ASC`, `l.id DESC`.

- [ ] **Step 1: Write the failing tests**

The fixture already seeds `m_activeMemberId` and four `m_copyIds`. Insert two open loans with fixed due dates:

```cpp
TEST_F(test_core_CirculationRepository, ListLoansSortsByDueDate)
{
    ASSERT_GT(rawInsertLoan(*m_db, m_activeMemberId, m_copyIds.at(0),
                            "2021-01-01", "2021-06-01"),
              0);
    ASSERT_GT(rawInsertLoan(*m_db, m_activeMemberId, m_copyIds.at(1),
                            "2021-01-01", "2021-01-15"),
              0);

    LoanQuery query;
    query.sortColumn = LoanSort::kDue;
    query.sortAscending = true;
    const auto asc = VLMS_UNWRAP(m_repository->listLoans(query));
    ASSERT_EQ(asc.size(), 2u);
    EXPECT_EQ(asc.at(0).dueAt, "2021-01-15");
    EXPECT_EQ(asc.at(1).dueAt, "2021-06-01");

    query.sortAscending = false;
    const auto desc = VLMS_UNWRAP(m_repository->listLoans(query));
    ASSERT_EQ(desc.size(), 2u);
    EXPECT_EQ(desc.at(0).dueAt, "2021-06-01");
    EXPECT_EQ(desc.at(1).dueAt, "2021-01-15");

    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfLoan(asc.at(0).id, query)), 1);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfLoan(asc.at(1).id, query)), 0);
}
```

Injection:

```cpp
TEST_F(test_core_CirculationInjection, HostileSortColumnUsesDefaultOrder)
{
    LoanQuery safe;
    const auto expected = VLMS_UNWRAP(m_repository->listLoans(safe));
    ASSERT_FALSE(expected.empty());
    for (const HostilePayload& entry : hostilePayloads()) {
        SCOPED_TRACE(entry.id);
        LoanQuery query;
        query.sortColumn = entry.value;
        const auto rows = VLMS_UNWRAP(m_repository->listLoans(query));
        ASSERT_EQ(rows.size(), expected.size());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            EXPECT_EQ(rows.at(i).id, expected.at(i).id);
        }
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target test_vlms_core --parallel
./build/libraries/Core/test/test_vlms_core --gtest_filter='test_core_CirculationRepository.ListLoansSortsByDueDate:test_core_CirculationInjection.HostileSortColumnUsesDefaultOrder'
```

Expected: compile fail.

- [ ] **Step 3: Write minimal implementation**

`LoanSort` keys as specified. `orderExpressions`:

- `member` → `TRIM(m.first_name || ' ' || m.last_name) COLLATE NOCASE`, `l.id`
- `number` → `CAST(m.membership_number AS INTEGER)`, `m.membership_number COLLATE NOCASE`, `l.id`
- `title` → `b.title COLLATE NOCASE`, `l.id`
- `borrowed` → `l.borrowed_at`, `l.id`
- `due` → `l.due_at`, `l.id`
- `status` → `CASE WHEN l.returned_at IS NOT NULL THEN 2 WHEN ` + `LoanSql::isOverdue("l.")` + ` THEN 0 ELSE 1 END`, `l.due_at`, `l.id`
- default →  
  `CASE WHEN l.returned_at IS NULL THEN 0 ELSE 1 END ASC,`  
  `CASE WHEN ` + `isOverdue("l.")` + ` THEN 0 ELSE 1 END ASC,`  
  `l.due_at ASC, l.id DESC`

`listLoans`: replace the current `ORDER BY` block with `LoanSql::orderClause(query)`.

`rankOfLoan`: same window-function wrapper as members, from `loans l` plus the same joins as `loanSelectSql()` (`members`, `book_copies`, `books`, `authors`), `WHERE 1 = 1` + `filterClause`. Call `LoanSql::bindTodayIfPresent` because status/default expressions may embed `:today`. Missing id → `error.loan.notFound` (copy `getLoan`’s key).

- [ ] **Step 4: Run tests to verify they pass**

Same filter as Step 2. Expected: PASS. Also run `test_core_CirculationDates.*` if the default order string changed — overdue-first default must be unchanged when `sortColumn` is empty.

- [ ] **Step 5: Commit**

```bash
git add libraries/Core/include/VLMS/Core/LoanTypes.h \
        libraries/Core/include/VLMS/Core/CirculationRepository.h \
        libraries/Core/src/LoanSql.h libraries/Core/src/LoanSql.cpp \
        libraries/Core/src/CirculationRepository.cpp \
        libraries/Core/test/src/test_circulation_repository.cpp \
        libraries/Core/test/src/test_circulation_injection.cpp
git commit -m "$(cat <<'EOF'
Sort loan lists from a whitelisted query column.

EOF
)"
```

---

### Task 5: Header-click helper and pager page set

**Files:**
- Create: `applications/vlms/src/ui/TableHeaderSort.h`
- Create: `applications/vlms/src/ui/TableHeaderSort.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (add both files to `vlms_ui`)
- Modify: `applications/vlms/src/ui/TablePager.h`
- Modify: `applications/vlms/src/ui/TablePager.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt`
- Create: `applications/vlms/test/src/test_table_header_sort.cpp`
- Modify: `applications/vlms/test/src/test_table_pager.cpp`

**Interfaces:**
- Consumes: `QTableWidget`, `QHeaderView`, existing `TablePager`
- Produces:

```cpp
namespace VLMS {
class TableHeaderSort : public QObject {
    Q_OBJECT
public:
    explicit TableHeaderSort(QTableWidget* table, QObject* parent = nullptr);
    void setColumnKeys(const QStringList& keys);
    [[nodiscard]] int column() const;
    [[nodiscard]] bool ascending() const;
    [[nodiscard]] bool isActive() const;   // false until first click
    [[nodiscard]] QString columnKey() const; // empty if inactive / no keys
signals:
    void sortChanged(int column, bool ascending);
};

void enableWidgetTableSort(QTableWidget* table,
                           int idColumn = 0,
                           int idRole = Qt::UserRole);
}

class TablePager {
    void setCurrentPage(int page); // clamp 1..pageCount; no pageChanged signal
};
```

Click rules: first click on a column → ascending; same column again → toggle; other column → ascending. Shows `header->setSortIndicatorShown(true)` and `setSortIndicator(column, order)`.

`enableWidgetTableSort` constructs a `TableHeaderSort` child of `table`, and on `sortChanged` stores the selected row’s id (if selection is allowed), calls `table->sortItems(column, order)`, restores the row with that id. `NoSelection` tables only sort.

- [ ] **Step 1: Write the failing tests**

`test_table_header_sort.cpp`:

```cpp
#include "ui/TableHeaderSort.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <gtest/gtest.h>

TEST(test_ui_TableHeaderSort, FirstClickAscendingThenToggles)
{
    QTableWidget table(0, 2);
    table.setHorizontalHeaderLabels({QStringLiteral("A"), QStringLiteral("B")});
    VLMS::TableHeaderSort sort(&table);
    sort.setColumnKeys({QStringLiteral("a"), QStringLiteral("b")});

    int lastColumn = -1;
    bool lastAsc = false;
    int fires = 0;
    QObject::connect(&sort, &VLMS::TableHeaderSort::sortChanged,
                     [&](int column, bool ascending) {
                         lastColumn = column;
                         lastAsc = ascending;
                         ++fires;
                     });

    emit table.horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(sort.column(), 0);
    EXPECT_TRUE(sort.ascending());
    EXPECT_EQ(sort.columnKey(), QStringLiteral("a"));
    EXPECT_EQ(table.horizontalHeader()->sortIndicatorSection(), 0);
    EXPECT_EQ(table.horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);

    emit table.horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(fires, 2);
    EXPECT_FALSE(sort.ascending());
    EXPECT_EQ(table.horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);

    emit table.horizontalHeader()->sectionClicked(1);
    EXPECT_EQ(fires, 3);
    EXPECT_EQ(sort.column(), 1);
    EXPECT_TRUE(sort.ascending());
    EXPECT_EQ(sort.columnKey(), QStringLiteral("b"));
}

TEST(test_ui_TableHeaderSort, WidgetSortRestoresSelection)
{
    QTableWidget table(2, 1);
    auto* keep = new QTableWidgetItem(QStringLiteral("b"));
    keep->setData(Qt::UserRole, 20);
    auto* other = new QTableWidgetItem(QStringLiteral("a"));
    other->setData(Qt::UserRole, 10);
    table.setItem(0, 0, keep);
    table.setItem(1, 0, other);
    table.selectRow(0);
    VLMS::enableWidgetTableSort(&table);

    emit table.horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table.item(0, 0)->data(Qt::UserRole).toInt(), 10);
    EXPECT_EQ(table.item(table.currentRow(), 0)->data(Qt::UserRole).toInt(), 20);
}
```

In `test_table_pager.cpp` add:

```cpp
TEST(test_ui_TablePager, SetCurrentPageDoesNotEmit)
{
    TablePager pager;
    pager.setPageSize(2);
    pager.setTotalCount(10);
    QSignalSpy spy(&pager, &TablePager::pageChanged);
    pager.setCurrentPage(3);
    EXPECT_EQ(pager.currentPage(), 3);
    EXPECT_EQ(pager.offset(), 4);
    EXPECT_EQ(spy.count(), 0);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target test_vlms_ui --parallel
QT_QPA_PLATFORM=offscreen ./build/applications/vlms/test/test_vlms_ui --gtest_filter='test_ui_TableHeaderSort.*:test_ui_TablePager.SetCurrentPageDoesNotEmit'
```

Expected: compile fail until the files exist and are in CMake.

- [ ] **Step 3: Write minimal implementation**

`TableHeaderSort.cpp`:

```cpp
#include "ui/TableHeaderSort.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace VLMS {

TableHeaderSort::TableHeaderSort(QTableWidget* table, QObject* parent)
    : QObject(parent == nullptr ? table : parent),
      m_table(table)
{
    if (m_table == nullptr) {
        return;
    }
    QHeaderView* header = m_table->horizontalHeader();
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    connect(header, &QHeaderView::sectionClicked, this, &TableHeaderSort::onSectionClicked);
}

void TableHeaderSort::setColumnKeys(const QStringList& keys)
{
    m_keys = keys;
}

int TableHeaderSort::column() const { return m_column; }
bool TableHeaderSort::ascending() const { return m_ascending; }
bool TableHeaderSort::isActive() const { return m_column >= 0; }

QString TableHeaderSort::columnKey() const
{
    if (m_column < 0 || m_column >= m_keys.size()) {
        return {};
    }
    return m_keys.at(m_column);
}

void TableHeaderSort::onSectionClicked(const int column)
{
    if (m_column == column) {
        m_ascending = !m_ascending;
    } else {
        m_column = column;
        m_ascending = true;
    }
    m_table->horizontalHeader()->setSortIndicator(
        m_column, m_ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    emit sortChanged(m_column, m_ascending);
}

void enableWidgetTableSort(QTableWidget* table, const int idColumn, const int idRole)
{
    if (table == nullptr) {
        return;
    }
    auto* sort = new TableHeaderSort(table, table);
    QObject::connect(sort, &TableHeaderSort::sortChanged, table,
                     [table, idColumn, idRole](int column, bool ascending) {
                         qint64 selected = 0;
                         if (table->selectionMode() != QAbstractItemView::NoSelection) {
                             const int row = table->currentRow();
                             if (row >= 0 && table->item(row, idColumn) != nullptr) {
                                 selected = table->item(row, idColumn)->data(idRole).toLongLong();
                             }
                         }
                         table->sortItems(column,
                                          ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
                         if (selected <= 0) {
                             return;
                         }
                         for (int row = 0; row < table->rowCount(); ++row) {
                             const QTableWidgetItem* item = table->item(row, idColumn);
                             if (item != nullptr
                                 && item->data(idRole).toLongLong() == selected) {
                                 table->selectRow(row);
                                 return;
                             }
                         }
                     });
}

}  // namespace VLMS
```

Header file: declare the class and `enableWidgetTableSort` as in the Interfaces block. Include `QObject`, `QStringList`; forward-declare `QTableWidget`.

`TablePager::setCurrentPage`:

```cpp
void TablePager::setCurrentPage(int page)
{
    const int pages = pageCount();
    m_currentPage = qBound(1, page, pages);
    updateControls();
}
```

Do **not** emit `pageChanged`. Add the declaration next to `resetToFirstPage`.

CMake: add `src/ui/TableHeaderSort.cpp` and `src/ui/TableHeaderSort.h` to `vlms_ui`. Add `src/test_table_header_sort.cpp` to `test_vlms_ui` `SRC`.

- [ ] **Step 4: Run tests to verify they pass**

Same filter as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add applications/vlms/src/ui/TableHeaderSort.h \
        applications/vlms/src/ui/TableHeaderSort.cpp \
        applications/vlms/src/ui/TablePager.h \
        applications/vlms/src/ui/TablePager.cpp \
        applications/vlms/CMakeLists.txt \
        applications/vlms/test/CMakeLists.txt \
        applications/vlms/test/src/test_table_header_sort.cpp \
        applications/vlms/test/src/test_table_pager.cpp
git commit -m "$(cat <<'EOF'
Add header-click sort helper and silent pager page set.

EOF
)"
```

---

### Task 6: Members page wiring

**Files:**
- Modify: `applications/vlms/src/ui/members/MembersPage.h`
- Modify: `applications/vlms/src/ui/members/MembersPage.cpp`
- Create: `applications/vlms/test/src/test_member_sort.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `TableHeaderSort`, `TablePager::setCurrentPage`, `MemberQuery` sort fields, `MemberRepository::rankOfMember`, `MemberSort` keys
- Produces: Members table headers clickable; `currentMemberQuery()` copies `m_sort->columnKey()` / `m_sort->ascending()` when `isActive()`

Column keys, in header order: `number`, `name`, `phone`, `city`, `status`, `loans`.

On `sortChanged`:
1. Read `selectedMemberId()` first.
2. If `id <= 0`: `m_pager->setCurrentPage(1)`.
3. Else: build `MemberQuery` from `currentMemberQuery()`, call `rankOfMember`. On success, `setCurrentPage(rank / pageSize + 1)`. On failure, `setCurrentPage(1)`.
4. `refreshMembers()`.
5. If `id > 0`, re-select the row whose `UserRole` is `id` (same loop as `retranslateUi`).

Do **not** call `setSortingEnabled(true)` on this table (that would sort only the current page).

- [ ] **Step 1: Write the failing UI tests**

`test_member_sort.cpp` — same fixture style as `test_member_filters.cpp` (`TestDatabase`, `MemberRepository`, `CirculationRepository`, `MembersPage`, `Locale::setCode("en")`).

```cpp
TEST_F(test_ui_MemberSort, ClickingNumberHeaderSortsAscendingThenDescending)
{
    MemberSeed late = uniqueMemberSeed(1);
    late.membershipNumber = "10";
    late.lastName = "Aaa";
    ASSERT_GT(seedMember(*m_db, late), 0);
    MemberSeed early = uniqueMemberSeed(2);
    early.membershipNumber = "2";
    early.lastName = "Zzz";
    ASSERT_GT(seedMember(*m_db, early), 0);

    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 2);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("10"));

    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("2"));
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("10"));
}

TEST_F(test_ui_MemberSort, NoSelectionReturnsToPageOne)
{
    for (int i = 0; i < 5; ++i) {
        MemberSeed seed = uniqueMemberSeed(10 + i);
        seed.membershipNumber = std::to_string(i + 1);
        seed.lastName = std::string(1, static_cast<char>('E' - i));
        ASSERT_GT(seedMember(*m_db, seed), 0);
    }
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* table = m_page->findChild<QTableWidget*>();
    auto* pager = m_page->findChild<VLMS::TablePager*>();
    ASSERT_NE(table, nullptr);
    ASSERT_NE(pager, nullptr);
    pager->setPageSize(2);
    pager->setCurrentPage(2);
    table->clearSelection();
    table->setCurrentItem(nullptr);

    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(pager->currentPage(), 1);
}

TEST_F(test_ui_MemberSort, SelectedRowKeepsFocusAcrossPages)
{
    std::int64_t firstId = 0;
    for (int i = 0; i < 5; ++i) {
        MemberSeed seed = uniqueMemberSeed(20 + i);
        seed.membershipNumber = std::to_string(i + 1);
        seed.lastName = std::string(1, static_cast<char>('A' + i));
        const std::int64_t id = seedMember(*m_db, seed);
        ASSERT_GT(id, 0);
        if (i == 0) {
            firstId = id;
        }
    }
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* table = m_page->findChild<QTableWidget*>();
    auto* pager = m_page->findChild<VLMS::TablePager*>();
    ASSERT_NE(table, nullptr);
    ASSERT_NE(pager, nullptr);
    pager->setPageSize(2);

    // default last-name order: A=1 is row 0 and selected
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), firstId);

    emit table->horizontalHeader()->sectionClicked(0); // number asc: 1,2,3,4,5 — still page 1
    emit table->horizontalHeader()->sectionClicked(0); // number desc: 5,4,3,2,1 — A=1 is last
    EXPECT_EQ(pager->currentPage(), 3);
    bool found = false;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->data(Qt::UserRole).toLongLong() == firstId) {
            found = table->item(row, 0)->isSelected();
        }
    }
    EXPECT_TRUE(found);
}
```

Add `src/test_member_sort.cpp` to the UI test `SRC` list.

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build --target test_vlms_ui --parallel
QT_QPA_PLATFORM=offscreen ./build/applications/vlms/test/test_vlms_ui --gtest_filter='test_ui_MemberSort.*'
```

Expected: first test FAIL (still last-name order, `"10"` stays first). Others FAIL (pager stays on page 2 / row not followed).

- [ ] **Step 3: Write minimal implementation**

`MembersPage.h`: include `ui/TableHeaderSort.h`; add `VLMS::TableHeaderSort* m_sort = nullptr;` and `void onSortChanged(int column, bool ascending);` `void selectMemberId(qint64 id);`

In `buildUi`, after the table exists:

```cpp
    m_sort = new VLMS::TableHeaderSort(m_membersTable, this);
    m_sort->setColumnKeys({
        QString::fromLatin1(MemberSort::kNumber),
        QString::fromLatin1(MemberSort::kName),
        QString::fromLatin1(MemberSort::kPhone),
        QString::fromLatin1(MemberSort::kCity),
        QString::fromLatin1(MemberSort::kStatus),
        QString::fromLatin1(MemberSort::kLoans),
    });
    connect(m_sort, &VLMS::TableHeaderSort::sortChanged,
            this, &MembersPage::onSortChanged);
```

`currentMemberQuery()`:

```cpp
    if (m_sort != nullptr && m_sort->isActive()) {
        query.sortColumn = ss(m_sort->columnKey());
        query.sortAscending = m_sort->ascending();
    }
```

```cpp
void MembersPage::selectMemberId(const qint64 id)
{
    if (id <= 0) {
        return;
    }
    for (int row = 0; row < m_membersTable->rowCount(); ++row) {
        if (m_membersTable->item(row, 0)->data(Qt::UserRole).toLongLong() == id) {
            m_membersTable->selectRow(row);
            return;
        }
    }
}

void MembersPage::onSortChanged(int, bool)
{
    const qint64 id = selectedMemberId();
    if (id <= 0) {
        m_pager->setCurrentPage(1);
    } else {
        MemberQuery query = currentMemberQuery();
        const auto rank = m_repository.rankOfMember(id, query);
        if (rank) {
            m_pager->setCurrentPage(rank.value() / m_pager->pageSize() + 1);
        } else {
            m_pager->setCurrentPage(1);
        }
    }
    refreshMembers();
    selectMemberId(id);
}
```

Use `selectMemberId` from `retranslateUi` as well so the loop is not duplicated.

For the loans column, set the item’s `DisplayRole` to the integer count so a future in-widget path would sort numerically; SQL already handles this page.

- [ ] **Step 4: Run tests to verify they pass**

Same filter as Step 2. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add applications/vlms/src/ui/members/MembersPage.h \
        applications/vlms/src/ui/members/MembersPage.cpp \
        applications/vlms/test/src/test_member_sort.cpp \
        applications/vlms/test/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Sort the members table from its column headers.

EOF
)"
```

---

### Task 7: Catalog and Circulation page wiring

**Files:**
- Modify: `applications/vlms/src/ui/catalog/CatalogPage.h`
- Modify: `applications/vlms/src/ui/catalog/CatalogPage.cpp`
- Modify: `applications/vlms/src/ui/circulation/CirculationPage.h`
- Modify: `applications/vlms/src/ui/circulation/CirculationPage.cpp`

**Interfaces:**
- Consumes: `TableHeaderSort`, `setCurrentPage`, `rankOfBook` / `rankOfLoan`, `BookSort` / `LoanSort`
- Produces: same click / page / selection behaviour as Members

Catalog keys in header order: `title`, `author`, `category`, `isbn`, `language`, `copies`, `available`.  
Circulation keys: `member`, `number`, `title`, `borrowed`, `due`, `status`.

- [ ] **Step 1: Write a failing catalog UI test**

Create `applications/vlms/test/src/test_catalog_sort.cpp` (register it in `test/CMakeLists.txt`). Fixture: `TestDatabase`, `CatalogRepository`, `CatalogPage`, `Locale::setCode("en")`.

```cpp
TEST_F(test_ui_CatalogSort, ClickingTitleHeaderSortsAscending)
{
    BookSeed zebra = uniqueBookSeed(1);
    zebra.title = "zebra";
    ASSERT_GT(seedBook(*m_db, zebra), 0);
    BookSeed apple = uniqueBookSeed(2);
    apple.title = "Apple";
    ASSERT_GT(seedBook(*m_db, apple), 0);

    m_page = std::make_unique<CatalogPage>(*m_catalog);
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("Apple"));
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("zebra"));
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target test_vlms_ui --parallel
QT_QPA_PLATFORM=offscreen ./build/applications/vlms/test/test_vlms_ui --gtest_filter='test_ui_CatalogSort.*'
```

Expected: FAIL (titles stay in insert / default order that may already be Apple-first — if default already puts Apple first, assert the descending click: after the second click the first row must be `zebra`).

- [ ] **Step 3: Write minimal implementation**

Catalog: `m_sort`, `onSortChanged`, `selectBookId` — copy the Members methods, substituting `m_booksTable`, `selectedBookId()`, `m_repository.rankOfBook`, `refreshBooks()`, and BookSort keys.

`currentBookQuery()` sets `sortColumn` / `sortAscending` from `m_sort` when active.

Circulation: `m_sort`, `onSortChanged`, `selectLoanId` — same, with `m_loansTable`, `selectedLoanId()`, `m_repository.rankOfLoan`, `refreshLoans()`, LoanSort keys.

`currentLoanQuery()` sets the sort fields the same way.

Do not enable `setSortingEnabled` on either table.

- [ ] **Step 4: Run tests to verify they pass**

```bash
QT_QPA_PLATFORM=offscreen ./build/applications/vlms/test/test_vlms_ui --gtest_filter='test_ui_CatalogSort.*:test_ui_MemberSort.*:test_ui_MemberFilters.*'
```

Expected: PASS (member filter tests still green).

- [ ] **Step 5: Commit**

```bash
git add applications/vlms/src/ui/catalog/CatalogPage.h \
        applications/vlms/src/ui/catalog/CatalogPage.cpp \
        applications/vlms/src/ui/circulation/CirculationPage.h \
        applications/vlms/src/ui/circulation/CirculationPage.cpp \
        applications/vlms/test/src/test_catalog_sort.cpp \
        applications/vlms/test/CMakeLists.txt
git commit -m "$(cat <<'EOF'
Sort catalog and circulation tables from their headers.

EOF
)"
```

---

### Task 8: In-widget tables

**Files:**
- Modify: `applications/vlms/src/ui/catalog/CategoryManagerDialog.cpp`
- Modify: `applications/vlms/src/ui/members/MemberLoansDialog.cpp`
- Modify: `applications/vlms/src/ui/metrics/MetricsPage.cpp`
- Modify: `applications/vlms/src/ui/catalog/BookCopiesTable.cpp`
- Test: `applications/vlms/test/src/test_table_header_sort.cpp` (add CategoryManager case) or new `test_category_sort.cpp`

**Interfaces:**
- Consumes: `enableWidgetTableSort`
- Produces: those tables sort on header click and restore selection by stored id. Copy header sort does not write copy order to the database (`copyInputs()` still walks current visual rows — that is existing behaviour and is acceptable; do not add a persist-on-sort path).

Numeric cells must use an integer `Qt::DisplayRole` so `2` sorts before `10`:

```cpp
auto* item = new QTableWidgetItem();
item->setData(Qt::DisplayRole, count);
```

- [ ] **Step 1: Write the failing test**

```cpp
#include "ui/catalog/CategoryManagerDialog.h"
#include <VLMS/Core/CatalogRepository.h>
#include "TestDatabase.h"
#include "TestSeed.h"
#include <QHeaderView>
#include <QTableWidget>
#include <gtest/gtest.h>

TEST(test_ui_CategorySort, HeaderClickSortsByCode)
{
    VLMS::Test::TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    CatalogRepository repository(db.session(), db.resourcesDirectory());
    ASSERT_GT(VLMS::Test::seedCategory(db, "Z9", "Zebra"), 0);
    ASSERT_GT(VLMS::Test::seedCategory(db, "A1", "Apple"), 0);

    CategoryManagerDialog dialog(repository);
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("A1"));
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("Z9"));
}
```

Add the `.cpp` to the UI test `SRC` list if it is a new file.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target test_vlms_ui --parallel
QT_QPA_PLATFORM=offscreen ./build/applications/vlms/test/test_vlms_ui --gtest_filter='test_ui_CategorySort.*'
```

Expected: FAIL (order stays load order, typically `Z9` then `A1` if that is insert order — `listAllCategories` is `ORDER BY c.code`, so A1 may already be first; then the second click must put `Z9` first).

- [ ] **Step 3: Write minimal implementation**

`CategoryManagerDialog::buildUi` after column setup:

```cpp
    VLMS::enableWidgetTableSort(m_table);
```

In `refresh()`, set book count as an integer DisplayRole:

```cpp
        auto* countItem = new QTableWidgetItem();
        countItem->setData(Qt::DisplayRole, category.bookCount);
        m_table->setItem(row, 2, countItem);
```

`MemberLoansDialog::buildUi`: `enableWidgetTableSort(m_table)`. In `refresh()`, store `loan.id` on column 0 `Qt::UserRole`.

`MetricsPage::buildUi`: after `configureMetricTable`, call `enableWidgetTableSort` on both tables. Change `makeCenteredTableItem` users for numeric cells to set `Qt::DisplayRole` to the `int` (keep alignment center). Period labels stay strings.

`BookCopiesTable` constructor, after the table is configured:

```cpp
    VLMS::enableWidgetTableSort(m_table, kCopyLocalId, kCopyIdRole);
```

`kCopyIdRole` is currently in the anonymous namespace — move `kCopyIdRole` / `kCopyLocalId` so the call can see them (they are already in that file’s anonymous namespace; call `enableWidgetTableSort` from the same `.cpp` after the enum). Leave the source combo as-is; keep a `QTableWidgetItem` on that column with the source code (`arabic` / `foreign`) *and* the combo widget so `sortItems` has an item to compare.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build --target test_vlms_core test_vlms_ui --parallel
./build/libraries/Core/test/test_vlms_core --gtest_filter='*Sort*:*HostileSortColumn*:*RankOf*'
QT_QPA_PLATFORM=offscreen ./build/applications/vlms/test/test_vlms_ui --gtest_filter='*Sort*:*MemberFilters*:*TableHeaderSort*:*TablePager.SetCurrentPage*'
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add applications/vlms/src/ui/catalog/CategoryManagerDialog.cpp \
        applications/vlms/src/ui/members/MemberLoansDialog.cpp \
        applications/vlms/src/ui/metrics/MetricsPage.cpp \
        applications/vlms/src/ui/catalog/BookCopiesTable.cpp \
        applications/vlms/test/CMakeLists.txt \
        applications/vlms/test/src/test_category_sort.cpp
git commit -m "$(cat <<'EOF'
Sort dialog, metrics, and copy tables in the widget.

EOF
)"
```

Add a one-line Session Log entry to `CLAUDE.md` in this commit or a follow-up: header-click sort on every table; list pages sort in SQL and follow the selected row.

---

## Self-review

**Spec coverage**

| Spec item | Task |
|---|---|
| Click / toggle / other column starts ASC | 5, 6, 7, 8 |
| Qt sort arrow | 5 |
| Text `COLLATE NOCASE` / numeric / `123b` | 1, 3, 4, 8 |
| Default order until first click | 1, 3, 4 (`orderExpressions` default) |
| Session-only | no persist code anywhere |
| Filters unchanged | query only adds sort fields |
| No selection → page 1 | 6 |
| Selection → rank page + keep focus | 2, 6, 7 |
| SQL whitelist, no caption in SQL | 1, 3, 4 + injection tests |
| `rankOf` + fallback page 1 | 2, 3, 4, 6, 7 |
| In-widget dialogs / metrics / copies | 8 |
| Copies sort does not write DB order | 8 (no persist path) |
| Core tests (text + numeric + rank + injection) | 1–4 |
| UI tests (click, page 1, follow row) | 6 |

**Placeholder scan:** none. Loan seed in Task 4 uses `rawInsertLoan` with the fixture’s `m_activeMemberId` / `m_copyIds`.

**Type consistency:** `sortColumn` / `sortAscending` on all three queries; `orderExpressions` / `orderClause` in all three SQL helpers; `rankOfMember` / `rankOfBook` / `rankOfLoan`; `TableHeaderSort` + `enableWidgetTableSort`; `TablePager::setCurrentPage`.
