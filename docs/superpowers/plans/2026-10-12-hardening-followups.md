# Hardening Follow-ups Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the four robustness gaps left from the security review: a second copy of the app on the same database, text that looks like HTML rendered as HTML, an image copied in by a save that is then rolled back, and installer backups that leave out the images.

**Architecture:** SQLite waits for a busy database instead of failing at once, and an `InstanceLock` (a `QLockFile` in the database folder) lets only one copy run per installation. Labels and message boxes that show library data are set to `Qt::PlainText`. Image files are staged as `<target>.part` by a new internal `StagedFiles` helper and only swapped in after the save's transaction commits. The Inno Setup backup also copies the `resources` folder.

**Tech Stack:** C++20, Qt 6 Widgets, SQLite 3, GoogleTest via CTest, Inno Setup 6 Pascal script.

## Global Constraints

- Work on branch `fix/hardening-followups`, created from `Beta`. Never commit to `main` or `Beta`.
- Build with at most four parallel jobs: `cmake --build build -j4`. Never `-j` without a number.
- Configure once if needed: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DVLMS_BUILD_TESTS=ON`
- Run tests through CTest, never the test binaries directly (CTest sets the environment): `QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R '<regex>'`
- Full suite before each commit: `QT_QPA_PLATFORM=offscreen ctest --test-dir build -j4 --no-tests=error -LE 'realdb|bench'` must report 100 % passed.
- Every new text shown to the user gets a key in all three tables of `libraries/Core/src/Strings.cpp`: Arabic (first table), French (second), English (third). `test_core_StringsParity` fails otherwise.
- Never write the real library's name, its town, or the name of the system VLMS replaced, in code, tests, docs or commit messages. Use "Tunis" or "Tunisia" for place names.
- Commit subjects are one imperative sentence ending with a full stop, no `feat:` prefix. Every commit message ends with a blank line and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Follow TDD: write the test, watch it fail for the stated reason, then implement.
- Match the surrounding comment style: comments explain why, in full sentences.

---

### Task 1: SQLite waits for a busy database

**Files:**
- Modify: `libraries/Database/include/VLMS/Database/SqliteSession.h`
- Modify: `libraries/Database/src/SqliteSession.cpp` (function `SqliteSession::open`)
- Modify: `libraries/Database/src/sqlite3_api.h` (fallback declarations used when no SQLite dev package exists)
- Test: `libraries/Database/test/src/test_sqlite_session.cpp`

**Interfaces:**
- Produces: `static constexpr int Database::SqliteSession::kBusyTimeoutMs = 5000;`

- [ ] **Step 1: Write the failing test** — append to `libraries/Database/test/src/test_sqlite_session.cpp`:

```cpp
// A second program on the same file (a backup tool, an antivirus scan, a
// second copy of VLMS) holds the lock for a moment. Without a busy timeout
// SQLite refuses at once with "database is locked" and the librarian's save
// fails; with one it waits its turn.
TEST(test_core_SqliteSession, WaitsForABusyDatabaseInsteadOfFailingAtOnce)
{
    ScopedTempDir dir;
    ASSERT_TRUE(dir.isValid());
    auto opened = Database::SqliteSession::open(dir.dbPath());
    ASSERT_TRUE(opened);

    auto query = opened.value()->prepare("PRAGMA busy_timeout");
    ASSERT_TRUE(query);
    ASSERT_TRUE(query.value().next());
    EXPECT_EQ(query.value().integer(0), Database::SqliteSession::kBusyTimeoutMs);
    EXPECT_GT(Database::SqliteSession::kBusyTimeoutMs, 0);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build -j4 --target test_vlms_database && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R 'test_vlms_database$'`
Expected: compile error, `kBusyTimeoutMs` is not a member of `SqliteSession`.

- [ ] **Step 3: Implement**

In `SqliteSession.h`, inside `class SqliteSession`, `public:` section, before `static Core::Result<std::unique_ptr<SqliteSession>> open(...)`:

```cpp
    /// How long a statement waits for a lock another connection holds before
    /// it gives up with SQLITE_BUSY.
    static constexpr int kBusyTimeoutMs = 5000;
```

In `SqliteSession.cpp`, `SqliteSession::open`, replace the final `return` with:

```cpp
    sqlite3_busy_timeout(db, kBusyTimeoutMs);
    return Core::Result<std::unique_ptr<SqliteSession>>::ok(
        std::unique_ptr<SqliteSession>(new SqliteSession(db)));
```

In `sqlite3_api.h`, after the line `int sqlite3_close(sqlite3*);` add:

```cpp
int sqlite3_busy_timeout(sqlite3*, int ms);
```

- [ ] **Step 4: Run the test and the full suite**

Run: `cmake --build build -j4 && QT_QPA_PLATFORM=offscreen ctest --test-dir build -j4 --no-tests=error -LE 'realdb|bench'`
Expected: 100 % passed.

- [ ] **Step 5: Commit**

```bash
git add libraries/Database
git commit -m "Wait for a busy database instead of failing a save at once." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Only one copy of VLMS per installation

**Files:**
- Create: `applications/vlms/src/ui/shell/InstanceLock.h`
- Create: `applications/vlms/src/ui/shell/InstanceLock.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (add both files next to `src/ui/shell/Application.cpp` / `.h`)
- Modify: `applications/vlms/src/ui/shell/Application.h`, `Application.cpp`
- Modify: `applications/vlms/src/main.cpp`
- Modify: `libraries/Core/src/Strings.cpp` (key `app.alreadyRunning`, three tables)
- Create: `applications/vlms/test/src/test_instance_lock.cpp`
- Modify: `applications/vlms/test/CMakeLists.txt` (add the test to the `test_vlms_shell` SRC list)

**Interfaces:**
- Produces: `class VLMS::InstanceLock { explicit InstanceLock(const QString& directory); bool tryAcquire(); }` and `bool Application::isAlreadyRunning() const`.

- [ ] **Step 1: Write the failing tests** — create `applications/vlms/test/src/test_instance_lock.cpp`:

```cpp
#include "ui/shell/InstanceLock.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

// Two copies on one vlms.db each see the other's writes fail with "database is
// locked", so a second copy of the same installation must not start.
TEST(InstanceLock, ASecondCopyIsRefusedWhileTheFirstRuns)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    VLMS::InstanceLock first(dir.path());
    ASSERT_TRUE(first.tryAcquire());

    VLMS::InstanceLock second(dir.path());
    EXPECT_FALSE(second.tryAcquire());
}

TEST(InstanceLock, TheLockIsFreeAgainOnceTheFirstCopyExits)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    {
        VLMS::InstanceLock first(dir.path());
        ASSERT_TRUE(first.tryAcquire());
    }
    VLMS::InstanceLock next(dir.path());
    EXPECT_TRUE(next.tryAcquire());
}

// QLockFile's default treats a lock older than 30 seconds as stale even while
// its owner runs, which would let a second copy start once the first had been
// open for half a minute.
TEST(InstanceLock, AnOldLockOfARunningCopyIsNotTakenOver)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    VLMS::InstanceLock first(dir.path());
    ASSERT_TRUE(first.tryAcquire());

    QFile lockFile(QDir(dir.path()).filePath(QStringLiteral("vlms.lock")));
    ASSERT_TRUE(lockFile.exists());
    ASSERT_TRUE(lockFile.open(QIODevice::ReadWrite));
    ASSERT_TRUE(lockFile.setFileTime(QDateTime::currentDateTime().addSecs(-3600),
                                     QFileDevice::FileModificationTime));
    lockFile.close();

    VLMS::InstanceLock second(dir.path());
    EXPECT_FALSE(second.tryAcquire());
}
```

In `applications/vlms/test/CMakeLists.txt`, in the `build_gtest_executable(NAME test_vlms_shell` block, change

```cmake
        src/test_shell_main.cpp
        src/test_shell.cpp
```

to

```cmake
        src/test_shell_main.cpp
        src/test_shell.cpp
        src/test_instance_lock.cpp
```

- [ ] **Step 2: Run and watch them fail**

Run: `cmake --build build -j4 --target test_vlms_shell`
Expected: compile error, `ui/shell/InstanceLock.h` not found.

- [ ] **Step 3: Implement `InstanceLock`**

`applications/vlms/src/ui/shell/InstanceLock.h`:

```cpp
#pragma once

#include <QLockFile>
#include <QString>

namespace VLMS {

/// One running copy per installation. Two copies on one vlms.db would each
/// see the other's writes fail with "database is locked".
class InstanceLock {
public:
    /// The lock file is `vlms.lock` inside `directory`, the database folder.
    explicit InstanceLock(const QString& directory);

    /// False while another running copy holds the lock. A lock left behind by
    /// a copy that crashed is taken over.
    [[nodiscard]] bool tryAcquire();

private:
    QLockFile m_lock;
};

}  // namespace VLMS
```

`applications/vlms/src/ui/shell/InstanceLock.cpp`:

```cpp
#include "ui/shell/InstanceLock.h"

#include <QDir>

namespace VLMS {

InstanceLock::InstanceLock(const QString& directory)
    : m_lock(QDir(directory).filePath(QStringLiteral("vlms.lock")))
{
    // Never stale by age: the default 30 seconds would hand the lock to a
    // second copy while the first is still open. A lock whose process is gone
    // is still recognised as stale and taken over.
    m_lock.setStaleLockTime(0);
}

bool InstanceLock::tryAcquire()
{
    return m_lock.tryLock(0);
}

}  // namespace VLMS
```

In `applications/vlms/CMakeLists.txt` add `    src/ui/shell/InstanceLock.cpp` on the line after `    src/ui/shell/Application.cpp`, and `    src/ui/shell/InstanceLock.h` on the line after `    src/ui/shell/Application.h`.

- [ ] **Step 4: Run the lock tests**

Run: `cmake --build build -j4 --target test_vlms_shell && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R 'InstanceLock'`
Expected: 3 tests pass.

- [ ] **Step 5: Wire it into the application**

`Application.h`: add `#include "ui/shell/InstanceLock.h"` to the includes; in the public section next to `isDatabaseReady()` add

```cpp
    /// True when another copy of this installation is already running; the
    /// database was not opened.
    [[nodiscard]] bool isAlreadyRunning() const { return m_alreadyRunning; }
```

and in the private section next to `bool m_databaseReady = false;` add

```cpp
    std::unique_ptr<VLMS::InstanceLock> m_instanceLock;
    bool m_alreadyRunning = false;
```

`Application.cpp`: directly before the line `    m_database = new VLMS::Database::Connection(VLMS::Core::Paths::databaseDirectory());` insert

```cpp
    // Before the database is opened: a second copy must not touch it at all.
    m_instanceLock = std::make_unique<VLMS::InstanceLock>(
        QString::fromStdString(VLMS::Core::Paths::databaseDirectory()));
    if (!m_instanceLock->tryAcquire()) {
        m_alreadyRunning = true;
        return;
    }

```

`main.cpp`: directly after `    Application app(argc, argv);` insert

```cpp
    if (app.isAlreadyRunning()) {
        VLMS::showWarning(nullptr, QStringLiteral("VLMS"), VLMS::T("app.alreadyRunning"));
        return 0;
    }

```

`libraries/Core/src/Strings.cpp`: the line `        {"app.databaseUnavailable",` occurs three times, once per table. Directly before each occurrence insert the matching line:
- first (Arabic) table: `        {"app.alreadyRunning", "برنامج VLMS مفتوح بالفعل على هذا الحاسوب. استعمل النافذة المفتوحة."},`
- second (French) table: `        {"app.alreadyRunning", "VLMS est déjà ouvert sur cet ordinateur. Utilisez la fenêtre déjà ouverte."},`
- third (English) table: `        {"app.alreadyRunning", "VLMS is already open on this computer. Use the window that is already open."},`

- [ ] **Step 6: Full suite**

Run: `cmake --build build -j4 && QT_QPA_PLATFORM=offscreen ctest --test-dir build -j4 --no-tests=error -LE 'realdb|bench'`
Expected: 100 % passed.

- [ ] **Step 7: Commit**

```bash
git add applications/vlms libraries/Core/src/Strings.cpp
git commit -m "Let only one copy of VLMS run per installation." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Library data is shown as plain text

Qt's `QLabel` and `QMessageBox` default to `Qt::AutoText`: text that looks like HTML is rendered as HTML, so a book titled `<h1>X</h1>` shows as a huge heading. Every label or message box that can contain library data (titles, names, notes, numbers) must use `Qt::PlainText`. No label in the app uses rich text on purpose; the licence dialog is a `QTextBrowser` and is not touched.

**Files:**
- Modify: `applications/vlms/src/ui/views/UiHelpers.cpp` (functions `makeDetailValueWidget` and `localizeMessageBox`)
- Modify: `applications/vlms/src/ui/views/LoanReturnDialog.cpp` (label `loanReturnSummary`)
- Modify: `applications/vlms/src/ui/views/LoanExtendDialog.cpp` (label `loanExtendSummary`)
- Modify: `applications/vlms/src/ui/views/LoanCheckoutDialog.cpp` (label `m_memberFixedLabel`)
- Test: `applications/vlms/test/src/test_loan_dialogs.cpp`, and create `applications/vlms/test/src/test_plain_text.cpp` added to the `test_vlms_ui` SRC list in `applications/vlms/test/CMakeLists.txt` (on the line after `        src/test_loan_dialogs.cpp`)

- [ ] **Step 1: Write the failing tests**

Append to `applications/vlms/test/src/test_loan_dialogs.cpp` (the fixture `test_ui_LoanDialogs` and its helper `loanDueOn` already exist in that file; add `#include <QLabel>` if it is not included):

```cpp
// A title or name that looks like HTML must read as typed, not render.
TEST_F(test_ui_LoanDialogs, ReturnSummaryShowsMarkupAsTyped)
{
    Repositories::LoanRecord loan = loanDueOn(QDate::currentDate().toString(Qt::ISODate));
    loan.bookTitle = "<b>Bold</b> title";
    LoanReturnDialog dialog(loan);
    auto* summary = dialog.findChild<QLabel*>(QStringLiteral("loanReturnSummary"));
    ASSERT_NE(summary, nullptr);
    EXPECT_EQ(summary->textFormat(), Qt::PlainText);
    EXPECT_TRUE(summary->text().contains(QStringLiteral("<b>Bold</b> title")));
}

TEST_F(test_ui_LoanDialogs, ExtendSummaryShowsMarkupAsTyped)
{
    Repositories::LoanRecord loan = loanDueOn(QDate::currentDate().addDays(3).toString(Qt::ISODate));
    loan.memberName = "<i>Amina</i>";
    LoanExtendDialog dialog(loan);
    auto* summary = dialog.findChild<QLabel*>(QStringLiteral("loanExtendSummary"));
    ASSERT_NE(summary, nullptr);
    EXPECT_EQ(summary->textFormat(), Qt::PlainText);
}

TEST_F(test_ui_LoanDialogs, CheckoutShowsAFixedMemberNameAsTyped)
{
    LoanScope scope;
    scope.memberId = 1;
    scope.label = QStringLiteral("<h1>Amina</h1>");
    LoanCheckoutDialog dialog(*m_lookups, scope);
    bool found = false;
    for (QLabel* label : dialog.findChildren<QLabel*>()) {
        if (label->text() == scope.label) {
            found = true;
            EXPECT_EQ(label->textFormat(), Qt::PlainText);
        }
    }
    EXPECT_TRUE(found) << "the fixed member label was not found";
}
```

Before running, check the `LoanExtendDialog` constructor in `applications/vlms/src/ui/views/LoanExtendDialog.h`. If it takes more than the loan record, pass the same extra arguments the existing extend tests in `test_loan_dialogs.cpp` pass.

Create `applications/vlms/test/src/test_plain_text.cpp`:

```cpp
#include "ui/views/UiHelpers.h"

#include <QLabel>
#include <QMessageBox>
#include <QWidget>

#include <gtest/gtest.h>

// The detail values beside every list (title, author, notes...) come from this
// one factory; the label variant must not render markup.
TEST(PlainText, DetailValueLabelsShowMarkupAsTyped)
{
    QWidget parent;
    QWidget* value = VLMS::makeDetailValueWidget(&parent, false);
    auto* label = qobject_cast<QLabel*>(value);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->textFormat(), Qt::PlainText);
}

// Every message box passes through localizeMessageBox; confirmations quote
// titles and names.
TEST(PlainText, MessageBoxesShowMarkupAsTyped)
{
    QMessageBox box(QMessageBox::Question, QStringLiteral("t"),
                    QStringLiteral("<b>Delete</b> this?"));
    VLMS::localizeMessageBox(&box);
    EXPECT_EQ(box.textFormat(), Qt::PlainText);
}
```

Add `        src/test_plain_text.cpp` to the `test_vlms_ui` SRC list as stated above. If `makeDetailValueWidget` or `localizeMessageBox` is not declared in `UiHelpers.h`, declare it there with the signature used in `UiHelpers.cpp`.

- [ ] **Step 2: Run and watch them fail**

Run: `cmake --build build -j4 --target test_vlms_ui && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R 'ShowsMarkupAsTyped|ShowMarkupAsTyped|MarkupAsTyped'`
Expected: all five fail on `textFormat()` being `Qt::AutoText`.

- [ ] **Step 3: Implement**

`UiHelpers.cpp`, `makeDetailValueWidget`, in the `QLabel` branch after `label->setObjectName(QStringLiteral("bookDetailValue"));` add:

```cpp
    // Library data: a title that looks like HTML is shown as typed.
    label->setTextFormat(Qt::PlainText);
```

`UiHelpers.cpp`, `localizeMessageBox`, directly after the `if (box == nullptr) { return; }` block add:

```cpp
    // Messages quote titles and names; markup in them is shown as typed.
    box->setTextFormat(Qt::PlainText);
```

`LoanReturnDialog.cpp`, after `summary->setObjectName(QStringLiteral("loanReturnSummary"));`, and `LoanExtendDialog.cpp`, after `summary->setObjectName(QStringLiteral("loanExtendSummary"));`, add:

```cpp
    summary->setTextFormat(Qt::PlainText);
```

`LoanCheckoutDialog.cpp`, after `m_memberFixedLabel = new QLabel(m_scope.label, this);` add:

```cpp
        m_memberFixedLabel->setTextFormat(Qt::PlainText);
```

- [ ] **Step 4: Full suite**

Run: `cmake --build build -j4 && QT_QPA_PLATFORM=offscreen ctest --test-dir build -j4 --no-tests=error -LE 'realdb|bench'`
Expected: 100 % passed.

- [ ] **Step 5: Commit**

```bash
git add applications/vlms
git commit -m "Show titles and names that look like HTML as typed." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: An image copied in by a save is swapped in only after the save commits

Today `MemberRepository::storeMemberImage` and `CatalogRepository::applyCoverImage` copy the new image over the old one inside the save's transaction. If a later step of the same save fails, the transaction rolls back the database but the file has already been replaced. Example: a member save with a new photo and an ID image that no longer exists replaces the photo on disk, then fails with `error.image.missingFile`.

**Files:**
- Create: `libraries/Repositories/src/StagedFiles.h`
- Create: `libraries/Repositories/src/StagedFiles.cpp`
- Modify: `libraries/Repositories/CMakeLists.txt` (add `src/StagedFiles.h` and `src/StagedFiles.cpp` after `src/CategoryStore.cpp`)
- Modify: `libraries/Repositories/include/VLMS/Repositories/MemberRepository.h` (private `storeMemberImage`)
- Modify: `libraries/Repositories/include/VLMS/Repositories/CatalogRepository.h` (private `applyCoverImage`)
- Modify: `libraries/Repositories/src/MemberRepository.cpp`
- Modify: `libraries/Repositories/src/CatalogRepository.cpp`
- Test: `libraries/Repositories/test/src/test_member_repository.cpp`, `libraries/Repositories/test/src/test_catalog_repository.cpp`

**Interfaces:**
- Produces (internal to the Repositories library): `class VLMS::Repositories::StagedFiles` with `bool stage(const std::filesystem::path& source, const std::filesystem::path& target)`, `bool commit()`, `void discard()`, destructor discards.

- [ ] **Step 1: Write the failing tests**

Append to `libraries/Repositories/test/src/test_member_repository.cpp`, after the existing `AFailedPhotoReplacementKeepsTheOldPhoto` test (the helpers `readWhole` and `writeSampleImage` already exist in that file):

```cpp
// The photo step succeeds, then the ID image is refused and the transaction
// rolls back. The photo on disk must be the one the record still points at.
TEST_F(test_core_MemberRepository, ARolledBackSaveKeepsTheOldPhoto)
{
    const MemberSeed seed = uniqueMemberSeed(61);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_repository->setPhotoImage(id, writeSampleImage("old.png")));
    const auto before = m_repository->getMember(id);
    ASSERT_TRUE(before.has_value());
    const std::filesystem::path stored = m_repository->resolveImagePath(before->photoPath);
    const std::string oldBytes = readWhole(stored);
    ASSERT_FALSE(oldBytes.empty());

    // Same extension as the photo on record, so the copy targets that very file.
    const std::filesystem::path root(m_db->rootDirectory());
    std::ofstream(root / "new.png", std::ios::binary) << "new";

    Repositories::MemberWrite write;
    write.member = seed.toInput();
    write.photoSourcePath = (root / "new.png").string();
    write.idImageSourcePath = (root / "missing.png").string();  // error.image.missingFile
    EXPECT_FALSE(m_repository->saveExistingMember(id, write));

    const auto after = m_repository->getMember(id);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->photoPath, before->photoPath);
    EXPECT_EQ(readWhole(stored), oldBytes) << "a rolled-back save must not change the photo";
    std::filesystem::path part = stored;
    part += ".part";
    EXPECT_FALSE(std::filesystem::exists(part)) << "a rolled-back save must not leave its copy";
}

TEST_F(test_core_MemberRepository, ACommittedSaveSwapsInTheNewPhotoAndLeavesNoCopy)
{
    const MemberSeed seed = uniqueMemberSeed(62);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_repository->setPhotoImage(id, writeSampleImage("old.png")));
    const std::filesystem::path root(m_db->rootDirectory());
    std::ofstream(root / "new.png", std::ios::binary) << "new";

    Repositories::MemberWrite write;
    write.member = seed.toInput();
    write.photoSourcePath = (root / "new.png").string();
    ASSERT_TRUE(m_repository->saveExistingMember(id, write));

    const auto after = m_repository->getMember(id);
    ASSERT_TRUE(after.has_value());
    const std::filesystem::path stored = m_repository->resolveImagePath(after->photoPath);
    EXPECT_EQ(readWhole(stored), "new");
    std::filesystem::path part = stored;
    part += ".part";
    EXPECT_FALSE(std::filesystem::exists(part));
}
```

Append to `libraries/Repositories/test/src/test_catalog_repository.cpp`, after `AFailedCoverReplacementKeepsTheOldCover`:

```cpp
TEST_F(test_core_CatalogRepository, ACommittedBookSaveSwapsInTheCoverAndLeavesNoCopy)
{
    const std::filesystem::path root(m_db->rootDirectory());
    std::ofstream(root / "cover.png", std::ios::binary) << "cover";

    Repositories::BookWrite write;
    write.book = uniqueBookSeed(96).toInput();
    write.coverSourcePath = (root / "cover.png").string();
    const auto created = m_repository->saveNewBook(write);
    ASSERT_TRUE(created) << created.error().key;

    const auto book = m_repository->getBook(created.value());
    ASSERT_TRUE(book.has_value());
    const std::filesystem::path stored = m_repository->resolveCoverPath(book->coverImagePath);
    std::ifstream in(stored, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()),
              "cover");
    std::filesystem::path part = stored;
    part += ".part";
    EXPECT_FALSE(std::filesystem::exists(part));
}
```

- [ ] **Step 2: Run and watch them fail**

Run: `cmake --build build -j4 --target test_vlms_repositories && QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R 'test_vlms_repositories$'`
Expected: `ARolledBackSaveKeepsTheOldPhoto` fails, because the photo bytes changed to `new`. The two "committed" tests pass already; they guard the new code path.

- [ ] **Step 3: Create `StagedFiles`**

`libraries/Repositories/src/StagedFiles.h`:

```cpp
#pragma once

#include <filesystem>
#include <utility>
#include <vector>

namespace VLMS::Repositories {

/// Image files copied in during a save. Each copy lands beside its target as
/// `<target>.part` and replaces the target only in commit(), which the caller
/// runs once the save's transaction has committed. A save that fails or is
/// rolled back leaves every target as it was: discard(), or the destructor,
/// removes the copies.
class StagedFiles {
public:
    StagedFiles() = default;
    StagedFiles(const StagedFiles&) = delete;
    StagedFiles& operator=(const StagedFiles&) = delete;
    ~StagedFiles();

    /// Copies `source` to `target` + ".part". False when the copy failed;
    /// nothing is left behind then.
    [[nodiscard]] bool stage(const std::filesystem::path& source,
                             const std::filesystem::path& target);

    /// Renames every staged copy over its target. False when a swap failed;
    /// a copy that could not be swapped is removed.
    [[nodiscard]] bool commit();

    /// Removes every staged copy. The targets are not touched.
    void discard();

private:
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> m_files;  // part, target
};

}  // namespace VLMS::Repositories
```

`libraries/Repositories/src/StagedFiles.cpp`:

```cpp
#include "StagedFiles.h"

namespace VLMS::Repositories {

namespace {

std::filesystem::path partFor(const std::filesystem::path& target)
{
    std::filesystem::path part = target;
    part += ".part";
    return part;
}

}  // namespace

StagedFiles::~StagedFiles()
{
    discard();
}

bool StagedFiles::stage(const std::filesystem::path& source, const std::filesystem::path& target)
{
    const std::filesystem::path part = partFor(target);
    std::error_code error;
    std::filesystem::remove(part, error);
    if (!std::filesystem::copy_file(source, part, error)) {
        std::filesystem::remove(part, error);
        return false;
    }
    m_files.emplace_back(part, target);
    return true;
}

bool StagedFiles::commit()
{
    bool swapped = true;
    for (const auto& [part, target] : m_files) {
        std::error_code error;
        std::filesystem::rename(part, target, error);
        if (error) {
            // A rename that will not replace an existing file. The new image is
            // complete beside it by now, so the swap itself is all that is at risk.
            std::filesystem::remove(target, error);
            std::filesystem::rename(part, target, error);
        }
        if (error) {
            std::filesystem::remove(part, error);
            swapped = false;
        }
    }
    m_files.clear();
    return swapped;
}

void StagedFiles::discard()
{
    for (const auto& entry : m_files) {
        std::error_code ignored;
        std::filesystem::remove(entry.first, ignored);
    }
    m_files.clear();
}

}  // namespace VLMS::Repositories
```

Add `    src/StagedFiles.h` and `    src/StagedFiles.cpp` to `add_library(...)` in `libraries/Repositories/CMakeLists.txt`, after `    src/CategoryStore.cpp`.

- [ ] **Step 4: Use it in `MemberRepository`**

`MemberRepository.h`: before `class MemberRepository` (inside `namespace VLMS::Repositories`), add `class StagedFiles;`. Change the private declaration

```cpp
    [[nodiscard]] Core::Status storeMemberImage(std::int64_t memberId,
                                                const std::string& sourceFilePath,
                                                ImageSlot slot);
```

to

```cpp
    [[nodiscard]] Core::Status storeMemberImage(std::int64_t memberId,
                                                const std::string& sourceFilePath,
                                                ImageSlot slot,
                                                StagedFiles& staged);
```

`MemberRepository.cpp`:
1. Add `#include "StagedFiles.h"` with the other local includes.
2. Delete the function `replaceWithCopy` and its doc comment from the anonymous namespace at the top of the file.
3. In `storeMemberImage`, change the signature the same way as the header, and replace

```cpp
    if (!replaceWithCopy(source, targetPath)) {
        return RepoSql::validation("error.image.copyFailed");
    }
```

with

```cpp
    if (!staged.stage(source, targetPath)) {
        return RepoSql::validation("error.image.copyFailed");
    }
```

4. In `saveNewMember`: declare `StagedFiles staged;` on the line before `const Core::Status work = m_session.transaction([&] {`. Pass `staged` as the fourth argument of both `storeMemberImage` calls. Replace the tail

```cpp
    if (!work) {
        return Core::Result<std::int64_t>::fail(work.error().kind, work.error().key, work.error().detail);
    }
    return Core::Result<std::int64_t>::ok(memberId);
```

with

```cpp
    if (!work) {
        return Core::Result<std::int64_t>::fail(work.error().kind, work.error().key, work.error().detail);
    }
    // Only now that the row is committed may the copies replace the files.
    if (!staged.commit()) {
        return RepoSql::validationResult<std::int64_t>("error.image.copyFailed");
    }
    return Core::Result<std::int64_t>::ok(memberId);
```

5. In `saveExistingMember`: declare `StagedFiles staged;` next to `std::vector<std::string> clearedPaths;`. Pass `staged` to both `storeMemberImage` calls. Directly after the transaction, before the comment `// Only after the commit: a save that failed must not have lost the file.`, insert

```cpp
    if (saved && !staged.commit()) {
        return RepoSql::validation("error.image.copyFailed");
    }
```

6. Replace the bodies of `setPhotoImage` and `setIdImage`:

```cpp
Core::Status MemberRepository::setPhotoImage(const std::int64_t memberId,
                                             const std::string& sourceFilePath)
{
    StagedFiles staged;
    if (const auto stored = storeMemberImage(memberId, sourceFilePath, ImageSlot::Photo, staged);
        !stored) {
        return stored;
    }
    return staged.commit() ? Core::Status::ok() : RepoSql::validation("error.image.copyFailed");
}

Core::Status MemberRepository::setIdImage(const std::int64_t memberId, const std::string& sourceFilePath)
{
    StagedFiles staged;
    if (const auto stored = storeMemberImage(memberId, sourceFilePath, ImageSlot::IdCard, staged);
        !stored) {
        return stored;
    }
    return staged.commit() ? Core::Status::ok() : RepoSql::validation("error.image.copyFailed");
}
```

If `RepoSql::validationResult` does not exist in `libraries/Repositories/src/RepoSql.h`, use the same construction `CatalogRepository::insertBookRow` uses for `error.book.titleRequired`.

- [ ] **Step 5: Use it in `CatalogRepository`**

`CatalogRepository.h`: add `class StagedFiles;` before `class CatalogRepository`, and change the private declaration of `applyCoverImage` to take `StagedFiles& staged` as a third parameter.

`CatalogRepository.cpp`:
1. Add `#include "StagedFiles.h"`; delete `replaceWithCopy` and its comment from the anonymous namespace.
2. `applyCoverImage(const std::int64_t bookId, const std::string& sourceFilePath, StagedFiles& staged)`: replace `if (!replaceWithCopy(source, targetPath)) {` with `if (!staged.stage(source, targetPath)) {`.
3. `setCoverImage`:

```cpp
Core::Status CatalogRepository::setCoverImage(const std::int64_t bookId, const std::string& sourceFilePath)
{
    StagedFiles staged;
    if (const auto cover = applyCoverImage(bookId, sourceFilePath, staged); !cover) {
        return cover;
    }
    return staged.commit() ? Core::Status::ok() : RepoSql::validation("error.cover.copyFailed");
}
```

4. `saveNewBook`: declare `StagedFiles staged;` before the transaction, pass it to `applyCoverImage`, and after the `if (!work) { ... }` block insert

```cpp
    if (!staged.commit()) {
        return RepoSql::validationResult<std::int64_t>("error.cover.copyFailed");
    }
```

5. `saveExistingBook`: change `return m_session.transaction([&] {` to `StagedFiles staged;` plus `const Core::Status saved = m_session.transaction([&] {`, pass `staged` to `applyCoverImage`, and end the function with

```cpp
    if (!saved) {
        return saved;
    }
    return staged.commit() ? Core::Status::ok() : RepoSql::validation("error.cover.copyFailed");
```

- [ ] **Step 6: Run the tests**

Run: `cmake --build build -j4 && QT_QPA_PLATFORM=offscreen ctest --test-dir build -j4 --no-tests=error -LE 'realdb|bench'`
Expected: 100 % passed, including `ARolledBackSaveKeepsTheOldPhoto`, `AFailedPhotoReplacementKeepsTheOldPhoto` and `AFailedCoverReplacementKeepsTheOldCover`.

- [ ] **Step 7: Commit**

```bash
git add libraries/Repositories
git commit -m "Swap in a saved image only after the save has committed." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The installer backup includes the images

The update page's backup copies only `vlms.db`. Choosing *Delete it, including its database and cover images* then removes every member photo, identity-card image and cover with no copy anywhere. This cannot be compiled or run on Linux; the controller verifies it with the Windows installer workflow after the task.

**Files:**
- Modify: `installer/vlms.iss`
- Modify: `SECURITY.md`
- Modify: `docs/windows-installer.md`

- [ ] **Step 1: Add a recursive copy** — in `installer/vlms.iss`, directly before `function BackupExistingDatabase: Boolean;`, add:

```pascal
{ Copies every file under Source into Dest, keeping the folder layout.
  False at the first file or folder that cannot be copied. }
function CopyTree(const Source, Dest: String): Boolean;
var
  FindRec: TFindRec;
begin
  Result := ForceDirectories(Dest);
  if not Result then
    Exit;
  if not FindFirst(Source + '\*', FindRec) then
    Exit;
  try
    repeat
      if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
      begin
        if FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY <> 0 then
          Result := CopyTree(Source + '\' + FindRec.Name, Dest + '\' + FindRec.Name)
        else
          Result := FileCopy(Source + '\' + FindRec.Name, Dest + '\' + FindRec.Name, False);
      end;
    until (not Result) or (not FindNext(FindRec));
  finally
    FindClose(FindRec);
  end;
end;
```

- [ ] **Step 2: Back up `resources` with the database**

In the `[Code]` variable block, on the line after `  BackupPath: String;            { set once the copy has been made, for the final message }`, add:

```pascal
  ResourcesBackupPath: String;   { the images' copy, beside BackupPath }
```

Rename `function BackupExistingDatabase: Boolean;` to `function BackupExistingData: Boolean;`, and update its single caller in `PrepareToInstall` (`if not BackupExistingDatabase then` becomes `if not BackupExistingData then`). At the end of the function, after the `-journal` copy, add:

```pascal

  { The photographs, identity-card images and covers are the library's records
    too, and deleting the old installation removes them with it. }
  if DirExists(PrevDir + '\resources') then
  begin
    ResourcesBackupPath := DestDir + '\vlms-' + Tag + '-' + Stamp + '-resources';
    if not CopyTree(PrevDir + '\resources', ResourcesBackupPath) then
    begin
      ResourcesBackupPath := '';
      Result := False;
      Exit;
    end;
  end;
```

- [ ] **Step 3: Say so in the wizard**

In `installer/vlms.iss` replace these exact strings:
- `'Copy the existing database to Documents\VLMS Backups first'` with `'Copy the existing database and images to Documents\VLMS Backups first'`
- `'Copy the database to Documents\VLMS Backups first'` with `'Copy the database and images to Documents\VLMS Backups first'`
- `'Setup could not copy the existing database to'` with `'Setup could not copy the existing database and images to'`

In `CurPageChanged`, replace

```pascal
  if (CurPageID = wpFinished) and (BackupPath <> '') then
    WizardForm.FinishedLabel.Caption :=
      WizardForm.FinishedLabel.Caption + #13#10#13#10 +
      'The previous database was copied to:' + #13#10 + BackupPath;
```

with

```pascal
  if (CurPageID = wpFinished) and (BackupPath <> '') then
  begin
    WizardForm.FinishedLabel.Caption :=
      WizardForm.FinishedLabel.Caption + #13#10#13#10 +
      'The previous database was copied to:' + #13#10 + BackupPath;
    if ResourcesBackupPath <> '' then
      WizardForm.FinishedLabel.Caption :=
        WizardForm.FinishedLabel.Caption + #13#10#13#10 +
        'Its images were copied to:' + #13#10 + ResourcesBackupPath;
  end;
```

- [ ] **Step 4: Update the docs**

`SECURITY.md`:
- Replace the bullet starting `- **The installer offers a second copy.**` with:
  `- **The installer offers a second copy.** On an update it copies \`vlms.db\` and the \`resources\` folder (photographs, identity-card images and covers) to \`Documents\VLMS Backups\` unless you untick the box.`
- Replace the bullet starting `- **No image backups.**` with:
  `- **Images are copied only on an update.** The copy made before a schema change holds the database only, and nothing copies the images between updates. Back up the installation folder yourself (see below).`

`docs/windows-installer.md`:
- In the paragraph starting `Either way, the checkbox on that page copies the existing \`vlms.db\``, change `copies the existing \`vlms.db\` (plus any \`-wal\`/\`-journal\` beside it) to \`Documents\VLMS Backups\` first.` to `copies the existing \`vlms.db\` (plus any \`-wal\`/\`-journal\` beside it) and its \`resources\` folder to \`Documents\VLMS Backups\` first.`
- In *Testing an update by hand*, step 5: change `Check \`Documents\VLMS Backups\` now holds a \`vlms.db\`.` to `Check \`Documents\VLMS Backups\` now holds a \`vlms-*.db\` and a \`vlms-*-resources\` folder with the three images from step 3.`
- Step 9: replace the sentence `Those are not in \`Documents\VLMS Backups\`: the backup holds the database only.` with `Their copies are in the \`vlms-*-resources\` folder in \`Documents\VLMS Backups\` from step 4.`

Read each target sentence with `grep -n` first. If the wording differs slightly, keep the meaning and replace the sentence that is there.

- [ ] **Step 5: Check and commit**

Run: `grep -n -E 'CopyTree|ResourcesBackupPath|BackupExistingData' installer/vlms.iss` and confirm every use is defined and the old name `BackupExistingDatabase` no longer appears.

```bash
git add installer/vlms.iss SECURITY.md docs/windows-installer.md
git commit -m "Copy the images with the database before an update." -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

## Deviations approved during execution

- Task 2 uses `heldByAnotherCopy()`: only a lock held by another running copy counts, other lock failures fall through to the database error, and a lock written by another computer is taken over.
- Task 4 swaps the images inside the transaction, keeping `<target>.old` until the commit and restoring it if the swap or the commit fails.
