# Frontend Redesign Phase 4: Dialogs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish the dialog boundary so form dialogs take `Lookups` (not repositories), wrap OCR in `OcrService`, and replace `BookLoansDialog` / `MemberLoansDialog` / `ArchiveLoansDialog` with one `LoanHistoryDialog` built on `ListPage` + pinned `LoanListSpec` + `LoanWorkflow`.

**Architecture:** Lookups interfaces live under `ui/presentation/` (read-only) with Qt implementations that call repositories. `OcrService` under `ui/services/` owns `Ocr::Job` (replacing `BookOcrController`). `QtDialogFactory` already returns forms; dialogs are updated to accept Lookups/`OcrService` and no longer store repository references. Workflows open loan history through `LoanHistoryDialog` instead of the three legacy dialogs.

**Tech Stack:** C++17, Qt 6 Widgets, CMake ≥ 3.21, GoogleTest, `VLMS::Ocr`, Phases 1–3 UI (`DialogFactory`, workflows, `ListPage`, `LoanListSpec`).

**Spec:** `docs/superpowers/specs/2026-09-29-frontend-redesign-design.md` (Phase 4).

**Branch:** `redesign/4-dialogs` (from `Beta` after Phase 3 merges).

**Worktree:** `<repo>/.worktrees/redesign-1-foundations` (switch/create Phase 4 branch at execution).

## Global Constraints

- Dependencies point downwards only. Dialogs never hold a repository; they receive a Lookups interface (and `OcrService&` for the book editor).
- Specs only read. Only workflows write.
- Field validation stays in the dialog before OK (`firstValidationFailure`, `LoanPolicy::validate*`); repository refusals stay in workflows via `ErrorPresenter`.
- `LoanHistoryDialog` is a `ListPage` on a `LoanListSpec` with `QueryState::pinned` set, using `LoanWorkflow` for checkout/return/extend.
- Backend libraries and schema do not change. No `LanguageChange` / folder moves (Phase 5). No manual screenshots (Phase 6).
- Delete old tests for removed dialogs in the same PR that adds replacements.
- Commits in task steps are optional for SDD hygiene.

---

## File map

| Path | Responsibility |
|---|---|
| `applications/vlms/src/ui/presentation/CatalogLookups.h` | Interface: authors, publishers, categories, languages, free local numbers, suggested copy ids |
| `applications/vlms/src/ui/presentation/CirculationLookups.h` | Interface: borrowable members, available copies |
| `applications/vlms/src/ui/presentation/MemberLookups.h` | Interface: suggested membership number, cities |
| `applications/vlms/src/ui/presentation/QtCatalogLookups.h` / `.cpp` | Implements CatalogLookups via CatalogRepository |
| `applications/vlms/src/ui/presentation/QtCirculationLookups.h` / `.cpp` | Implements CirculationLookups via CirculationRepository (+ catalog/members as today) |
| `applications/vlms/src/ui/presentation/QtMemberLookups.h` / `.cpp` | Implements MemberLookups via MemberRepository |
| `applications/vlms/src/ui/services/OcrService.h` / `.cpp` | Wraps `Ocr::Job` + progress dialog (port of BookOcrController) |
| `applications/vlms/src/ui/catalog/BookEditorDialog.h` / `.cpp` | Takes `CatalogLookups&` + `OcrService&`; no CatalogRepository member |
| `applications/vlms/src/ui/catalog/BookCopiesTable.*` / delegates | Use lookups for free numbers / suggestions where they currently call the repo |
| `applications/vlms/src/ui/catalog/CategoryManagerDialog.*` | **Decision:** `QtDialogFactory::manageCategories` keeps opening `CategoryManagerDialog` with `CatalogRepository&` for category CRUD (`listAllCategories` / `createCategory` / `updateCategory` / `deleteCategory`). That is the only dialog allowed to hold a repository in Phase 4, because category writes are already scoped inside the factory method and are not part of Lookups. `BookEditorDialog` must not receive or store a repository. |
| `applications/vlms/src/ui/members/MemberEditorDialog.*` | Takes `MemberLookups&` |
| `applications/vlms/src/ui/circulation/LoanCheckoutDialog.*` | Takes `CirculationLookups&` |
| `applications/vlms/src/ui/circulation/LoanReturnDialog.*` / `LoanExtendDialog.*` | No repo today beyond loan record — confirm; strip any repo if present |
| `applications/vlms/src/ui/LoanHistoryDialog.h` / `.cpp` | New; replaces three loan dialogs (lives under `ui/` until Phase 5 `ui/views/`) |
| `applications/vlms/src/ui/workflows/BookWorkflow.cpp` / `MemberWorkflow.cpp` / `ArchiveWorkflow.cpp` | Open `LoanHistoryDialog` |
| `applications/vlms/src/ui/workflows/QtDialogFactory.*` | Pass Lookups/OcrService into dialogs; Application/MainWindow owns lookup instances |
| `applications/vlms/src/Application.*` or `MainWindow.*` | Construct Lookups + OcrService |
| Delete | `BookLoansDialog.*`, `MemberLoansDialog.*`, `ArchiveLoansDialog.*`, `BookOcrController.*`, `LoanHistoryActions.*` (if fully absorbed) |
| Tests | `test_book_editor_lookups.cpp`, `test_loan_history_dialog.cpp`, update OCR/editor/checkout tests; remove three loan-dialog page tests' legacy includes |

---

### Task 1: CatalogLookups + QtCatalogLookups

**Files:**
- Create: `applications/vlms/src/ui/presentation/CatalogLookups.h`
- Create: `applications/vlms/src/ui/presentation/QtCatalogLookups.h`
- Create: `applications/vlms/src/ui/presentation/QtCatalogLookups.cpp`
- Create: `applications/vlms/test/src/test_catalog_lookups.cpp`
- Modify: `applications/vlms/CMakeLists.txt`, `applications/vlms/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `CatalogRepository` (implementation only)
- Produces:

```cpp
namespace VLMS {

class CatalogLookups {
public:
    virtual ~CatalogLookups() = default;

    virtual QStringList authorNames() const = 0;
    virtual QStringList publisherNames() const = 0;
    virtual std::vector<Repositories::CategoryRecord> categories() const = 0;
    virtual std::vector<Repositories::LanguageRecord> bookLanguages(
        Repositories::ArchiveScope scope = Repositories::ArchiveScope::Live) const = 0;
    virtual QStringList freeLocalNumbers(const QString& source, int limit) const = 0;
    virtual void suggestCopyIdentifiers(const QString& language,
                                        QString* outSource,
                                        QString* outLocalId,
                                        QString* outGlobalCopyId) const = 0;
    virtual QString resolveCoverPath(const QString& storedPath) const = 0;
};

class QtCatalogLookups final : public CatalogLookups {
public:
    explicit QtCatalogLookups(Repositories::CatalogRepository& catalog);
    // ... overrides forwarding to repository, converting std::string ↔ QString
};

}
```

- [ ] **Step 1: Write failing test**

```cpp
TEST(CatalogLookups, AuthorNamesForwardFromRepository)
{
    // UiTest / temp DB with one known author
    VLMS::QtCatalogLookups lookups(catalog);
    const QStringList names = lookups.authorNames();
    EXPECT_TRUE(names.contains(QStringLiteral("Fixture Author")));
}

TEST(CatalogLookups, SuggestCopyIdentifiersReturnsNonEmptyLocalId)
{
    VLMS::QtCatalogLookups lookups(catalog);
    QString source, localId, globalId;
    lookups.suggestCopyIdentifiers(QStringLiteral("ar"), &source, &localId, &globalId);
    EXPECT_FALSE(localId.isEmpty());
}
```

- [ ] **Step 2: Run — Expected: FAIL (header missing)**
- [ ] **Step 3: Implement interface + Qt wrapper; register CMake**
- [ ] **Step 4: Run — Expected: PASS**
- [ ] **Step 5: Commit (optional)** `feat(ui): add CatalogLookups`

---

### Task 2: MemberLookups + CirculationLookups

**Files:**
- Create: `MemberLookups.h`, `QtMemberLookups.h` / `.cpp`
- Create: `CirculationLookups.h`, `QtCirculationLookups.h` / `.cpp`
- Create: `test_member_lookups.cpp`, `test_circulation_lookups.cpp`
- Modify: CMake lists

**Produces:**

```cpp
class MemberLookups {
public:
    virtual ~MemberLookups() = default;
    virtual QString suggestMembershipNumber() const = 0;
    virtual QStringList cities(Repositories::ArchiveScope scope =
                                   Repositories::ArchiveScope::Live) const = 0;
    virtual QString resolveImagePath(const QString& storedPath) const = 0;
};

class CirculationLookups {
public:
    virtual ~CirculationLookups() = default;
    virtual std::vector<Repositories::LoanMemberOption> borrowableMembers(
        const QString& search = {}) const = 0;
    virtual std::vector<Repositories::LoanCopyOption> availableCopies(
        const QString& search = {}, qint64 bookId = 0) const = 0;
};
```

Implement `QtCirculationLookups` by forwarding to:

```cpp
m_circulation.listBorrowableMembers(ss(search));
m_circulation.listAvailableCopies(ss(search), bookId);
```

(`CirculationRepository.h` lines 44–47.)

- [ ] **Step 1: Write failing tests for both**
- [ ] **Step 2: Implement**
- [ ] **Step 3: PASS**
- [ ] **Step 4: Commit (optional)** `feat(ui): add MemberLookups and CirculationLookups`

---

### Task 3: OcrService (replace BookOcrController)

**Files:**
- Create: `applications/vlms/src/ui/services/OcrService.h`
- Create: `applications/vlms/src/ui/services/OcrService.cpp`
- Modify: `applications/vlms/src/ui/catalog/BookEditorDialog.h` / `.cpp` — take `OcrService&` (or construct service with dialog parent)
- Delete: `BookOcrController.h` / `.cpp`
- Modify: `test_book_editor_ocr.cpp` — expect `OcrService` / same signals
- Modify: CMake lists; keep PRIVATE link to `VLMS::Ocr` on `vlms_ui`

**Produces:**

```cpp
namespace VLMS {

class OcrService final : public QObject {
    Q_OBJECT
public:
    explicit OcrService(QObject* parent = nullptr);
    ~OcrService() override;

    /// Starts OCR for the current cover image path held by the editor.
    void start(QWidget* dialogParent, const QString& imagePath, const QString& languages);

signals:
    void textReady(const QString& text);

private:
    void poll();
    void finish();

    std::unique_ptr<Ocr::Job> m_job;
    QProgressDialog* m_progress = nullptr;
    QTimer* m_pollTimer = nullptr;
};

}
```

Port polling / progress behaviour verbatim from `BookOcrController.cpp`. BookEditorDialog stores `OcrService*` or references an injected service; call `start(this, coverPath, languages)` where it currently calls `m_ocr->start(languages)`.

- [ ] **Step 1: Write/adjust OCR test to use OcrService API**
- [ ] **Step 2: Implement OcrService; switch BookEditorDialog; delete BookOcrController**
- [ ] **Step 3:** `ctest -R 'Ocr|BookEditor' --output-on-failure` — PASS
- [ ] **Step 4: Grep** — no `BookOcrController`
- [ ] **Step 5: Commit (optional)** `feat(ui): replace BookOcrController with OcrService`

---

### Task 4: BookEditorDialog + copies UI take CatalogLookups

**Files:**
- Modify: `BookEditorDialog.h` / `.cpp`
- Modify: `FreeLocalNumberDelegate.*`, `LocalNumberDelegate.*`, `BookCopiesTable.*` as needed so free-number / suggest paths call Lookups
- Modify: `QtDialogFactory.cpp` — construct dialogs with `CatalogLookups&`
- Modify: Application/MainWindow — own `QtCatalogLookups`
- Modify: tests constructing `BookEditorDialog`

Constructor change:

```cpp
// before
BookEditorDialog(Repositories::CatalogRepository& repository, QWidget* parent = nullptr);
// after
BookEditorDialog(CatalogLookups& lookups, OcrService& ocr, QWidget* parent = nullptr);
BookEditorDialog(CatalogLookups& lookups, OcrService& ocr,
                 const Repositories::BookRecord& book, QWidget* parent = nullptr);
```

Completers for author/publisher use `lookups.authorNames()` / `publisherNames()`. Category combo uses `lookups.categories()`. Cover resolve uses `lookups.resolveCoverPath`.

`QtDialogFactory::addBook` / `editBook` still build `BookForm` from dialog outputs; factory holds lookups reference (or receives from Application).

- [ ] **Step 1: Update dialog + factory + tests**
- [ ] **Step 2: Build and run editor/lookup tests — PASS**
- [ ] **Step 3: Grep BookEditorDialog for CatalogRepository — must not store a repo member**
- [ ] **Step 4: Commit (optional)** `refactor(ui): BookEditorDialog uses CatalogLookups`

---

### Task 5: MemberEditorDialog + LoanCheckoutDialog take Lookups

**Files:**
- Modify: `MemberEditorDialog.*` — `MemberLookups&`; `suggestMembershipNumber()` from lookups
- Modify: `LoanCheckoutDialog.*` — `CirculationLookups&`; member/copy combos from lookups
- Modify: `QtDialogFactory.*` + owners
- Modify: `test_loan_dialogs.cpp`, `test_member_editor_status.cpp`, etc.

```cpp
explicit MemberEditorDialog(MemberLookups& lookups, QWidget* parent = nullptr);
MemberEditorDialog(MemberLookups& lookups, const Repositories::MemberRecord& member,
                   QWidget* parent = nullptr);

explicit LoanCheckoutDialog(CirculationLookups& lookups, const LoanForm& prefill,
                            QWidget* parent = nullptr);
```

Return/Extend dialogs: if they only edit dates/notes from a `LoanRecord`, leave them repo-free (already). Confirm with grep.

- [ ] **Step 1: Update dialogs, factory, tests**
- [ ] **Step 2: `ctest -R 'MemberEditor|Loan|Dialog' --output-on-failure` — PASS**
- [ ] **Step 3: Grep** editors/checkout for `Repository&` members — none on those dialogs
- [ ] **Step 4: Commit (optional)** `refactor(ui): member and checkout dialogs use Lookups`

---

### Task 6: LoanHistoryDialog

**Files:**
- Create: `applications/vlms/src/ui/LoanHistoryDialog.h`
- Create: `applications/vlms/src/ui/LoanHistoryDialog.cpp`
- Create: `applications/vlms/test/src/test_loan_history_dialog.cpp`
- Modify: `BookWorkflow::showLoans`, `MemberWorkflow::showLoans`, `ArchiveWorkflow::showLoans`
- Delete: `BookLoansDialog.*`, `MemberLoansDialog.*`, `ArchiveLoansDialog.*`
- Delete or gut: `LoanHistoryActions.*` if its buttons become PageActions on the history ListPage
- Modify: CMake + tests (`test_catalog_loans.cpp`, `test_member_loans_actions.cpp`, …)

**Produces:**

```cpp
namespace VLMS {

class LoanHistoryDialog final : public QDialog {
    Q_OBJECT
public:
    enum class Kind { Book, Member, ArchivedMember, ArchivedBook, ArchivedCopy };

    LoanHistoryDialog(LoanListSpec& loansSpec,
                      LoanWorkflow& loans,
                      Kind kind,
                      qint64 pinnedId,
                      const QString& title,
                      Repositories::ArchiveScope scope,
                      QWidget* parent = nullptr);
};

}
```

Behaviour:

1. Build `QueryState` with `pinned` set (`bookId` / `memberId` / `copyId` from kind), `scope` Live or Archived/Any as today's archive dialog used.
2. Embed a `ListPage` (or a slimmed list without the full app chrome) bound to `LoanListSpec` with actions: checkout (prefill from pin), return, extend — closures call `LoanWorkflow`.
3. Window title uses existing string keys from the three dialogs (`catalog.loansTitle` / `members.loansTitle` / archive equivalents — reuse exact keys from deleted dialogs' `retranslateUi`).
4. On workflow success, `DataEvents` reloads the embedded model (already subscribed).

Workflow change sketch:

```cpp
Core::Status BookWorkflow::showLoans(QWidget* parent, qint64 bookId)
{
    const auto book = m_catalog.getBook(bookId);
    if (!book) { m_errors.show(book.error()); return Core::asStatus(book.error()); }
    LoanHistoryDialog dialog(m_loanListSpec, m_loanWorkflow, LoanHistoryDialog::Kind::Book,
                             bookId, qs(book->title), Repositories::ArchiveScope::Live, parent);
    dialog.exec();
    return Core::Status::ok();
}
```

`BookWorkflow` needs a `LoanListSpec&` (or a dedicated history spec instance). Application/MainWindow owns one `LoanListSpec` used by Circulation and, with temporary `pinned` state, by the dialog — **careful:** do not reuse the Circulation page's live query state. Prefer a second `LoanListSpec` instance for history dialogs, or have the dialog stack/save restore `QueryState` around `exec()`.

Tests:

```cpp
TEST(LoanHistoryDialog, BookPinSetsQueryStateBookId)
TEST(LoanHistoryDialog, MemberPinSetsQueryStateMemberId)
TEST(LoanHistoryDialog, ArchivedScopeUsesArchivedOrAny)
TEST(LoanHistoryDialog, ReturnActionCallsLoanWorkflow)  // Fake workflow or FakeDialogFactory
```

Use offscreen QApplication; avoid blocking `exec()` — call private/testable setup method or `show()` + process events.

- [ ] **Step 1: Write failing tests**
- [ ] **Step 2: Implement dialog; switch workflows; delete three dialogs + LoanHistoryActions if unused**
- [ ] **Step 3: Full `ctest -L ui` — PASS**
- [ ] **Step 4: Grep** — no `BookLoansDialog|MemberLoansDialog|ArchiveLoansDialog|LoanHistoryActions|BookOcrController`
- [ ] **Step 5: Commit (optional)** `feat(ui): unify loan history into LoanHistoryDialog`

---

### Task 7: Presentation/UI tests for Lookups in dialogs + checklist hooks

**Files:**
- Create or extend: `test_book_editor_lookups.cpp` (`BookEditorDialog.Lookups*`, `BookEditorDialog.Ocr*`)
- Ensure `LoanHistoryDialog.Book*`, `.Member*`, `.Archived*` names exist for Phase 6 checklist
- Modify: `test_vlms_ui` / `test_vlms_presentation` sources as appropriate

- [ ] **Step 1: Add named tests matching checklist rows 4, 5, 8, 12, 19**
- [ ] **Step 2: PASS**
- [ ] **Step 3: Commit (optional)** `test(ui): cover Lookups and LoanHistoryDialog use cases`

---

## Verification (phase exit)

```bash
cmake --build build -j$(nproc)
cd build && ctest -L ui --output-on-failure
git grep -nE 'BookLoansDialog|MemberLoansDialog|ArchiveLoansDialog|BookOcrController|LoanHistoryActions' applications/vlms/
# Dialogs that must not hold repositories:
git grep -n 'CatalogRepository\|MemberRepository\|CirculationRepository' \
  applications/vlms/src/ui/catalog/BookEditorDialog.* \
  applications/vlms/src/ui/members/MemberEditorDialog.* \
  applications/vlms/src/ui/circulation/LoanCheckoutDialog.*
```

Expected: tests PASS; first grep empty; second grep empty (CategoryManagerDialog may still appear under factory, not BookEditorDialog).

Open PR `redesign/4-dialogs` → `Beta`.

---

## Self-review (Phase 4)

| Spec item | Task |
|---|---|
| CatalogLookups / CirculationLookups / MemberLookups | 1–2 |
| OcrService wraps Ocr::Job | 3 |
| Dialogs take Lookups not repositories | 4–5 |
| Forms via DialogFactory (already) finished wiring | 4–5 |
| LoanHistoryDialog replaces three dialogs | 6 |
| Checklist 4, 5, 8, 12, 19 test names | 7 |

**Explicit exception:** `CategoryManagerDialog` still takes `CatalogRepository&` via `QtDialogFactory::manageCategories` only. Book/member/checkout editors and `LoanHistoryDialog` must not.
