# Frontend Redesign Phase 5: Harmonisation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish shared list action order/shortcuts/menus where Phase 3 left gaps, switch language updates to `QEvent::LanguageChange` (remove `languageChanged` + `retranslateUi` chain), and move UI sources into layer folders (`ui/shell`, `ui/views`, `ui/presentation`, `ui/workflows`, `ui/services`).

**Architecture:** `Application::setUiLocale` sends `LanguageChange` to every top-level widget; widgets re-read labels in `changeEvent`. `ListPage` refreshes column/action/facet titles and calls `spec->retranslate()`. Include paths and CMake entries follow the new folders. No behaviour features beyond harmonisation and the language path.

**Tech Stack:** C++17, Qt 6 Widgets (`QEvent::LanguageChange`), CMake ≥ 3.21, GoogleTest, Phases 1–4 tree.

**Spec:** `docs/superpowers/specs/2026-09-29-frontend-redesign-design.md` (Phase 5).

**Branch:** `redesign/5-harmonisation` (from `Beta` after Phase 4 merges).

**Worktree:** `<repo>/.worktrees/redesign-1-foundations`.

## Global Constraints

- Dependencies point downwards only after the move: shell → views → presentation → workflows → services → repositories.
- Remove `Application::languageChanged` and every `retranslateUi()` chain entry point used for locale switches. Keep private helpers that only rebuild chrome if renamed away from the old signal path.
- New `Strings::t()` keys only for new menu/shortcut labels that are not already covered.
- Do not invent features. Metrics tables stay `QTableWidget`. Backend unchanged.
- Update every `#include` and CMake path in the same tasks that move files.
- Commits optional per task for SDD hygiene.

---

## File map (target layout)

| New path | Moved from | Responsibility |
|---|---|---|
| `applications/vlms/src/ui/shell/Application.h` / `.cpp` | `src/Application.*` | Composition root |
| `applications/vlms/src/ui/shell/MainWindow.h` / `.cpp` | `ui/MainWindow.*` | Navigator impl, page host |
| `applications/vlms/src/ui/shell/Navigator.h` | `ui/workflows/Navigator.h` | Navigation interface (already existed; relocate) |
| `applications/vlms/src/ui/views/ListPage.*` | `ui/ListPage.*` | List shell |
| `applications/vlms/src/ui/views/LoanHistoryDialog.*` | `ui/LoanHistoryDialog.*` | History dialog |
| `applications/vlms/src/ui/views/DashboardPage.*` or `ui/views/MetricsPage.*` | `ui/metrics/MetricsPage.*` | Metrics (rename optional; prefer keep class name `MetricsPage` path under views) |
| `applications/vlms/src/ui/views/*Dialog.*` | `ui/catalog/*`, `ui/members/*`, `ui/circulation/*` dialogs | Form dialogs |
| `applications/vlms/src/ui/views/BirthDateEdit.*`, `BookCopiesTable.*`, delegates, `ImageViewerDialog.*`, `LicenceDialog.*`, … | scattered | View helpers used only by dialogs/pages |
| `applications/vlms/src/ui/presentation/*` | already | Specs, model, lookups |
| `applications/vlms/src/ui/workflows/*` | already | Workflows, DialogFactory, DataEvents, ErrorPresenter |
| `applications/vlms/src/ui/services/Theme.*` | `ui/Theme.*` | Theme |
| `applications/vlms/src/ui/services/OcrService.*` | already from Phase 4 | OCR |
| Delete empty dirs | `ui/catalog`, `ui/members`, `ui/circulation`, `ui/archive`, `ui/metrics` | After moves |

Shared chrome still used by views (`UiHelpers`, `FacetList`, `PreviewImages`, `FlagIcons`, `LanguageSelector`, `ListPageFrame` if still needed for dashboard, `ClickableLabel`, `ManualLocation`) move to `ui/views/` or `ui/shell/` as appropriate: LanguageSelector → shell or views; Theme → services; ManualLocation → shell/views.

`main.cpp` includes `ui/shell/Application.h` and `ui/shell/MainWindow.h`.

---

### Task 1: Audit and finish PageAction gaps

**Files:**
- Modify: each `*ListSpec::actions()` and `ListPage` shortcut installation
- Modify: string catalogue if new keys required (`libraries/Core` strings / VLMS string tables — only new menu/shortcut labels)
- Create: `applications/vlms/test/src/test_list_page_shortcuts.cpp` (or extend `test_list_page.cpp`)

Checklist against the standard table on every live list:

| Action id | Members | Catalog | Circulation | Archive |
|---|---|---|---|---|
| add / checkout | add | add | checkout | — |
| edit | edit | edit | — | — |
| loans | loans | loans | — | loans (not on Loans type) |
| archive / restore | archive | archive | archive | restore |
| purge | — | — | — | purge |
| reuse | — | — | — | copies only |
| return / extend | — | — | yes | — |
| tickAll | yes | yes | yes | yes |
| refresh | yes | yes | yes | yes |

Verify shortcuts and scopes from the Phase 3 table. Context menu order matches button order. Circulation button visual order remains Return, Checkout, Extend, Archive while standard ids still map to shortcuts (Checkout → Ctrl+N).

- [ ] **Step 1: Write failing tests for any missing shortcut/menu entry discovered by audit**

```cpp
TEST(ListPageShortcuts, MembersHasCtrlNEnterCtrlLDelete)
TEST(ListPageShortcuts, CirculationCheckoutIsCtrlN)
TEST(ListPageShortcuts, ArchivePurgeIsShiftDelete)
TEST(ListPageShortcuts, SearchLineEditKeepsCtrlAAndEnter)
```

- [ ] **Step 2: Implement missing actions/shortcuts/keys**
- [ ] **Step 3: PASS**
- [ ] **Step 4: Commit (optional)** `feat(ui): finish shared list actions and shortcuts`

---

### Task 2: LanguageChange path in Application

**Files:**
- Modify: `applications/vlms/src/Application.h` / `.cpp` (or `ui/shell/Application.*` if Task 4 already moved — do this task before or after move consistently; prefer **before** the big move so include churn is once)
- Modify: `MainWindow.*` — remove `connect(..., languageChanged, ..., retranslateUi)`
- Create: `applications/vlms/test/src/test_language_change.cpp`

**Application change:**

```cpp
void Application::setUiLocale(const QString& code)
{
    if (code == uiLocale()) {
        return;
    }
    VLMS::Core::Locale::setCode(VLMS::ss(code));
    saveLocale();
    applyLocale(this);          // layout direction
    applyQtCatalogue();

    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* w : topLevels) {
        QEvent event(QEvent::LanguageChange);
        QCoreApplication::sendEvent(w, &event);
    }
    // languageChanged signal removed
}
```

Remove from `Application.h`:

```cpp
signals:
    void languageChanged();  // DELETE
```

- [ ] **Step 1: Write failing test**

```cpp
TEST(LanguageChange, SetUiLocaleSendsLanguageChangeToTopLevelWidgets)
{
    // Construct Application + a QWidget with changeEvent recording
    // Call setUiLocale to a different code
    // EXPECT recording observed QEvent::LanguageChange
}

TEST(LanguageChange, NoLanguageChangedSignalExists)
{
    // Compile-time / link-time: Application has no languageChanged — covered by
    // removing connects; runtime test asserts MainWindow still updates a known label
}
```

- [ ] **Step 2: Implement Application change; remove signal**
- [ ] **Step 3: Build — fix every `languageChanged` reference**
- [ ] **Step 4: Commit (optional)** `feat(ui): broadcast LanguageChange from setUiLocale`

---

### Task 3: Widgets handle changeEvent; delete retranslateUi chain

**Files:**
- Modify: `ListPage`, `MainWindow`, `MetricsPage`, `LanguageSelector`, dialogs that had `retranslateUi`, `LicenceDialog`, etc.
- Delete public `retranslateUi()` methods used only for locale (private `rebuildChrome()` OK)
- Modify: tests that called `retranslateUi()` — send `LanguageChange` or call `setUiLocale`

Pattern for each `QWidget` subclass:

```cpp
void ListPage::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange) {
        rebuildChrome();          // titles, action labels, facet titles, placeholders
        if (m_spec) {
            m_spec->retranslate();
        }
        if (m_model) {
            emit m_model->headerDataChanged(Qt::Horizontal, 0, m_model->columnCount() - 1);
            // or dataChanged for all display roles
        }
        applyPreviewForSelection();
    }
    QWidget::changeEvent(event);
}
```

MainWindow `changeEvent`: update nav button labels, window title, theme toggle text; children receive their own events only if they are top-level — **important:** `sendEvent` to top-level widgets does **not** automatically recurse to children. Spec says: "sends `QEvent::LanguageChange` to every top-level widget. Widgets re-read their labels in `changeEvent`."

Qt's default `QWidget::changeEvent` for `LanguageChange` does **not** propagate to children. Options that match the spec:

1. MainWindow's `changeEvent` walks child pages and `sendEvent`s `LanguageChange` to each page/dialog host; or
2. Application sends to top-levels **and** MainWindow forwards to pages.

Implement (2): Application sends to top-levels; `MainWindow::changeEvent` updates shell chrome and forwards to `m_membersPage`, `m_catalogPage`, … and any open dialogs it owns. ListPage handles itself. Dialogs override `changeEvent` for their labels.

Remove `MainWindow::retranslateUi` and all `m_*Page->retranslateUi()` calls.

- [ ] **Step 1: Extend `test_language_change.cpp`**

```cpp
TEST(LanguageChange, ListPageRebuildsColumnHeadersWithoutReloadQuery)
TEST(LanguageChange, RtlLayoutAppliedForArabic)
```

- [ ] **Step 2: Implement changeEvent on shell + ListPage + Metrics + LanguageSelector + dialogs**
- [ ] **Step 3: Grep gate**

```bash
git grep -n 'retranslateUi\|languageChanged' applications/vlms/
```

Expected: empty (or only comments in plans/docs outside applications).

- [ ] **Step 4: `ctest -L ui` — PASS**
- [ ] **Step 5: Commit (optional)** `refactor(ui): replace retranslateUi chain with LanguageChange`

---

### Task 4: Move sources into layer folders

**Files:** mass move under `applications/vlms/src/ui/{shell,views,presentation,workflows,services}/` + `main.cpp` include updates + every test include + CMakeLists path updates.

Procedure (do as one careful commit, or per-layer commits):

1. `git mv` Application and MainWindow + Navigator → `ui/shell/`
2. `git mv` ListPage, LoanHistoryDialog, MetricsPage, all *Dialog*, editors' helper widgets → `ui/views/`
3. Confirm presentation/workflows/services already correct; move Theme, OcrService → `ui/services/`
4. Move remaining shared UI (`UiHelpers`, `FacetList`, …) → `ui/views/` (helpers) unless shell-specific
5. Delete empty `ui/catalog`, `ui/members`, `ui/circulation`, `ui/archive`, `ui/metrics`
6. Update `applications/vlms/CMakeLists.txt` every path
7. Update `#include "ui/..."` across src and tests
8. Fix `target_include_directories` if needed (`src/` remains root)

Smoke:

```bash
cmake --build build -j$(nproc)
cd build && ctest -L ui --output-on-failure
```

- [ ] **Step 1: Move shell files; fix includes; build**
- [ ] **Step 2: Move views files; fix includes; build**
- [ ] **Step 3: Move services (Theme); remove empty legacy folders; full test run**
- [ ] **Step 4: Commit (optional)** `refactor(ui): organise sources into shell/views/presentation/workflows/services`

---

### Task 5: Dependency lint + checklist LanguageChange test name

**Files:**
- Ensure `test_language_change.cpp` exposes `LanguageChange.*` for checklist row 21
- Optional: a simple `scripts/` or test that greps `#include` direction (views must not include workflow .cpp details beyond interfaces; presentation must not include views) — if no script exists, document manual grep:

```bash
# presentation must not include ui/views
git grep -n '#include "ui/views' applications/vlms/src/ui/presentation && exit 1 || true
# views must not call repository write method names directly
git grep -nE '->(save|create|update|delete|archive|restore|purge|returnLoan|extendLoan)' \
  applications/vlms/src/ui/views && exit 1 || true
```

- [ ] **Step 1: Run greps; fix violations**
- [ ] **Step 2: Full UI tests PASS**
- [ ] **Step 3: Commit (optional)** `test(ui): assert LanguageChange and layer include rules`

---

## Verification (phase exit)

```bash
cmake --build build -j$(nproc)
cd build && ctest -L ui --output-on-failure
git grep -nE 'retranslateUi|languageChanged' applications/vlms/src applications/vlms/test || true
test ! -d applications/vlms/src/ui/catalog
test ! -d applications/vlms/src/ui/members
test ! -d applications/vlms/src/ui/circulation
test ! -d applications/vlms/src/ui/archive
ls applications/vlms/src/ui/shell applications/vlms/src/ui/views applications/vlms/src/ui/services
```

Open PR `redesign/5-harmonisation` → `Beta`.

---

## Self-review (Phase 5)

| Spec item | Task |
|---|---|
| Shared actions/menus/shortcuts finished | 1 |
| QEvent::LanguageChange; remove languageChanged + retranslateUi | 2–3 |
| Layer folders; remove ui/catalog|members|circulation|archive | 4 |
| Checklist row 21 LanguageChange.* | 5 |
| Debounce already in Phase 3 — confirm still 250 ms | 1 audit |
