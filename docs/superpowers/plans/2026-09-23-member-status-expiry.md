# Member Status Expiry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Two member statuses, Active and Not active, read from a stored last-active day (`members.active_until`) that a registration or a renewal sets one year ahead.

**Architecture:** Status stops being stored. `members.active_until` holds the member's last active day, and one SQL expression in `MemberSql` turns it into `'active'` / `'non_active'` against `Clock`'s today, bound as `:today`. The rule that moves the date is two static functions on `MemberRepository` (`statusOn`, `activeUntilFor`), shared by the repository and the editor's preview. The schema moves in two steps, so every task ends green: the column is added first (Task 2), and `status` is dropped only after nothing reads it (Task 4).

**Tech Stack:** Qt 6 Widgets, C++20, SQLite (≥ 3.35 for `ALTER TABLE … DROP COLUMN`) via `VLMS::SqliteSession`, GoogleTest, Python 3 for the import script.

## Global Constraints

- **Prerequisite:** the `permanent-removal-funnel` branch (worktree `.worktrees/permanent-removal-funnel`, 10 commits ahead of `Beta`) is merged into `Beta` before this work branches. It adds `loan_count` to the member select, so `readMemberRow` reads `archived_at` at index 21. Every index in this plan assumes that merge. If it has not happened, stop and ask.
- Work on a feature branch `member-status-expiry` cut from `Beta`. Commit only on that branch.
- **Two status codes, exactly:** `active` and `non_active` (`MemberStatus::kActive`, `MemberStatus::kNonActive`). `subscribed` and `unsubscribed` must not survive in any `.cpp`, `.h`, `.sql` (other than legacy test fixtures under `libraries/Core/test/data/`) or string table.
- **Rule (from the spec, verbatim):** a member is active on day `d` exactly when `active_until >= d`. New registration → registration date + 1 year − 1 day. A not-active member set to Active → today + 1 year − 1 day. An active member set to Not active → yesterday. Status unchanged → date unchanged. **Borrowing is refused if the member is not active.** Returning is unaffected.
- **Today comes from `Clock`, never SQLite's `'now'`.** C++ uses `Clock::today()`; SQL binds `LoanSql::todayPlaceholder()` (`":today"`) with `Clock::todayIso()`, or calls `LoanSql::bindTodayIfPresent(stmt, sql)`.
- **Date arithmetic matches SQLite:** `date(x, '+1 year', '-1 day')`. A leap day plus one year is 1 March, so 2024-02-29 registers through 2025-02-28. `Date::addYears` (Task 1) does the same.
- **Repositories are Qt-free.** Core uses `std::string`, `std::vector` and `std::int64_t`, and returns `VLMS::Result<T>` / `Status`.
- **Every user-visible string has three entries** (Arabic, French, English) in `libraries/Core/src/Strings.cpp`. `test_core_StringsParity` fails when one is missing.
- **British English in prose and comments.** Existing identifiers stay as they are (`CatalogPage`, `catalog`).
- **Status-history notes are plain English literals**, not string keys. `member_status_history` is never shown in the UI.
- **Judge tests by `ctest`.** `ctest --test-dir build --output-on-failure`, or `-R '^test_vlms_core$'` for Core, or `-R '^test_ui_'` for the UI. For a quick filtered Core run while iterating, use the same environment `ctest` sets:
  ```bash
  env VLMS_SCHEMA_PATH=$PWD/database/schema.sql VLMS_TEST_DATA_DIR=$PWD/libraries/Core/test/data \
      TZ=Africa/Tunis QT_QPA_PLATFORM=offscreen ./build/bin/test_vlms_core --gtest_filter='<Suite>.*'
  ```
  Running `./build/bin/test_vlms_core` bare fails 16 unrelated tests. Do not judge by it.
- UI tests are discovered per case (`test_ui_<Suite>.<Case>`), so `ctest --test-dir build -R 'test_ui_MemberEditorStatus'` runs one suite.
- **Parallel waves run in separate git worktrees**, one per task, under `.worktrees/` (already gitignored). Each has its own build directory, configured once:
  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DVLMS_BUILD_TESTS=ON -DVLMS_DEV_PATHS=ON \
        -DVLMS_TEST_REAL_DB=/home/amin/Dokumente/dev/VLMS/database/vlms.db.bak-20260814-232925
  cmake --build build -j
  ```
  Never build two tasks in the same `build/`. The controller merges each approved task branch back into `member-status-expiry` before the next wave starts.
- **Never open, copy or modify `database/vlms.db` or its backups** except in Task 9, which the controller runs with a fresh backup first. The database holds member PII: never paste its rows into a commit, a test or a chat.
- `scripts/import_members_from_xlsx.py` already has uncommitted changes that belong to the user. Task 8 edits the file but **does not commit**.
- Commit messages are short imperative sentences in the repo's style (`Refuse to …`, `Show …`) and end with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```

**Spec:** `docs/superpowers/specs/2026-09-23-member-status-expiry-design.md`

---

## Waves and dependencies

| Wave | Tasks (run in parallel within a wave) | Needs |
|---|---|---|
| 1 | Task 1 (two statuses and the rule) ∥ Task 2 (add `active_until`) | prerequisite merge |
| 2 | Task 3 (status is read from `active_until`) | 1, 2 |
| 3 | Task 4 (drop `status`) ∥ Task 5 (editor) ∥ Task 6 (Members page) ∥ Task 7 (Metrics tiles) ∥ Task 8 (import script) | 3 |
| 4 | Task 9 (close-out, controller only) | 4–8 |

Files are disjoint within a wave. The one shared file is `applications/vlms/test/CMakeLists.txt`, where Tasks 5 and 7 each insert one line into different hunks, so the merges stay clean.

Each task is gated by a reviewer before its branch merges. The reviewer checks it against the spec and against this plan's Global Constraints, then does a code-quality pass.

## File structure

| File | Responsibility | Task |
|---|---|---|
| `libraries/Core/include/VLMS/Core/Date.h`, `src/Date.cpp` | `Date::addYears` | 1 |
| `libraries/Core/include/VLMS/Core/MemberTypes.h` | two status codes; `MemberInput::status` defaults to `active` (1); `MemberRecord::activeUntil` (3) | 1, 3 |
| `libraries/Core/include/VLMS/Core/MemberRepository.h`, `src/MemberRepository.cpp` | `statusCodes`, `statusOn`, `activeUntilFor` (1); reads and writes `active_until`, history notes (3) | 1, 3 |
| `libraries/Core/src/Strings.cpp` | removes four keys, adds `member.field.activeUntil`, rewrites the Members help text | 1 |
| `libraries/Core/include/VLMS/Core/Database.h`, `src/Database.cpp`, `database/schema.sql` | v7: add the column (2), drop `status` (4) | 2, 4 |
| `libraries/Core/src/MemberSql.h`, `.cpp` | `isActive`, `statusExpression`; status filter and sort | 3 |
| `libraries/Core/src/CirculationRepository.cpp` | borrow check, borrowable list, loan-row member status | 3 |
| `libraries/Core/src/MetricsRepository.cpp`, `include/.../MetricsTypes.h` | Active / Not active counts (3); the two old fields go (7) | 3, 7 |
| `applications/vlms/src/ui/members/MemberEditorDialog.h`, `.cpp` | two-entry combo, Active until preview | 1 (one line), 5 |
| `applications/vlms/src/ui/members/MembersPage.cpp` | Active until detail row | 6 |
| `applications/vlms/src/ui/metrics/MetricsPage.cpp` | three member tiles | 7 |
| `scripts/import_members_from_xlsx.py` | writes `active_until` | 8 |
| `libraries/Core/test/src/test_member_status_rule.cpp` | **new**: the pure rule | 1 |
| `libraries/Core/test/data/schema_v6.sql` | **new**: v6 fixture | 2 |
| `libraries/Core/test/src/test_member_status_expiry.cpp` | **new**: the rule through the repositories | 3 |
| `applications/vlms/test/src/test_member_editor_status.cpp` | **new** | 5 |
| `applications/vlms/test/src/test_metrics_member_tiles.cpp` | **new** | 7 |

---

### Task 1: Two statuses and the one-year rule

**Wave 1, runs in parallel with Task 2.** Core logic and strings only. The database still stores `status`, so every existing path keeps working.

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/Date.h`, `libraries/Core/src/Date.cpp`
- Modify: `libraries/Core/include/VLMS/Core/MemberTypes.h`
- Modify: `libraries/Core/include/VLMS/Core/MemberRepository.h`, `libraries/Core/src/MemberRepository.cpp`
- Modify: `libraries/Core/src/Strings.cpp`
- Modify: `applications/vlms/src/ui/members/MemberEditorDialog.cpp` (one line, so it compiles)
- Create: `libraries/Core/test/src/test_member_status_rule.cpp`
- Modify: `libraries/Core/test/CMakeLists.txt`
- Modify: `libraries/Core/test/src/test_member_repository.cpp`, `test_metrics_repository.cpp`, `test_circulation_repository.cpp`
- Modify: `applications/vlms/test/src/test_dialog_translations.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `VLMS::Date VLMS::Date::addYears(int years) const;` An invalid date stays invalid. 29 February rolls to 1 March.
  - `static std::string MemberRepository::statusOn(const std::string& activeUntil, const VLMS::Date& today);` Returns `MemberStatus::kActive` when `activeUntil` is a valid ISO date `>= today`, otherwise `kNonActive`.
  - `static std::string MemberRepository::activeUntilFor(const std::string& currentActiveUntil, const std::string& chosenStatus, const VLMS::Date& today);` Returns the ISO date to store. An empty `currentActiveUntil` means a new member.
  - `MemberRepository::statusCodes()` returns exactly `{"active", "non_active"}`.
  - The string key `member.field.activeUntil` exists in all three tables.

- [ ] **Step 1: Write the failing rule tests**

Create `libraries/Core/test/src/test_member_status_rule.cpp`:

```cpp
#include <VLMS/Core/Date.h>
#include <VLMS/Core/MemberRepository.h>
#include <VLMS/Core/MemberTypes.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using VLMS::Date;

/**
 * The one-year rule on its own, with no database: which day a member's
 * year ends, and what a librarian's pick in the editor does to it.
 */
class test_core_MemberStatusRule : public ::testing::Test {
protected:
    const Date m_today{2026, 9, 23};
};

TEST_F(test_core_MemberStatusRule, StatusCodesAreActiveAndNotActiveOnly)
{
    const std::vector<std::string> expected = {MemberStatus::kActive, MemberStatus::kNonActive};
    EXPECT_EQ(MemberRepository::statusCodes(), expected);
}

TEST_F(test_core_MemberStatusRule, AddYearsKeepsTheDayOfTheMonth)
{
    EXPECT_EQ(Date(2025, 9, 24).addYears(1), Date(2026, 9, 24));
}

TEST_F(test_core_MemberStatusRule, AddYearsRollsALeapDayToTheFirstOfMarch)
{
    // SQLite's date('2024-02-29', '+1 year') is 2025-03-01; the migration
    // uses SQLite and the repository uses Date, so they must agree.
    EXPECT_EQ(Date(2024, 2, 29).addYears(1), Date(2025, 3, 1));
}

TEST_F(test_core_MemberStatusRule, AddYearsOnAnInvalidDateStaysInvalid)
{
    EXPECT_FALSE(Date().addYears(1).isValid());
}

TEST_F(test_core_MemberStatusRule, ActiveOnTheLastDayNotActiveTheDayAfter)
{
    EXPECT_EQ(MemberRepository::statusOn("2026-09-23", m_today), MemberStatus::kActive);
    EXPECT_EQ(MemberRepository::statusOn("2026-09-22", m_today), MemberStatus::kNonActive);
}

TEST_F(test_core_MemberStatusRule, NoDateOrAnUnreadableDateIsNotActive)
{
    EXPECT_EQ(MemberRepository::statusOn("", m_today), MemberStatus::kNonActive);
    EXPECT_EQ(MemberRepository::statusOn("next year", m_today), MemberStatus::kNonActive);
}

TEST_F(test_core_MemberStatusRule, ANewMemberIsActiveForAYearLessADay)
{
    EXPECT_EQ(MemberRepository::activeUntilFor("", MemberStatus::kActive, m_today), "2027-09-22");
}

TEST_F(test_core_MemberStatusRule, ANewMemberRegisteredOnALeapDayEndsOnTheLastDayOfFebruary)
{
    EXPECT_EQ(MemberRepository::activeUntilFor("", MemberStatus::kActive, Date(2024, 2, 29)),
              "2025-02-28");
}

TEST_F(test_core_MemberStatusRule, ANewMemberSavedNotActiveEndedYesterday)
{
    EXPECT_EQ(MemberRepository::activeUntilFor("", MemberStatus::kNonActive, m_today),
              "2026-09-22");
}

TEST_F(test_core_MemberStatusRule, RenewingAnExpiredMemberGivesAYearFromToday)
{
    EXPECT_EQ(MemberRepository::activeUntilFor("2026-01-09", MemberStatus::kActive, m_today),
              "2027-09-22");
}

TEST_F(test_core_MemberStatusRule, EndingAMembershipEarlySetsYesterday)
{
    EXPECT_EQ(MemberRepository::activeUntilFor("2027-01-01", MemberStatus::kNonActive, m_today),
              "2026-09-22");
}

TEST_F(test_core_MemberStatusRule, KeepingTheStatusKeepsTheDate)
{
    EXPECT_EQ(MemberRepository::activeUntilFor("2027-01-01", MemberStatus::kActive, m_today),
              "2027-01-01");
    EXPECT_EQ(MemberRepository::activeUntilFor("2026-01-09", MemberStatus::kNonActive, m_today),
              "2026-01-09");
}

TEST_F(test_core_MemberStatusRule, AnEmptyChoiceMeansActive)
{
    EXPECT_EQ(MemberRepository::activeUntilFor("", "", m_today), "2027-09-22");
}
```

Add `src/test_member_status_rule.cpp` to `TST_SOURCES` in `libraries/Core/test/CMakeLists.txt`, directly after `src/test_member_repository.cpp`.

- [ ] **Step 2: Run the tests and confirm they fail to compile**

Run: `cmake --build build -j 2>&1 | grep -E "error" | head`
Expected: errors naming `addYears`, `statusOn` and `activeUntilFor`.

- [ ] **Step 3: Add `Date::addYears`**

In `Date.h`, below `addDays`:

```cpp
    /// SQLite's '+N years': the same month and day, except 29 February in a
    /// year without one, which rolls to 1 March.
    [[nodiscard]] Date addYears(int years) const;
```

In `Date.cpp`, after `Date::addDays`:

```cpp
Date Date::addYears(const int years) const
{
    if (!m_valid) {
        return {};
    }
    const Date same(m_year + years, m_month, m_day);
    if (same.isValid()) {
        return same;
    }
    return Date(m_year + years, 3, 1);
}
```

- [ ] **Step 4: Keep two status codes**

In `MemberTypes.h`, replace the `MemberStatus` namespace:

```cpp
namespace MemberStatus {
inline constexpr auto kActive = "active";
inline constexpr auto kNonActive = "non_active";
}  // namespace MemberStatus
```

In `MemberInput`, change `std::string status = "subscribed";` to `std::string status = "active";`.

In `MemberRepository.cpp`, make `statusCodes()` return `{MemberStatus::kActive, MemberStatus::kNonActive}`. In `validateInput` and `insertMemberRow`, replace both `status = MemberStatus::kSubscribed;` with `status = MemberStatus::kActive;`.

- [ ] **Step 5: Add the rule**

In `MemberRepository.h`, add `#include <VLMS/Core/Date.h>` and, beside `ageGroupFromBirthDate`:

```cpp
    /// Active while activeUntil (the member's last active day) is today or
    /// later. Empty or unreadable is not active.
    [[nodiscard]] static std::string statusOn(const std::string& activeUntil,
                                              const VLMS::Date& today);
    /// The last active day to store once a librarian has chosen chosenStatus.
    /// Unchanged status keeps the date; Active starts a year today (a new
    /// member, or a renewal); Not active ends yesterday. An empty
    /// currentActiveUntil is a member not yet saved.
    [[nodiscard]] static std::string activeUntilFor(const std::string& currentActiveUntil,
                                                    const std::string& chosenStatus,
                                                    const VLMS::Date& today);
```

In `MemberRepository.cpp`, after `ageGroupFromBirthDate`:

```cpp
std::string MemberRepository::statusOn(const std::string& activeUntil, const Date& today)
{
    const Date lastDay = Date::fromIso(trim(activeUntil));
    return lastDay.isValid() && lastDay >= today ? MemberStatus::kActive : MemberStatus::kNonActive;
}

std::string MemberRepository::activeUntilFor(const std::string& currentActiveUntil,
                                             const std::string& chosenStatus,
                                             const Date& today)
{
    const std::string current = trim(currentActiveUntil);
    const std::string chosen =
        trim(chosenStatus).empty() ? std::string(MemberStatus::kActive) : trim(chosenStatus);
    if (!current.empty() && statusOn(current, today) == chosen) {
        return current;
    }
    if (chosen == MemberStatus::kActive) {
        return today.addYears(1).addDays(-1).toIso();
    }
    return today.addDays(-1).toIso();
}
```

- [ ] **Step 6: Fix the compile sites that named the old codes**

`MemberEditorDialog.cpp` `populateStatuses()`: `QString::fromLatin1(MemberStatus::kSubscribed)` becomes `QString::fromLatin1(MemberStatus::kActive)`.

`test_member_repository.cpp` `StatusCodesAndSexCodesAreNonEmpty`: replace the status half with

```cpp
    const std::vector<std::string> statuses = MemberRepository::statusCodes();
    EXPECT_EQ(statuses.size(), 2u);
    EXPECT_TRUE(contains(statuses, MemberStatus::kActive));
    EXPECT_TRUE(contains(statuses, MemberStatus::kNonActive));
```

`test_metrics_repository.cpp` `MemberStatusCountsSumToTotalMembers`: set `statuses` to `{kActive, kActive, kNonActive}` and assert `metrics.membersActive + metrics.membersNonActive == metrics.totalMembers` and `metrics.membersActive == 2`. Do not name `membersSubscribed` / `membersUnsubscribed`, which Task 7 removes.

`test_circulation_repository.cpp`: in the case table near line 105, drop the `subscribed` and `unsubscribed` rows and keep `non_active`. In `ListBorrowableMembersOnlyReturnsActive`, loop over `{MemberStatus::kNonActive}` only.

`test_dialog_translations.cpp` `validMember()`: `member.status = "subscribed";` becomes `member.status = "active";`.

Then check nothing else names the old codes:
Run: `grep -rn "kSubscribed\|kUnsubscribed\|\"subscribed\"\|\"unsubscribed\"" libraries applications --include=*.cpp --include=*.h`
Expected: no output.

- [ ] **Step 7: Update the string tables**

In `libraries/Core/src/Strings.cpp`, in each of the three tables:
- delete `member.status.subscribed`, `member.status.unsubscribed`, `metrics.membersSubscribed` and `metrics.membersUnsubscribed`;
- add beside `member.field.status`: Arabic `{"member.field.activeUntil", "نشط حتى"}`, French `{"member.field.activeUntil", "Actif jusqu'au"}`, English `{"member.field.activeUntil", "Active until"}`;
- rewrite the status clause in `page.members.body`: Arabic `تغيير الحالة (نشط، غير نشط)`, French `changer le statut (actif, inactif)`, English `change status (active, non active)`. Keep the rest of each sentence as it is.

`MetricsPage` keeps showing the two old tiles until Task 7, with their raw keys as labels. That is expected between the waves.

- [ ] **Step 8: Build and run the Core and UI suites**

Run: `cmake --build build -j && ctest --test-dir build -R '^test_vlms_core$|^test_ui_' --output-on-failure`
Expected: all pass. The new suite `test_core_MemberStatusRule` runs inside `test_vlms_core`, so confirm it with the filtered command from Global Constraints and `--gtest_filter='test_core_MemberStatusRule.*'` (13 tests, all passing).

- [ ] **Step 9: Commit**

```bash
git add libraries/Core applications/vlms/src/ui/members/MemberEditorDialog.cpp \
        applications/vlms/test/src/test_dialog_translations.cpp
git commit -m "Keep two member statuses and the one-year rule.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Add `members.active_until` (schema v7, step 1 of 2)

**Wave 1, runs in parallel with Task 1.** This task adds and fills the column and leaves `status` in place. Nothing reads the new column yet.

**Files:**
- Create: `libraries/Core/test/data/schema_v6.sql`
- Modify: `database/schema.sql`
- Modify: `libraries/Core/include/VLMS/Core/Database.h`
- Modify: `libraries/Core/src/Database.cpp`
- Modify: `libraries/Core/test/src/test_database_migrations.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: the column `members.active_until TEXT CHECK (date(active_until) IS active_until)`, last in the table, filled with `date(registered_at, '+1 year', '-1 day')` for every existing row; `Database::kSchemaVersion == 7`; `bool Database::migrateMemberActiveUntilIfNeeded();` (Task 4 extends it).

- [ ] **Step 1: Snapshot the v6 schema as a fixture**

Run this **before** touching `database/schema.sql`:

```bash
git show HEAD:database/schema.sql > libraries/Core/test/data/schema_v6.sql
cat >> libraries/Core/test/data/schema_v6.sql <<'EOF'

-- v6 rows for the v7 migration: one registered exactly a year before
-- 2026-09-24, one a day earlier, one on a leap day.
INSERT INTO members (id, membership_number, first_name, last_name, status, registered_at)
VALUES (1, '1', 'Amina', 'Ben Salah', 'active', '2025-09-24 10:00:00'),
       (2, '2', 'Karim', 'Trabelsi', 'active', '2025-09-23 09:00:00'),
       (3, '3', 'Leila', 'Haddad', 'non_active', '2024-02-29 12:00:00');
INSERT INTO member_status_history (member_id, old_status, new_status)
VALUES (1, NULL, 'active'), (2, NULL, 'active'), (3, NULL, 'non_active');
EOF
tail -14 libraries/Core/test/data/schema_v6.sql
```

Expected: the output shows `PRAGMA user_version = 6;` followed by the inserts.

- [ ] **Step 2: Write the failing migration tests**

In `test_database_migrations.cpp`, change `VersionFiveGainsArchiveColumnsAndNullableCopyNumbers` to expect `Database::kSchemaVersion` instead of `6`, because a v5 database now runs through to the latest version. Then add at the end of the file:

```cpp
// ---------------------------------------------------------------------------
// v7: members.active_until
// ---------------------------------------------------------------------------

TEST_F(test_core_DatabaseMigrations, VersionSixGainsActiveUntilOneYearAfterRegistration)
{
    const auto db = openFixture("schema_v6.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    EXPECT_EQ(db->userVersion(), Database::kSchemaVersion);
    EXPECT_EQ(db->scalar("SELECT active_until FROM members WHERE id = 1").toString(), "2026-09-23");
    EXPECT_EQ(db->scalar("SELECT active_until FROM members WHERE id = 2").toString(), "2026-09-22");
    EXPECT_EQ(db->scalar("SELECT active_until FROM members WHERE id = 3").toString(), "2025-02-28");
}

TEST_F(test_core_DatabaseMigrations, VersionSevenIgnoresTheStoredStatus)
{
    const auto db = openFixture("schema_v6.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    // Member 2 is stored 'active' but registered 2025-09-23: the year is the
    // rule, so the date is what registration gives, whatever status said.
    EXPECT_EQ(db->scalar("SELECT active_until FROM members WHERE id = 2").toString(), "2026-09-22");
}

TEST_F(test_core_DatabaseMigrations, VersionSevenMigrationKeepsStatusHistory)
{
    const auto db = openFixture("schema_v6.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    EXPECT_EQ(db->count("member_status_history"), 3);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM pragma_foreign_key_check").toInt(), 0);
    EXPECT_EQ(db->scalar("PRAGMA foreign_keys").toInt(), 1);
}

TEST_F(test_core_DatabaseMigrations, VersionSevenMigrationRunsOnce)
{
    const auto db = openFixture("schema_v6.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    // A renewal after the upgrade must survive the next launch.
    ASSERT_TRUE(db->exec("UPDATE members SET active_until = '2030-01-01' WHERE id = 1"));
    ASSERT_TRUE(db->database().open()) << "second open() failed";
    EXPECT_EQ(db->scalar("SELECT active_until FROM members WHERE id = 1").toString(), "2030-01-01");
}

TEST_F(test_core_DatabaseMigrations, VersionSevenColumnOrderMatchesAFreshDatabase)
{
    const auto migrated = openFixture("schema_v6.sql");
    ASSERT_NE(migrated, nullptr);
    ASSERT_TRUE(migrated->isValid()) << migrated->lastError();
    TestDatabase fresh;
    ASSERT_TRUE(fresh.isValid()) << fresh.lastError();

    const std::string columns = "SELECT group_concat(name, ',') FROM "
                                "(SELECT name FROM pragma_table_info('members') ORDER BY cid)";
    EXPECT_EQ(migrated->scalar(columns).toString(), fresh.scalar(columns).toString());
}

TEST_F(test_core_DatabaseMigrations, ActiveUntilRefusesADayThatDoesNotExist)
{
    TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();

    EXPECT_FALSE(db.exec("INSERT INTO members (membership_number, first_name, last_name, "
                         "active_until) VALUES ('90', 'A', 'B', '2026-02-30')"));
    EXPECT_TRUE(db.exec("INSERT INTO members (membership_number, first_name, last_name, "
                        "active_until) VALUES ('91', 'A', 'B', '2026-02-28')"));
}
```

- [ ] **Step 3: Run the tests and confirm they fail**

Run: `cmake --build build -j && env VLMS_SCHEMA_PATH=$PWD/database/schema.sql VLMS_TEST_DATA_DIR=$PWD/libraries/Core/test/data TZ=Africa/Tunis ./build/bin/test_vlms_core --gtest_filter='test_core_DatabaseMigrations.*'`
Expected: the six new tests fail (`no such column: active_until`, or version 6 against 7 expected once the constant moves).

- [ ] **Step 4: Declare the column in `database/schema.sql`**

In `CREATE TABLE IF NOT EXISTS members`, change `source_row INTEGER` to `source_row INTEGER,` and add after it:

```sql
    -- The member's last active day: one year after registering, less a day,
    -- or a year from the day a librarian renewed them. Status is read from it
    -- and never stored. Last for the same reason as email: a v6 database
    -- gains it from ALTER TABLE, which can only append.
    active_until TEXT CHECK (date(active_until) IS active_until)
```

Change the closing `PRAGMA user_version = 6;` to `PRAGMA user_version = 7;`.

- [ ] **Step 5: Add the migration**

In `Database.h`, change `kSchemaVersion = 6` to `kSchemaVersion = 7` and declare `bool migrateMemberActiveUntilIfNeeded();` beside `migrateArchiveColumnsIfNeeded()`.

In `Database.cpp`, after `migrateArchiveColumnsIfNeeded()`:

```cpp
bool Database::migrateMemberActiveUntilIfNeeded()
{
    if (tableHasColumn("members", "active_until")) {
        return true;
    }
    const Status work = m_session->transaction([&] {
        if (const Status added = m_session->exec(
                "ALTER TABLE members ADD COLUMN active_until TEXT "
                "CHECK (date(active_until) IS active_until)");
            !added) {
            return added;
        }
        // A year from registering, less a day: registered 2025-09-24, active
        // through 2026-09-23. The stored status is deliberately not read --
        // the year is the rule, and a status nobody expired is not evidence.
        return m_session->exec(
            "UPDATE members SET active_until = date(registered_at, '+1 year', '-1 day')");
    });
    if (!work) {
        warn("Member active_until migration failed: " + work.error().detail);
        return false;
    }
    return true;
}
```

Wire it in twice:
- in `migrateLegacyShapesIfNeeded()`, directly after the `migrateArchiveColumnsIfNeeded()` block:
  ```cpp
      if (!migrateMemberActiveUntilIfNeeded()) {
          return false;
      }
  ```
- in `upgradeSchemaIfNeeded()`, after the `version < 6` line:
  ```cpp
      if (version < 7 && (!migrateMemberActiveUntilIfNeeded() || !setSchemaVersion(7))) {
          return false;
      }
  ```

- [ ] **Step 6: Give the legacy constraint rebuild the same column**

`ConstraintMigrationMatchesWhatSchemaSqlDeclares` compares the `CHECK` lines of a legacy-rebuilt `members` with a fresh one. An `ALTER` would append `, active_until …` to the last line, and that line would not match. So `rebuildTablesWithDateConstraints()` has to create the column itself. In `membersRebuild`:
- change `source_row INTEGER` to `source_row INTEGER,` and add the line `active_until TEXT CHECK (date(active_until) IS active_until)` after it;
- in the `INSERT INTO members_new (...)` column list, append `, active_until`, and in its `SELECT`, append `, date(registered_at, '+1 year', '-1 day')` after the four `NULL`s.

`migrateMemberActiveUntilIfNeeded()` then finds the column and skips.

- [ ] **Step 7: Run the whole Core suite**

Run: `cmake --build build -j && ctest --test-dir build -R '^test_vlms_core' --output-on-failure`
Expected: all pass, including `test_vlms_core_realdb` (the 2026-08-14 backup migrates to v7) and both timezone entries.

- [ ] **Step 8: Commit**

```bash
git add database/schema.sql libraries/Core/include/VLMS/Core/Database.h libraries/Core/src/Database.cpp \
        libraries/Core/test/data/schema_v6.sql libraries/Core/test/src/test_database_migrations.cpp
git commit -m "Give every member a last active day.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Status is read from `active_until`

**Wave 2, serial.** After this task nothing reads or writes `members.status`. The column is still there, and its default fills it on insert, harmlessly.

**Files:**
- Modify: `libraries/Core/src/MemberSql.h`, `libraries/Core/src/MemberSql.cpp`
- Modify: `libraries/Core/include/VLMS/Core/MemberTypes.h`
- Modify: `libraries/Core/src/MemberRepository.cpp`
- Modify: `libraries/Core/src/CirculationRepository.cpp`
- Modify: `libraries/Core/src/MetricsRepository.cpp`
- Create: `libraries/Core/test/src/test_member_status_expiry.cpp`
- Modify: `libraries/Core/test/CMakeLists.txt`

**Interfaces:**
- Consumes (Task 1): `MemberRepository::statusOn`, `MemberRepository::activeUntilFor`, `Date::addYears`. From Task 2: the `members.active_until` column.
- Produces:
  - `std::string VLMS::MemberSql::isActive(std::string_view alias);` An SQL boolean that is never NULL and uses `:today`. `alias` is `"m."` or `""`.
  - `std::string VLMS::MemberSql::statusExpression(std::string_view alias);` An SQL expression that yields `'active'` or `'non_active'` and uses `:today`.
  - `MemberRecord::activeUntil` (`std::string`, ISO date or empty). `MemberRecord::status` is now derived when the record is read.
  - `LoanRecord::memberStatus` and `LoanMemberOption::status` are derived the same way.

- [ ] **Step 1: Write the failing behaviour tests**

Create `libraries/Core/test/src/test_member_status_expiry.cpp`:

```cpp
#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Core/MemberRepository.h>
#include <VLMS/Core/MetricsRepository.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using VLMS::Date;
using VLMS::ScopedClock;
using namespace VLMS::Test;

/**
 * The one-year rule through the repositories: what a member reads as,
 * what a librarian's save does to the date, and who may borrow.
 */
class test_core_MemberStatusExpiry : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());
        m_metrics = std::make_unique<MetricsRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_metrics.reset();
        m_circulation.reset();
        m_members.reset();
        m_db.reset();
    }

    /// A member registered on `day`, saved Active.
    std::int64_t registerOn(const Date& day, const int index)
    {
        const ScopedClock pinned(day);
        return seedMember(*m_db, uniqueMemberSeed(index));
    }

    std::string lastHistoryNote(const std::int64_t memberId) const
    {
        return m_db->scalar("SELECT note FROM member_status_history WHERE member_id = :id "
                            "ORDER BY id DESC LIMIT 1",
                            {{"id", memberId}})
            .toString();
    }

    int historyCount(const std::int64_t memberId) const
    {
        return m_db->scalar("SELECT COUNT(*) FROM member_status_history WHERE member_id = :id",
                            {{"id", memberId}})
            .toInt();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MemberRepository> m_members;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::unique_ptr<MetricsRepository> m_metrics;
};

TEST_F(test_core_MemberStatusExpiry, RegisteringStartsAYearLessADay)
{
    const std::int64_t id = registerOn(Date(2026, 9, 23), 1);
    ASSERT_GT(id, 0);

    const ScopedClock pinned(Date(2026, 9, 23));
    const MemberRecord member = VLMS_UNWRAP(m_members->getMember(id));
    EXPECT_EQ(member.activeUntil, "2027-09-22");
    EXPECT_EQ(member.status, MemberStatus::kActive);
    EXPECT_EQ(lastHistoryNote(id), "Registered, active until 2027-09-22");
}

TEST_F(test_core_MemberStatusExpiry, ActiveOnTheLastDayNotActiveTheDayAfter)
{
    const std::int64_t id = registerOn(Date(2025, 9, 24), 1);
    ASSERT_GT(id, 0);

    MemberQuery active;
    active.statuses = {MemberStatus::kActive};
    MemberQuery notActive;
    notActive.statuses = {MemberStatus::kNonActive};

    {
        const ScopedClock pinned(Date(2026, 9, 23));
        EXPECT_EQ(VLMS_UNWRAP(m_members->getMember(id)).status, MemberStatus::kActive);
        EXPECT_EQ(VLMS_UNWRAP(m_members->countMembers(active)), 1);
        EXPECT_EQ(VLMS_UNWRAP(m_members->countMembers(notActive)), 0);
    }
    {
        const ScopedClock pinned(Date(2026, 9, 24));
        EXPECT_EQ(VLMS_UNWRAP(m_members->getMember(id)).status, MemberStatus::kNonActive);
        EXPECT_EQ(VLMS_UNWRAP(m_members->countMembers(active)), 0);
        EXPECT_EQ(VLMS_UNWRAP(m_members->countMembers(notActive)), 1);
        EXPECT_EQ(VLMS_UNWRAP(m_members->listMembers(notActive)).size(), 1u);
    }
}

TEST_F(test_core_MemberStatusExpiry, RenewingAnExpiredMemberGivesAnotherYear)
{
    const std::int64_t id = registerOn(Date(2025, 1, 10), 1);
    ASSERT_GT(id, 0);

    const ScopedClock pinned(Date(2026, 9, 23));
    ASSERT_EQ(VLMS_UNWRAP(m_members->getMember(id)).status, MemberStatus::kNonActive);

    MemberInput input = uniqueMemberSeed(1).toInput();
    input.status = MemberStatus::kActive;
    ASSERT_TRUE(m_members->updateMember(id, input));

    const MemberRecord member = VLMS_UNWRAP(m_members->getMember(id));
    EXPECT_EQ(member.activeUntil, "2027-09-22");
    EXPECT_EQ(member.status, MemberStatus::kActive);
    EXPECT_EQ(lastHistoryNote(id), "Renewed until 2027-09-22");
    EXPECT_EQ(m_db->scalar("SELECT old_status FROM member_status_history WHERE member_id = :id "
                           "ORDER BY id DESC LIMIT 1",
                           {{"id", id}})
                  .toString(),
              MemberStatus::kNonActive);
}

TEST_F(test_core_MemberStatusExpiry, SettingNotActiveEndsTheMembershipYesterday)
{
    const std::int64_t id = registerOn(Date(2026, 9, 1), 1);
    ASSERT_GT(id, 0);

    const ScopedClock pinned(Date(2026, 9, 23));
    MemberInput input = uniqueMemberSeed(1).toInput();
    input.status = MemberStatus::kNonActive;
    ASSERT_TRUE(m_members->updateMember(id, input));

    const MemberRecord member = VLMS_UNWRAP(m_members->getMember(id));
    EXPECT_EQ(member.activeUntil, "2026-09-22");
    EXPECT_EQ(member.status, MemberStatus::kNonActive);
    EXPECT_EQ(lastHistoryNote(id), "Ended early");
}

TEST_F(test_core_MemberStatusExpiry, SavingWithTheSameStatusKeepsTheDate)
{
    const std::int64_t id = registerOn(Date(2026, 9, 1), 1);
    ASSERT_GT(id, 0);
    const int historyBefore = historyCount(id);

    const ScopedClock pinned(Date(2026, 9, 23));
    MemberInput input = uniqueMemberSeed(1).toInput();
    input.phone = "71 000 000";
    input.status = MemberStatus::kActive;
    ASSERT_TRUE(m_members->updateMember(id, input));

    EXPECT_EQ(VLMS_UNWRAP(m_members->getMember(id)).activeUntil, "2027-08-31");
    EXPECT_EQ(historyCount(id), historyBefore);
}

TEST_F(test_core_MemberStatusExpiry, BorrowingIsRefusedTheDayAfterExpiry)
{
    const std::int64_t memberId = registerOn(Date(2025, 9, 24), 1);
    ASSERT_GT(memberId, 0);
    BookSeed book = uniqueBookSeed(1);
    book.initialCopyCount = 2;
    const auto copies = copyIdsOf(*m_db, seedBook(*m_db, book));
    ASSERT_EQ(copies.size(), 2u);

    LoanInput loan;
    loan.memberId = memberId;
    {
        const ScopedClock pinned(Date(2026, 9, 23));
        loan.bookCopyId = copies.at(0);
        EXPECT_TRUE(m_circulation->createLoan(loan)) << "the last active day still lends";
    }
    {
        const ScopedClock pinned(Date(2026, 9, 24));
        loan.bookCopyId = copies.at(1);
        const auto refused = m_circulation->createLoan(loan);
        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().key, "error.loan.memberInactive");
    }
}

TEST_F(test_core_MemberStatusExpiry, TheBorrowableListDropsTheExpiredMember)
{
    const std::int64_t expired = registerOn(Date(2025, 1, 10), 1);
    const std::int64_t current = registerOn(Date(2026, 9, 1), 2);
    ASSERT_GT(expired, 0);
    ASSERT_GT(current, 0);

    const ScopedClock pinned(Date(2026, 9, 23));
    const auto borrowable = VLMS_UNWRAP(m_circulation->listBorrowableMembers());
    ASSERT_EQ(borrowable.size(), 1u);
    EXPECT_EQ(borrowable.front().id, current);
    EXPECT_EQ(borrowable.front().status, MemberStatus::kActive);
}

TEST_F(test_core_MemberStatusExpiry, LoanRowsCarryTheMembersStatusOnTheDay)
{
    const std::int64_t memberId = registerOn(Date(2025, 9, 24), 1);
    const auto copies = copyIdsOf(*m_db, seedBook(*m_db, uniqueBookSeed(1)));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copies.at(0), "2026-09-20", "2026-10-04"), 0);

    LoanQuery all;
    all.filters = {LoanFilter::kAll};
    {
        const ScopedClock pinned(Date(2026, 9, 23));
        EXPECT_EQ(VLMS_UNWRAP(m_circulation->listLoans(all)).front().memberStatus,
                  MemberStatus::kActive);
    }
    {
        const ScopedClock pinned(Date(2026, 9, 24));
        EXPECT_EQ(VLMS_UNWRAP(m_circulation->listLoans(all)).front().memberStatus,
                  MemberStatus::kNonActive);
    }
}

TEST_F(test_core_MemberStatusExpiry, MetricsCountFromTheDate)
{
    ASSERT_GT(registerOn(Date(2025, 1, 10), 1), 0);
    ASSERT_GT(registerOn(Date(2026, 9, 1), 2), 0);
    ASSERT_GT(registerOn(Date(2026, 9, 2), 3), 0);

    const ScopedClock pinned(Date(2026, 9, 23));
    const LibraryMetrics metrics = VLMS_UNWRAP(m_metrics->fetchMetrics());
    EXPECT_EQ(metrics.membersActive, 2);
    EXPECT_EQ(metrics.membersNonActive, 1);
}

TEST_F(test_core_MemberStatusExpiry, SortingByStatusPutsTheEarliestLastDayFirst)
{
    const std::int64_t later = registerOn(Date(2026, 9, 1), 1);
    const std::int64_t earlier = registerOn(Date(2025, 1, 10), 2);

    const ScopedClock pinned(Date(2026, 9, 23));
    MemberQuery query;
    query.sortColumn = MemberSort::kStatus;
    query.sortAscending = true;
    const auto members = VLMS_UNWRAP(m_members->listMembers(query));
    ASSERT_EQ(members.size(), 2u);
    EXPECT_EQ(members.at(0).id, earlier);
    EXPECT_EQ(members.at(1).id, later);
}
```

Add `src/test_member_status_expiry.cpp` to `TST_SOURCES` after `src/test_member_status_rule.cpp`.

Before writing Step 3, check two assumptions the tests make:
Run: `grep -n "struct LoanQuery" -A6 libraries/Core/include/VLMS/Core/LoanTypes.h; grep -n "kAll" libraries/Core/include/VLMS/Core/LoanTypes.h; grep -n "listBorrowableMembers" libraries/Core/include/VLMS/Core/CirculationRepository.h`
Expected: `LoanQuery::filters` exists, `LoanFilter::kAll` exists, and `listBorrowableMembers` has a default `search` argument. If any name differs, adjust the test to the real name. Keep the assertion the same.

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `cmake --build build -j 2>&1 | grep -E "error" | head -5`
Expected: `MemberRecord` has no member `activeUntil`.

- [ ] **Step 3: Add the SQL expressions**

In `MemberSql.h`, add `#include <string_view>` and:

```cpp
/// True while a member's year is running: active_until is today or later.
/// Never NULL, so NOT works on it. Uses :today -- bind it with
/// LoanSql::bindTodayIfPresent. `alias` is "m." or "".
[[nodiscard]] std::string isActive(std::string_view alias);
/// 'active' or 'non_active', worked out from active_until. Status is not
/// stored anywhere. Uses :today.
[[nodiscard]] std::string statusExpression(std::string_view alias);
```

In `MemberSql.cpp`, add `#include "LoanSql.h"` and, above `filterClause`:

```cpp
std::string isActive(const std::string_view alias)
{
    const std::string column = std::string(alias) + "active_until";
    return "(" + column + " IS NOT NULL AND " + column + " >= " + LoanSql::todayPlaceholder() + ")";
}

std::string statusExpression(const std::string_view alias)
{
    return "(CASE WHEN " + isActive(alias) + " THEN '" + std::string(MemberStatus::kActive)
        + "' ELSE '" + std::string(MemberStatus::kNonActive) + "' END)";
}
```

In `filterClause`, `appendInClause(sql, "m.status", "status_", query.statuses);` becomes `appendInClause(sql, statusExpression("m."), "status_", query.statuses);`.

In `orderExpressions`, the `MemberSort::kStatus` branch becomes:

```cpp
    if (column == MemberSort::kStatus) {
        // The last active day, not the label: ascending puts the longest
        // expired first and the newest registration last.
        return withDirection("COALESCE(m.active_until, '')", asc) + ", " + withDirection("m.id", asc);
    }
```

- [ ] **Step 4: Read and write `active_until` in `MemberRepository`**

In `MemberTypes.h` `MemberRecord`, add after `archivedAt`:

```cpp
    /// Last active day, ISO. `status` is worked out from it on every read.
    std::string activeUntil;
```

In `MemberRepository.cpp`:

1. Add `#include "LoanSql.h"` and `namespace LoanSql = VLMS::LoanSql;`.
2. Replace `const char* kMemberSelect = R"SQL(...)SQL";` with a function. The column list is the merged one (with `loan_count`). `m.status` becomes the expression, and `active_until` is appended last:

```cpp
std::string memberSelectSql()
{
    return std::string(R"SQL(
        SELECT
            m.id,
            m.membership_number,
            m.first_name,
            m.last_name,
            COALESCE(m.sex, '') AS sex,
            COALESCE(m.date_of_birth, '') AS date_of_birth,
            COALESCE(m.email, '') AS email,
            COALESCE(m.phone, '') AS phone,
            COALESCE(m.address, '') AS address,
            COALESCE(m.city, '') AS city,
            )SQL")
        + MemberSql::statusExpression("m.") + R"SQL( AS status,
            COALESCE(m.photo_path, '') AS photo_path,
            COALESCE(m.id_image_path, '') AS id_image_path,
            COALESCE(m.notes, '') AS notes,
            m.registered_at,
            m.updated_at,
            COALESCE(m.occupation, '') AS occupation,
            COALESCE(m.age_group, '') AS age_group,
            COALESCE(m.full_name, '') AS full_name,
            (
                SELECT COUNT(*)
                FROM loans l
                WHERE l.member_id = m.id
                  AND l.returned_at IS NULL
            ) AS active_loan_count,
            (
                SELECT COUNT(*)
                FROM loans l
                WHERE l.member_id = m.id
            ) AS loan_count,
            COALESCE(m.archived_at, '') AS archived_at,
            COALESCE(m.active_until, '') AS active_until
        FROM members m
)SQL";
}
```

   In `readMemberRow`, add `member.activeUntil = query.text(22);` after the `archivedAt` line.
3. In `listMembers`, `std::string(kMemberSelect)` becomes `memberSelectSql()`, and right after `MemberSql::bindFilters(*q, query);` add `LoanSql::bindTodayIfPresent(*q, sql);`.
4. In `rankOfMember` and `countMembers`, add `LoanSql::bindTodayIfPresent(*q, sql);` after `MemberSql::bindFilters(*q, query);`. It binds only when a status filter put `:today` into the SQL.
5. In `getMember`, build the SQL as `const std::string sql = memberSelectSql() + "        WHERE m.id = :id\n";`, prepare `sql`, and bind `:today` along with `:id`:
   ```cpp
   if (!q->bind(":id", id) || !q->bind(LoanSql::todayPlaceholder(), Clock::todayIso())) {
   ```
6. In `insertMemberRow`, after `registeredAt` / `ageGroup`:
   ```cpp
       // Registered today, so the year starts today: registration + 1 year - 1 day.
       const std::string activeUntil = activeUntilFor({}, status, Clock::today());
   ```
   In the `INSERT`, replace the column `status` with `active_until` and `:status` with `:active_until`. Replace `insert->bind(":status", status)` with `insert->bind(":active_until", activeUntil)`. The history call becomes:
   ```cpp
       if (const auto history =
               recordStatusChange(memberId, {}, status, "Registered, active until " + activeUntil);
           !history) {
   ```
7. In `applyMemberFields`, replace `const std::string status = trim(input.status);` with:
   ```cpp
       // existing->status is derived on read, so this is what the librarian saw.
       const std::string oldStatus = existing->status;
       const std::string status = trim(input.status).empty() ? oldStatus : trim(input.status);
       const std::string activeUntil = activeUntilFor(existing->activeUntil, status, Clock::today());
   ```
   In the `UPDATE`, change `status = :status,` to `active_until = :active_until,`. Change the bind `update->bind(":status", status)` to `update->bind(":active_until", activeUntil)`. Replace the closing history block with:
   ```cpp
       if (oldStatus != status) {
           const std::string note = status == MemberStatus::kActive
               ? "Renewed until " + activeUntil
               : std::string("Ended early");
           if (const auto history = recordStatusChange(id, oldStatus, status, note); !history) {
               return history;
           }
       }
   ```
8. Confirm that nothing in the file still names the column:
   Run: `grep -n "m.status\|:status\|status = :\| status,$" libraries/Core/src/MemberRepository.cpp`
   Expected: no SQL hits. The C++ variables `status` / `oldStatus` are fine.

- [ ] **Step 5: Switch `CirculationRepository`**

Add `#include "MemberSql.h"` and `namespace MemberSql = VLMS::MemberSql;`.

1. `loanSelectSql()`: replace the line `m.status AS member_status,` by closing the raw string before it and splicing in the expression:
   ```cpp
               TRIM(m.first_name || ' ' || m.last_name) AS member_name,
               )SQL")
           + MemberSql::statusExpression("m.") + R"SQL( AS member_status,
               l.book_copy_id,
   ```
   Both callers (`listLoans`, `getLoan`) already bind `:today` for `isOverdue`. Confirm it:
   Run: `grep -n "loanSelectSql()" -A8 libraries/Core/src/CirculationRepository.cpp | grep -c "today"`
   Expected: `2`.
2. `memberCanBorrow`: the query becomes
   ```cpp
       auto q = m_session.prepare("SELECT " + MemberSql::isActive("")
                                  + ", archived_at FROM members WHERE id = :id");
   ```
   Bind `:today` with `:id`: `if (!q->bind(":id", memberId) || !q->bind(LoanSql::todayPlaceholder(), Clock::todayIso()))`. The inactive check becomes `if (q->integer(0) == 0) { return RepoSql::validation("error.loan.memberInactive"); }`. The archived check keeps its place, before it.
3. `listBorrowableMembers`: the SQL head becomes
   ```cpp
       std::string sql = "SELECT id, membership_number, first_name, last_name, "
           + MemberSql::statusExpression("") + " FROM members WHERE " + MemberSql::isActive("")
           + " AND archived_at IS NULL ";
   ```
   Replace `q->bind(":status", std::string_view{MemberStatus::kActive})` with `q->bind(LoanSql::todayPlaceholder(), Clock::todayIso())`.

- [ ] **Step 6: Count members from the date in `MetricsRepository`**

Add `#include "MemberSql.h"`. Replace the four member-status entries with two:

```cpp
        {&metrics.membersActive,
         "SELECT COUNT(*) FROM members m WHERE " + VLMS::MemberSql::isActive("m."),
         {{LoanSql::todayPlaceholder(), Clock::todayIso()}}},
        {&metrics.membersNonActive,
         "SELECT COUNT(*) FROM members m WHERE NOT " + VLMS::MemberSql::isActive("m."),
         {{LoanSql::todayPlaceholder(), Clock::todayIso()}}},
```

`membersSubscribed` and `membersUnsubscribed` stay in the struct and stay 0 until Task 7 removes them.

- [ ] **Step 7: Confirm that no SQL reads the column**

Run: `grep -rnE "m\.status|members\.status|:status\b|SELECT[^;]*\bstatus\b[^_]" libraries/Core/src --include=*.cpp | grep -v Database.cpp`
Expected: no output. Then read every remaining `status` in `MemberRepository.cpp`, `CirculationRepository.cpp` and `MetricsRepository.cpp` (`grep -n "status"`), and confirm each one is a C++ variable, a history column (`old_status` / `new_status`), or the `AS status` / `AS member_status` alias.

- [ ] **Step 8: Run everything**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all pass. `test_core_MemberStatusExpiry` has 10 tests. Confirm them with the filtered command.

- [ ] **Step 9: Commit**

```bash
git add libraries/Core
git commit -m "Read a member's status from their last active day.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Drop `members.status` (schema v7, step 2 of 2)

**Wave 3, runs in parallel with Tasks 5–8.** Database files only.

**Files:**
- Modify: `libraries/Core/src/Database.cpp`
- Modify: `database/schema.sql`
- Modify: `libraries/Core/test/src/test_database_migrations.cpp`

**Interfaces:**
- Consumes (Task 2): `migrateMemberActiveUntilIfNeeded()`. From Task 3: nothing reads `status`.
- Produces: after `open()`, `members` has no `status` column and there is no `idx_members_status`, on fresh, v6 and legacy databases alike.

- [ ] **Step 1: Write the failing tests**

In `ConstraintMigrationRecreatesEveryIndex`, remove `"idx_members_status"` from the list. Add at the end of the file:

```cpp
TEST_F(test_core_DatabaseMigrations, VersionSevenDropsTheStatusColumnAndItsIndex)
{
    const auto db = openFixture("schema_v6.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();

    EXPECT_FALSE(contains(db->columnNames("members"), "status"));
    EXPECT_TRUE(contains(db->columnNames("members"), "active_until"));
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM sqlite_master WHERE name = 'idx_members_status'")
                  .toInt(),
              0);
    // DROP COLUMN rewrites the table; it must not take the history with it.
    EXPECT_EQ(db->count("member_status_history"), 3);
    EXPECT_EQ(db->scalar("SELECT COUNT(*) FROM pragma_foreign_key_check").toInt(), 0);
}

TEST_F(test_core_DatabaseMigrations, FreshDatabaseHasNoStatusColumn)
{
    const TestDatabase fresh;
    ASSERT_TRUE(fresh.isValid()) << fresh.lastError();
    EXPECT_FALSE(contains(fresh.columnNames("members"), "status"));
    EXPECT_TRUE(contains(fresh.columnNames("members"), "active_until"));
}

TEST_F(test_core_DatabaseMigrations, EveryLegacyShapeEndsWithoutStatus)
{
    for (const std::string& fixture : fixtureNames()) {
        SCOPED_TRACE(fixture);
        const auto db = openFixture(fixture);
        ASSERT_NE(db, nullptr);
        ASSERT_TRUE(db->isValid()) << db->lastError();
        EXPECT_FALSE(contains(db->columnNames("members"), "status"));
        EXPECT_TRUE(contains(db->columnNames("members"), "active_until"));
    }
}

TEST_F(test_core_DatabaseMigrations, PreConstraintDatabaseEndsWithoutStatus)
{
    const auto db = openFixture("pre_constraint_dates.sql");
    ASSERT_NE(db, nullptr);
    ASSERT_TRUE(db->isValid()) << db->lastError();
    EXPECT_FALSE(contains(db->columnNames("members"), "status"));
    EXPECT_EQ(db->count("member_status_history"), 2);
}
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `cmake --build build -j && env VLMS_SCHEMA_PATH=$PWD/database/schema.sql VLMS_TEST_DATA_DIR=$PWD/libraries/Core/test/data TZ=Africa/Tunis ./build/bin/test_vlms_core --gtest_filter='test_core_DatabaseMigrations.*'`
Expected: the four new tests fail on `status` still being present.

- [ ] **Step 3: Drop the column in the migration**

Replace `migrateMemberActiveUntilIfNeeded()` in `Database.cpp` with:

```cpp
bool Database::migrateMemberActiveUntilIfNeeded()
{
    const bool addColumn = !tableHasColumn("members", "active_until");
    const bool dropStatus = tableHasColumn("members", "status");
    if (!addColumn && !dropStatus) {
        return true;
    }
    const Status work = m_session->transaction([&] {
        if (addColumn) {
            if (const Status added = m_session->exec(
                    "ALTER TABLE members ADD COLUMN active_until TEXT "
                    "CHECK (date(active_until) IS active_until)");
                !added) {
                return added;
            }
            // A year from registering, less a day: registered 2025-09-24,
            // active through 2026-09-23. The stored status is deliberately not
            // read -- the year is the rule, and a status nobody expired is not
            // evidence.
            if (const Status filled = m_session->exec(
                    "UPDATE members SET active_until = date(registered_at, '+1 year', '-1 day')");
                !filled) {
                return filled;
            }
        }
        if (dropStatus) {
            // Status is read from active_until now. DROP COLUMN (SQLite 3.35+)
            // refuses an indexed column, so the index goes first; the CHECK is
            // the column's own and goes with it. No rows are deleted, so
            // member_status_history's ON DELETE CASCADE never fires.
            if (const Status dropped = m_session->exec("DROP INDEX IF EXISTS idx_members_status");
                !dropped) {
                return dropped;
            }
            return m_session->exec("ALTER TABLE members DROP COLUMN status");
        }
        return Status::ok();
    });
    if (!work) {
        warn("Member active_until migration failed: " + work.error().detail);
        return false;
    }
    return true;
}
```

- [ ] **Step 4: Remove `status` from the legacy rebuild**

In `rebuildTablesWithDateConstraints()` `membersRebuild`:
- delete the two lines `status TEXT NOT NULL DEFAULT 'subscribed'` / `CHECK (status IN (...)),`;
- remove `status, ` from both the `INSERT INTO members_new (...)` column list and its `SELECT` list;
- delete `"CREATE INDEX IF NOT EXISTS idx_members_status ON members(status)",`.

- [ ] **Step 5: Remove `status` from `database/schema.sql`**

- Delete the two-line `status TEXT NOT NULL DEFAULT 'subscribed' … CHECK (...)` column.
- Delete `CREATE INDEX IF NOT EXISTS idx_members_status ON members(status);`.
- Replace the header comment block above `CREATE TABLE IF NOT EXISTS members` with:
  ```sql
  -- ---------------------------------------------------------------------------
  -- Library members (customers — no app login)
  -- Status is not stored. A member is active while active_until (their last
  -- active day) is today or later, and only an active member may borrow.
  -- Registering or renewing sets it a year ahead; see MemberSql::isActive.
  -- ---------------------------------------------------------------------------
  ```

- [ ] **Step 6: Run the whole Core suite, the real-database run included**

Run: `cmake --build build -j && ctest --test-dir build -R '^test_vlms_core' --output-on-failure`
Expected: all pass, including `test_vlms_core_realdb`, which takes the 2026-08-14 backup copy through ADD and DROP.

If a legacy fixture fails at `DROP COLUMN` (SQLite refuses a column that is named in a table-level CHECK, an index other than `idx_members_status`, or a view), read that fixture's DDL and report it to the controller. **Do not edit the fixture to make the test pass.** A fixture is a shape that real databases had.

- [ ] **Step 7: Commit**

```bash
git add database/schema.sql libraries/Core/src/Database.cpp libraries/Core/test/src/test_database_migrations.cpp
git commit -m "Drop the stored member status.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The editor shows two statuses and the last active day

**Wave 3, runs in parallel.**

**Files:**
- Modify: `applications/vlms/src/ui/members/MemberEditorDialog.h`, `.cpp`
- Create: `applications/vlms/test/src/test_member_editor_status.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt` (one line, after `src/test_member_filters.cpp`)

**Interfaces:**
- Consumes: `MemberRepository::activeUntilFor`, `MemberRecord::activeUntil`, `member.field.activeUntil`.
- Produces: the widgets `memberStatusCombo` (`QComboBox`) and `memberActiveUntilValue` (`QLabel`) in the dialog.

- [ ] **Step 1: Write the failing tests**

Create `applications/vlms/test/src/test_member_editor_status.cpp`:

```cpp
#include "TestDatabase.h"
#include "UiTest.h"

#include "ui/members/MemberEditorDialog.h"

#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/MemberRepository.h>
#include <VLMS/Core/MemberTypes.h>
#include <VLMS/Core/Strings.h>

#include <QComboBox>
#include <QLabel>

#include <gtest/gtest.h>

#include <memory>

using VLMS::Date;
using VLMS::Locale;
using VLMS::ScopedClock;
using namespace VLMS::Test;

namespace {

MemberRecord memberUntil(const std::string& activeUntil, const std::string& status)
{
    MemberRecord member;
    member.id = 1;
    member.membershipNumber = "1";
    member.firstName = "Amina";
    member.lastName = "Ben Salah";
    member.dateOfBirth = "1990-05-12";
    member.status = status;
    member.activeUntil = activeUntil;
    return member;
}

bool pick(QComboBox* combo, const char* code)
{
    const int index = combo->findData(QString::fromLatin1(code));
    if (index < 0) {
        return false;
    }
    combo->setCurrentIndex(index);
    return true;
}

}  // namespace

class test_ui_MemberEditorStatus : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_repository.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MemberRepository> m_repository;
};

TEST_F(test_ui_MemberEditorStatus, TheComboOffersActiveAndNotActiveOnly)
{
    MemberEditorDialog dialog(*m_repository);
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("memberStatusCombo"));
    ASSERT_NE(combo, nullptr);
    ASSERT_EQ(combo->count(), 2);
    EXPECT_EQ(combo->itemData(0).toString(), QString::fromLatin1(MemberStatus::kActive));
    EXPECT_EQ(combo->itemData(1).toString(), QString::fromLatin1(MemberStatus::kNonActive));
    EXPECT_EQ(combo->currentData().toString(), QString::fromLatin1(MemberStatus::kActive));
}

TEST_F(test_ui_MemberEditorStatus, ANewMemberShowsAYearFromToday)
{
    const ScopedClock pinned(Date(2026, 9, 23));
    MemberEditorDialog dialog(*m_repository);
    auto* until = dialog.findChild<QLabel*>(QStringLiteral("memberActiveUntilValue"));
    ASSERT_NE(until, nullptr);
    EXPECT_EQ(until->text(), QStringLiteral("2027-09-22"));
}

TEST_F(test_ui_MemberEditorStatus, PickingActiveForAnExpiredMemberShowsTheRenewal)
{
    const ScopedClock pinned(Date(2026, 9, 23));
    MemberEditorDialog dialog(*m_repository, memberUntil("2026-01-09", MemberStatus::kNonActive));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("memberStatusCombo"));
    auto* until = dialog.findChild<QLabel*>(QStringLiteral("memberActiveUntilValue"));
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(until, nullptr);

    EXPECT_EQ(combo->currentData().toString(), QString::fromLatin1(MemberStatus::kNonActive));
    EXPECT_EQ(until->text(), QStringLiteral("2026-01-09"));

    ASSERT_TRUE(pick(combo, MemberStatus::kActive));
    EXPECT_EQ(until->text(), QStringLiteral("2027-09-22"));
    EXPECT_EQ(dialog.memberInput().status, MemberStatus::kActive);
}

TEST_F(test_ui_MemberEditorStatus, PickingNotActiveForAnActiveMemberShowsYesterday)
{
    const ScopedClock pinned(Date(2026, 9, 23));
    MemberEditorDialog dialog(*m_repository, memberUntil("2027-03-01", MemberStatus::kActive));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("memberStatusCombo"));
    auto* until = dialog.findChild<QLabel*>(QStringLiteral("memberActiveUntilValue"));
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(until, nullptr);

    ASSERT_TRUE(pick(combo, MemberStatus::kNonActive));
    EXPECT_EQ(until->text(), QStringLiteral("2026-09-22"));

    ASSERT_TRUE(pick(combo, MemberStatus::kActive));
    EXPECT_EQ(until->text(), QStringLiteral("2027-03-01")) << "back to the saved status keeps the date";
}

TEST_F(test_ui_MemberEditorStatus, TheRowIsLabelledInEveryLanguage)
{
    for (const char* locale : {"ar", "fr", "en"}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        MemberEditorDialog dialog(*m_repository);
        bool found = false;
        for (QLabel* label : dialog.findChildren<QLabel*>()) {
            if (label->text().startsWith(qs(VLMS::Strings::t("member.field.activeUntil")))) {
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }
    Locale::setCode("en");
}
```

Add `src/test_member_editor_status.cpp` to `applications/vlms/test/CMakeLists.txt` directly after `src/test_member_filters.cpp`.

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `cmake --build build -j && ctest --test-dir build -R 'test_ui_MemberEditorStatus' --output-on-failure`
Expected: they fail, because `memberStatusCombo` / `memberActiveUntilValue` are not found (null).

- [ ] **Step 3: Implement**

`MemberEditorDialog.h`: declare `void updateActiveUntil();` among the private helpers, and among the members:

```cpp
    QLabel* m_activeUntilLabel = nullptr;
    /// The stored last active day, empty for a member not yet saved.
    QString m_loadedActiveUntil;
```

`MemberEditorDialog.cpp`:
1. Add `#include <VLMS/Core/Clock.h>`.
2. In `buildUi()`, set `m_statusCombo->setObjectName(QStringLiteral("memberStatusCombo"));` right after creating it. Directly after the `addPairedRow(... member.field.status, m_statusCombo);` call, add:
   ```cpp
       m_activeUntilLabel = new QLabel(m_fieldsPanel);
       m_activeUntilLabel->setObjectName(QStringLiteral("memberActiveUntilValue"));
       m_activeUntilLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
       addWideRow(row++, QStringLiteral("member.field.activeUntil"), m_activeUntilLabel);
   ```
3. In `loadMember`, add `m_loadedActiveUntil = qs(member.activeUntil);` before the status combo is set.
4. At the end of **both** constructors, after `retranslateUi();`:
   ```cpp
       connect(m_statusCombo, &QComboBox::currentIndexChanged,
               this, &MemberEditorDialog::updateActiveUntil);
       updateActiveUntil();
   ```
5. Add:
   ```cpp
   void MemberEditorDialog::updateActiveUntil() {
       // The date the save will store, so the librarian sees what Active buys.
       const std::string chosen = ss(m_statusCombo->currentData().toString());
       m_activeUntilLabel->setText(qs(MemberRepository::activeUntilFor(
           ss(m_loadedActiveUntil), chosen, VLMS::Clock::today())));
   }
   ```

`retranslateUi` repopulates the combo and keeps the current code, so the label stays right after a language change.

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build -j && ctest --test-dir build -R 'test_ui_' --output-on-failure`
Expected: all pass, including `test_ui_DialogTranslations`.

- [ ] **Step 5: Commit**

```bash
git add applications/vlms/src/ui/members/MemberEditorDialog.h applications/vlms/src/ui/members/MemberEditorDialog.cpp \
        applications/vlms/test/src/test_member_editor_status.cpp applications/vlms/test/CMakeLists.txt
git commit -m "Show the last active day beside the member status.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The Members page shows the last active day and filters on two statuses

**Wave 3, runs in parallel.**

**Files:**
- Modify: `applications/vlms/src/ui/members/MembersPage.cpp`
- Modify: `applications/vlms/test/src/test_member_filters.cpp`

**Interfaces:**
- Consumes: `MemberRecord::activeUntil`, `member.field.activeUntil`, `MemberRepository::statusCodes()` (two codes), and the filter on the derived status (Task 3).
- Produces: nothing new for other tasks.

- [ ] **Step 1: Write the failing tests**

Append to `test_member_filters.cpp`:

```cpp
TEST_F(test_ui_MemberFilters, StatusFilterOffersActiveAndNotActive)
{
    seedTunisAndSfax();
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("statusFilter"));
    ASSERT_NE(list, nullptr);

    QStringList codes;
    for (int row = 0; row < list->count(); ++row) {
        const QString code = list->item(row)->data(Qt::UserRole).toString();
        if (!code.isEmpty()) {
            codes << code;
        }
    }
    EXPECT_EQ(codes, (QStringList{QStringLiteral("active"), QStringLiteral("non_active")}));
}

TEST_F(test_ui_MemberFilters, NotActiveKeepsOnlyTheMemberWhoseYearEnded)
{
    seedTunisAndSfax();
    ASSERT_TRUE(m_db->execBound("UPDATE members SET active_until = '2026-01-01' WHERE id = :id",
                                {{"id", static_cast<std::int64_t>(m_maleId)}}));
    const ScopedClock pinned(Date(2026, 9, 23));
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 2);

    ASSERT_TRUE(selectFilterCode(m_page->findChild<QListWidget*>(QStringLiteral("statusFilter")),
                                 QStringLiteral("non_active")));
    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(tableText(table, 0, 4), QStringLiteral("Non active"));
}

TEST_F(test_ui_MemberFilters, DetailsShowTheLastActiveDay)
{
    seedTunisAndSfax();
    ASSERT_TRUE(m_db->execBound("UPDATE members SET active_until = '2027-04-30' WHERE id = :id",
                                {{"id", static_cast<std::int64_t>(m_femaleId)}}));
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* search = m_page->findChild<QLineEdit*>(QStringLiteral("listSearch"));
    ASSERT_NE(search, nullptr);
    search->setText(QStringLiteral("Sfax"));
    QApplication::processEvents();
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_EQ(table->rowCount(), 1);
    table->selectRow(0);
    QApplication::processEvents();

    QString value;
    for (QLabel* label : m_page->findChildren<QLabel*>(QStringLiteral("bookDetailLabel"))) {
        if (label->text() != QStringLiteral("Active until:")) {
            continue;
        }
        for (QLabel* sibling : label->parentWidget()->findChildren<QLabel*>()) {
            if (sibling != label) {
                value = sibling->text();
            }
        }
    }
    EXPECT_EQ(value, QStringLiteral("2027-04-30"));
}
```

Add `#include <QLabel>` if it is missing. Before relying on them, check two assumptions: that the English label for `non_active` is `Non active` (`grep -n '"member.status.non_active"' libraries/Core/src/Strings.cpp`), and that the search matches `city` (it does: see `MemberSql::filterClause`). If the search runs on a debounce timer, look at how `SearchStillFindsMembersByOccupation` waits and use the same wait.

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `cmake --build build -j && ctest --test-dir build -R 'test_ui_MemberFilters' --output-on-failure`
Expected: `DetailsShowTheLastActiveDay` fails with an empty value. The two filter tests may already pass after Tasks 1 and 3. They guard the two-status filter, which is what this task is about.

- [ ] **Step 3: Implement**

In `MembersPage.cpp`, add the key after `member.field.status` in `memberDetailLabelKeys()`:

```cpp
        QStringLiteral("member.field.status"),
        QStringLiteral("member.field.activeUntil"),
```

In `updatePreview`'s `rows`, add the matching row at the same position, after the status row. Keys and rows pair up by index:

```cpp
        {QStringLiteral("member.field.activeUntil"), VLMS::dashIfEmpty(member.activeUntil)},
```

If `dashIfEmpty` has no `std::string` overload, wrap the argument in `qs(...)`, the way the other rows do.

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build -j && ctest --test-dir build -R 'test_ui_' --output-on-failure`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add applications/vlms/src/ui/members/MembersPage.cpp applications/vlms/test/src/test_member_filters.cpp
git commit -m "Show a member's last active day in their details.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Metrics keeps the Active and Not active tiles only

**Wave 3, runs in parallel.**

**Files:**
- Modify: `libraries/Core/include/VLMS/Core/MetricsTypes.h`
- Modify: `libraries/Core/src/MetricsRepository.cpp` (confirm only; Task 3 already removed the queries)
- Modify: `applications/vlms/src/ui/metrics/MetricsPage.cpp`
- Create: `applications/vlms/test/src/test_metrics_member_tiles.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt` (one line, after `src/test_metrics_activity_sort.cpp`)

**Interfaces:**
- Consumes: `LibraryMetrics::membersActive` / `membersNonActive` (Task 3).
- Produces: `LibraryMetrics` without `membersSubscribed` / `membersUnsubscribed`.

- [ ] **Step 1: Write the failing test**

Create `applications/vlms/test/src/test_metrics_member_tiles.cpp`:

```cpp
#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/metrics/MetricsPage.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/MetricsRepository.h>

#include <QLabel>

#include <gtest/gtest.h>

#include <memory>

using VLMS::Locale;
using namespace VLMS::Test;

class test_ui_MetricsMemberTiles : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_metrics = std::make_unique<MetricsRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_metrics.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MetricsRepository> m_metrics;
};

TEST_F(test_ui_MetricsMemberTiles, OnlyActiveAndNotActiveAreCounted)
{
    MetricsPage page(*m_metrics);
    page.refreshMetrics();

    QStringList labels;
    for (QLabel* label : page.findChildren<QLabel*>(QStringLiteral("metricLabel"))) {
        labels << label->text();
    }
    EXPECT_TRUE(labels.contains(QStringLiteral("Total members")));
    EXPECT_TRUE(labels.contains(QStringLiteral("Active")));
    EXPECT_TRUE(labels.contains(QStringLiteral("Non-active")));
    EXPECT_FALSE(labels.contains(QStringLiteral("Subscribed")));
    EXPECT_FALSE(labels.contains(QStringLiteral("Unsubscribed")));
    for (const QString& text : labels) {
        EXPECT_FALSE(text.startsWith(QStringLiteral("metrics."))) << text.toStdString();
    }
    // 4 overview + 3 members + 3 circulation.
    EXPECT_EQ(labels.size(), 10);
}
```

Add `src/test_metrics_member_tiles.cpp` to `applications/vlms/test/CMakeLists.txt` directly after `src/test_metrics_activity_sort.cpp`.

- [ ] **Step 2: Run the test and confirm it fails**

Run: `cmake --build build -j && ctest --test-dir build -R 'test_ui_MetricsMemberTiles' --output-on-failure`
Expected: it fails. Two labels read `metrics.membersSubscribed` / `metrics.membersUnsubscribed`, and there are 12 labels.

- [ ] **Step 3: Implement**

`MetricsTypes.h`: delete `int membersSubscribed = 0;` and `int membersUnsubscribed = 0;`.

`MetricsPage.cpp`: remove `metrics.membersSubscribed` / `metrics.membersUnsubscribed` from `memberKeys` and change that `makeMetricGrid(..., 5)` to `3`. Remove `metrics.membersSubscribed` / `metrics.membersUnsubscribed` from `memberValues`.

Then run: `grep -rn "membersSubscribed\|membersUnsubscribed" libraries applications`
Expected: no output.

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add libraries/Core/include/VLMS/Core/MetricsTypes.h applications/vlms/src/ui/metrics/MetricsPage.cpp \
        applications/vlms/test/src/test_metrics_member_tiles.cpp applications/vlms/test/CMakeLists.txt
git commit -m "Count members as active or not, nothing else.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: The member import writes `active_until`

**Wave 3, runs in parallel. Edit the file but do not commit it**, because it already holds the user's uncommitted `--repair-names` work.

**Files:**
- Modify: `scripts/import_members_from_xlsx.py` (`insert_members`, around lines 249–286)

**Interfaces:**
- Consumes: the v7 schema (`active_until`, no `status`).
- Produces: nothing for other tasks.

- [ ] **Step 1: Write a failing check against a scratch database**

Write `$SCRATCH/check_import.py`, where `$SCRATCH` is the session scratchpad and never the repo:

```python
import importlib.util, sqlite3, sys
from pathlib import Path

repo = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("imp", repo / "scripts/import_members_from_xlsx.py")
imp = importlib.util.module_from_spec(spec); spec.loader.exec_module(imp)

conn = sqlite3.connect(":memory:")
conn.executescript((repo / "database/schema.sql").read_text(encoding="utf-8"))
row = {"membership_number": "1", "first_name": "A", "last_name": "B", "sex": "", "date_of_birth": "",
       "phone": "", "address": "", "city": "", "occupation": "", "age_group": "", "full_name": "A B",
       "registered_at": "2025-09-24", "source_row": 2}
imp.insert_members(conn, [row])
until = conn.execute("SELECT active_until FROM members").fetchone()[0]
assert until == "2026-09-23", until
history = conn.execute("SELECT new_status FROM member_status_history").fetchall()
assert len(history) == 1 and history[0][0] in ("active", "non_active"), history
print("ok", until, history)
```

If `insert_members` expects other keys in `row`, copy them from `read_member_rows` and keep the assertions.

Run: `python3 $SCRATCH/check_import.py $PWD`
Expected: `AssertionError: None`. This task branches from Task 3, where `status` still exists with a default, so the old `INSERT` runs and leaves `active_until` empty. Once Task 4 merges, the old `INSERT` would fail outright on the missing `status` column. The fix below works on both schemas.

- [ ] **Step 2: Implement**

In `insert_members`, change the `INSERT` column list from `phone, address, city, status, notes, …` to `phone, address, city, active_until, notes, …`, and its value `'subscribed'` to `date(:registered_at, '+1 year', '-1 day')`. Replace the history loop with:

```python
    # Status is not stored: a member is active while active_until is today or
    # later. The history row records what that made them on the day of import.
    for row in conn.execute(
        "SELECT id, CASE WHEN active_until >= date('now', 'localtime') "
        "THEN 'active' ELSE 'non_active' END FROM members"
    ).fetchall():
        conn.execute(
            "INSERT INTO member_status_history (member_id, old_status, new_status) "
            "VALUES (?, NULL, ?)",
            (row[0], row[1]),
        )
```

Update the module docstring's mention of status, if it has one.

- [ ] **Step 3: Run the check and confirm it passes**

Run: `python3 $SCRATCH/check_import.py $PWD && python3 -m py_compile scripts/import_members_from_xlsx.py`
Expected: `ok 2026-09-23 [...]`.

- [ ] **Step 4: Do not commit**

Report `git diff scripts/import_members_from_xlsx.py` to the controller. The controller asks the user whether to commit this change together with their uncommitted `--repair-names` work.

---

### Task 9: Close-out (controller only, after Tasks 4–8 have merged)

- [ ] **Step 1: Run the full suite on the merged branch**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: all pass. Paste the summary line in the report.

- [ ] **Step 2: Back up the live database, then let the app migrate it**

```bash
cp database/vlms.db "database/vlms.db.bak-$(date +%Y%m%d-%H%M%S)"
```

Launch the app from `build/bin/vlms`, following the screenshot recipe in memory (xcb, foreground, capture by window id), and let `open()` migrate. Then check the result with a read-only connection:

```bash
python3 - <<'EOF'
import sqlite3
c = sqlite3.connect('file:database/vlms.db?mode=ro', uri=True)
print(c.execute("PRAGMA user_version").fetchone())
print([r[1] for r in c.execute("PRAGMA table_info(members)") if r[1] in ('status', 'active_until')])
print(c.execute("SELECT active_until >= date('now','localtime'), COUNT(*) FROM members "
                "WHERE archived_at IS NULL GROUP BY 1").fetchall())
EOF
```

Expected on 2026-09-23: `(7,)`, `['active_until']`, and 728 not active / 808 active among live members. The 8 members registered between 2025-09-02 and 2025-09-23 now read Not active, as agreed. On a later date the split moves with the calendar, which is the point.

- [ ] **Step 3: Look at it**

Take screenshots of: the Members page with the Status filter showing two entries; one member's details showing Active until; the editor for an expired member with Active picked, showing next year's date; and Metrics with three member tiles. Check the Arabic view as well.

- [ ] **Step 4: Record it**

- Add a dated entry at the top of the session log in `CLAUDE.md`. It covers: two statuses; `active_until`, from which status is derived; renewal through the editor combo; migration v7 (ADD, fill, DROP); the 8 members who expired; the backup file name; and that status history is written again. Remove the 2026-09-22 claim that "`member_status_history` is still written but never read … nothing transitions a member automatically", or mark it superseded.
- In `docs/superpowers/specs/2026-09-23-user-manual-wiki-design.md`, change "the four member statuses" (two places) to "the two member statuses and the last active day".
- Commit both: `git commit -m "Note the one-year member status in the session log."` with the attribution trailer.

- [ ] **Step 5: Whole-branch review and finish**

Dispatch a final reviewer over `git diff Beta...member-status-expiry` against the spec, then use `finishing-a-development-branch`.
