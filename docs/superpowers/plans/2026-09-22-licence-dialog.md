# Licence Dialog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Clicking the copyright line in the application footer opens a modal dialog showing the licence, written in whichever of the three UI languages is active.

**Architecture:** The licence text is fifteen new keys in the three string tables in `Strings.cpp`, so the existing key-parity test guards it. A new `LicenceDialog` composes those keys into HTML and shows them in a read-only `QTextBrowser`. A new `ClickableLabel` replaces the footer's plain `QLabel` and emits `clicked()`; `MainWindow` connects that to `LicenceDialog::exec()`. The click widget is separate because `MainWindow` cannot be constructed in a test.

**Tech Stack:** C++20, Qt 6 Widgets, CMake (Unix Makefiles), GoogleTest via `build_gtest_executable`, ctest.

**Spec:** `docs/superpowers/specs/2026-09-22-licence-dialog-design.md`

## Global Constraints

- **Build directory is `build/`.** Build with `cmake --build build -j`. Never run a test binary directly — `./build/bin/test_vlms_core` fails 16 tests that pass under ctest, because the schema path, data directory and TZ come from per-test `ENVIRONMENT` in `cmake/TestUtils.cmake`. **Always judge tests by `ctest`.**
- **Test naming:** Core tests are `test_core_<Suite>`, UI tests are `test_ui_<Suite>`.
- **British English in prose and UI copy** — "licence" (noun), "catalogue". Never in Qt API names or identifiers: the C++ class is `LicenceDialog`, and the string keys use `licence.`.
- **Every new string key must be added to all three tables** in `libraries/Core/src/Strings.cpp` (Arabic ~line 32, French ~line 510, English ~line 1003). `test_core_StringsParity.TablesHaveIdenticalKeySets` fails otherwise.
- **The author is never named in the licence body** — only "the author" / «L'auteur» / «المؤلف». The name appears once, in the contact block, in Arabic script in the Arabic table.
- **`footer.copyright` is not changed** in any language.
- **Exact contact values**, identical in all three languages, defined once in `LicenceDialog.cpp`:
  - `https://www.linkedin.com/in/mlbh/`
  - `mohamed.ben-hassine@mailfence.com`
- Qt classes used must already be linked: the app links only Core/Gui/Widgets. `QTextBrowser` is in Widgets — no new dependency.
- Commit after every task. Commit messages end with:
  `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`

---

### Task 1: The licence strings

**Files:**
- Modify: `libraries/Core/src/Strings.cpp` (three tables, after each `footer.copyright` entry)
- Create: `libraries/Core/test/src/test_licence_strings.cpp`
- Modify: `libraries/Core/test/CMakeLists.txt:50` (add to `TST_SOURCES`)

**Interfaces:**
- Consumes: nothing.
- Produces: fifteen string keys, read later via `Strings::t` / `VLMS::T` and asserted via `Strings::rawValue(locale, key)`:
  ```
  licence.title                   licence.intro
  licence.clause.ownership.head   licence.clause.ownership.body
  licence.clause.data.head        licence.clause.data.body
  licence.clause.warranty.head    licence.clause.warranty.body
  licence.clause.liability.head   licence.clause.liability.body
  licence.clause.support.head     licence.clause.support.body
  licence.contact.head            licence.contact.author
  licence.tooltip
  ```

- [ ] **Step 1: Write the failing test**

Create `libraries/Core/test/src/test_licence_strings.cpp`:

```cpp
#include <VLMS/Core/Strings.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using VLMS::Strings;

namespace {

constexpr auto kArabic = "ar";
constexpr auto kFrench = "fr";
constexpr auto kEnglish = "en";

/// Every key the licence dialog composes. Listed here rather than derived,
/// because the parity test only proves the three tables agree with each
/// other -- it would stay green if all three lost the licence together.
const std::vector<std::string>& licenceKeys()
{
    static const std::vector<std::string> keys = {
        "licence.title",
        "licence.intro",
        "licence.clause.ownership.head",
        "licence.clause.ownership.body",
        "licence.clause.data.head",
        "licence.clause.data.body",
        "licence.clause.warranty.head",
        "licence.clause.warranty.body",
        "licence.clause.liability.head",
        "licence.clause.liability.body",
        "licence.clause.support.head",
        "licence.clause.support.body",
        "licence.contact.head",
        "licence.contact.author",
        "licence.tooltip",
    };
    return keys;
}

}  // namespace

TEST(test_core_LicenceStrings, EveryClauseIsWrittenInEveryLanguage)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        for (const std::string& key : licenceKeys()) {
            SCOPED_TRACE(std::string(locale) + " " + key);
            EXPECT_FALSE(Strings::rawValue(locale, key).empty());
        }
    }
}

// An untranslated clause left as English in the Arabic table is the realistic
// failure. Arabic script against Latin makes that a reliable check. French and
// English are deliberately not compared: "Licence" is legitimately identical
// in both.
TEST(test_core_LicenceStrings, TheArabicIsNotTheEnglish)
{
    for (const std::string& key : licenceKeys()) {
        SCOPED_TRACE(key);
        EXPECT_NE(Strings::rawValue(kArabic, key), Strings::rawValue(kEnglish, key));
    }
}

TEST(test_core_LicenceStrings, TheLicenceNamesTheAuthorOnlyInTheContactBlock)
{
    const std::vector<std::string> bodyKeys = {
        "licence.intro",
        "licence.clause.ownership.body",
        "licence.clause.data.body",
        "licence.clause.warranty.body",
        "licence.clause.liability.body",
        "licence.clause.support.body",
    };
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        for (const std::string& key : bodyKeys) {
            SCOPED_TRACE(std::string(locale) + " " + key);
            EXPECT_EQ(Strings::rawValue(locale, key).find("Ben Hassine"), std::string::npos);
        }
    }
    EXPECT_NE(Strings::rawValue(kEnglish, "licence.contact.author").find("Ben Hassine"),
              std::string::npos);
}
```

Register it — in `libraries/Core/test/CMakeLists.txt`, inside `set(TST_SOURCES ...)`, add after the line `src/test_strings_parity.cpp`:

```cmake
    src/test_licence_strings.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build -j && (cd build && ctest -R '^test_vlms_core$' --output-on-failure)
```

Note the whole Core suite runs as **one** ctest entry — `build_gtest_executable` registers it with
`DISCOVER OFF`, so there is no per-test entry to filter on, and the binary must not be run
directly (see Global Constraints).

Expected: FAIL — within that run, `test_core_LicenceStrings.EveryClauseIsWrittenInEveryLanguage`
reports empty values, because `rawValue` returns an empty string for a key that is not in the
table.

- [ ] **Step 3: Add the Arabic strings**

In `libraries/Core/src/Strings.cpp`, in `arabicStrings()`, immediately after the line
`{"footer.copyright", "© {year} المكتبة العمومية بقصور الساف"},` insert:

```cpp
        // The licence, shown by LicenceDialog when the footer's copyright
        // line is clicked. A deed of gift rather than a software licence:
        // the library owns the solution outright, so there is nothing here
        // restricting what it may do with it. The author is never named in
        // the body, only in the contact block.
        {"licence.title", "الرخصة"},
        {"licence.intro",
         "كُتب برنامج VLMS من أجل المكتبة العمومية بقصور الساف، وهو هدية مُهداة إليها. "
         "لم يُطلب عنه أي مقابل، ولا شيء مستحق عليه، والهبة لا رجعة فيها."},
        {"licence.clause.ownership.head", "الحلّ ملك للمكتبة"},
        {"licence.clause.ownership.body",
         "التطبيق، كما هو مُثبَّت هنا، ملك للمكتبة العمومية بقصور الساف. وللمكتبة أن تستعمله "
         "وتحتفظ به وتنسخه وتنقله كما تشاء."},
        {"licence.clause.data.head", "بيانات المكتبة ملك لها"},
        {"licence.clause.data.body",
         "الفهرس وبطاقات المنخرطين والمسوحات والصور التي تنشئها المكتبة بهذا التطبيق ملك "
         "للمكتبة وحدها. ولا يدّعي المؤلف أيّ حقّ فيها ولا يحتفظ بنسخة منها."},
        {"licence.clause.warranty.head", "بلا ضمان"},
        {"licence.clause.warranty.body",
         "يُقدَّم التطبيق كما هو. ولا يُقدَّم أيّ وعد بخلوّه من العيوب، أو بملاءمته لغرض بعينه، "
         "أو باستمرار عمله. وعلى المكتبة أن تحتفظ بنسخ احتياطية من بياناتها."},
        {"licence.clause.liability.head", "بلا مسؤولية"},
        {"licence.clause.liability.body",
         "لا يتحمّل المؤلف مسؤولية أيّ خسارة أو ضرر ينشأ عن استعمال هذا التطبيق، سواء كان "
         "الاستعمال خاطئًا أو متعمَّدًا. ويشمل ذلك ضياع البيانات أو تلفها، وتعطّل العمل، وكلّ "
         "ما يترتّب على ذلك."},
        {"licence.clause.support.head", "لا التزام بالدعم"},
        {"licence.clause.support.body",
         "لا يلتزم المؤلف بتقديم دعم أو تدريب أو تحديثات أو إصلاحات. وكلّ عون يُقدَّم هو "
         "امتداد للهبة، لا واجب مستحقّ."},
        {"licence.contact.head", "جهة الاتصال"},
        {"licence.contact.author", "المؤلف: محمد الأمين بن حسين"},
        {"licence.tooltip", "عرض رخصة الاستعمال"},
```

- [ ] **Step 4: Add the French strings**

In `frenchStrings()`, immediately after
`{"footer.copyright", "© {year} Bibliothèque publique de Ksour Essef"},` insert:

```cpp
        {"licence.title", "Licence"},
        {"licence.intro",
         "VLMS a été écrit pour la Bibliothèque publique de Ksour Essef et lui est "
         "offert en don. Rien n'a été facturé, rien n'est dû, et ce don est irrévocable."},
        {"licence.clause.ownership.head", "La solution appartient à la Bibliothèque"},
        {"licence.clause.ownership.body",
         "L'application, telle qu'elle est installée ici, appartient à la Bibliothèque "
         "publique de Ksour Essef. La Bibliothèque peut l'utiliser, la conserver, la copier "
         "et la transmettre comme elle l'entend."},
        {"licence.clause.data.head", "Les données de la Bibliothèque lui appartiennent"},
        {"licence.clause.data.body",
         "Le catalogue, les fiches des adhérents, les numérisations et les photographies que "
         "la Bibliothèque crée avec cette application lui appartiennent exclusivement. "
         "L'auteur n'y revendique aucun droit et n'en conserve aucune copie."},
        {"licence.clause.warranty.head", "Aucune garantie"},
        {"licence.clause.warranty.body",
         "L'application est fournie en l'état. Aucune promesse n'est faite quant à l'absence "
         "de défauts, à l'adéquation à un usage particulier, ou à la continuité de son "
         "fonctionnement. La Bibliothèque doit conserver ses propres sauvegardes."},
        {"licence.clause.liability.head", "Aucune responsabilité"},
        {"licence.clause.liability.body",
         "L'auteur n'est pas responsable des pertes ou dommages résultant de l'utilisation "
         "de cette application, que cette utilisation ait été erronée ou délibérée. Cela "
         "comprend les données perdues ou corrompues, le travail interrompu, et toute "
         "conséquence de l'un ou l'autre."},
        {"licence.clause.support.head", "Aucune obligation d'assistance"},
        {"licence.clause.support.body",
         "L'auteur n'est tenu à aucune assistance, formation, mise à jour ni réparation. "
         "Toute aide apportée prolonge le don ; elle n'est pas une obligation."},
        {"licence.contact.head", "Contact"},
        {"licence.contact.author", "L'auteur : Mohamed Lamine Ben Hassine"},
        {"licence.tooltip", "Afficher la licence"},
```

- [ ] **Step 5: Add the English strings**

In `englishStrings()`, immediately after
`{"footer.copyright", "© {year} Public Library of Ksour Essef"},` insert:

```cpp
        {"licence.title", "Licence"},
        {"licence.intro",
         "VLMS was written for the Public Library of Ksour Essef and is given to it as "
         "a gift. Nothing was charged for it, nothing is owed for it, and the gift cannot be "
         "taken back."},
        {"licence.clause.ownership.head", "The solution is the Library's"},
        {"licence.clause.ownership.body",
         "The application, as it is installed here, belongs to the Public Library of Ksour "
         "Essef. The Library may use it, keep it, copy it and pass it on as it sees fit."},
        {"licence.clause.data.head", "The Library's records are its own"},
        {"licence.clause.data.body",
         "The catalogue, the member records, the scans and the photographs the Library "
         "creates with this application belong to the Library alone. The author claims no "
         "right over them and keeps no copy of them."},
        {"licence.clause.warranty.head", "No warranty"},
        {"licence.clause.warranty.body",
         "The application is given as it is. No promise is made that it is free of faults, "
         "that it suits any particular purpose, or that it will keep working. The Library "
         "should keep its own backups of its records."},
        {"licence.clause.liability.head", "No liability"},
        {"licence.clause.liability.body",
         "The author is not responsible for any loss or damage that follows from using this "
         "application, whether the use was mistaken or deliberate. This includes records "
         "lost or corrupted, work interrupted, and anything that follows from either."},
        {"licence.clause.support.head", "No duty to support"},
        {"licence.clause.support.body",
         "The author is under no obligation to provide support, training, updates or "
         "repairs. Any help given is a continuation of the gift, not a duty owed."},
        {"licence.contact.head", "Contact"},
        {"licence.contact.author", "The author: Mohamed Lamine Ben Hassine"},
        {"licence.tooltip", "Show the licence"},
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build build -j && (cd build && ctest -R '^test_vlms_core$' --output-on-failure)
```

Expected: PASS — the whole Core suite, including the 3 new `test_core_LicenceStrings` tests and
the existing `test_core_StringsParity` ones. Parity passing is what proves the three tables still
have identical key sets.

- [ ] **Step 7: Commit**

```bash
git add libraries/Core/src/Strings.cpp \
        libraries/Core/test/src/test_licence_strings.cpp \
        libraries/Core/test/CMakeLists.txt
git commit -m "$(cat <<'MSG'
Write the licence into the three string tables.

A deed of gift: the library owns the solution outright, so nothing here
restricts what it may do with it. Fifteen keys, guarded by the existing
parity test plus a check that the Arabic is not the English.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
)"
```

---

### Task 2: `ClickableLabel`

**Files:**
- Create: `applications/vlms/src/ui/ClickableLabel.h`
- Create: `applications/vlms/src/ui/ClickableLabel.cpp`
- Create: `applications/vlms/test/src/test_clickable_label.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (source list after `src/ui/LanguageSelector.cpp`, header list after `src/ui/LanguageSelector.h`)
- Modify: `applications/vlms/test/CMakeLists.txt` (add to `SRC`)

**Interfaces:**
- Consumes: nothing.
- Produces: `VLMS::ClickableLabel`, a `QLabel` subclass with signal `void clicked()`. Constructor `explicit ClickableLabel(QWidget* parent = nullptr)` sets `Qt::PointingHandCursor`. Task 4 uses it as the type of `MainWindow::m_footerLabel`.

- [ ] **Step 1: Write the failing test**

Create `applications/vlms/test/src/test_clickable_label.cpp`:

```cpp
#include "ui/ClickableLabel.h"

#include <QPoint>
#include <QSignalSpy>
#include <QTest>
#include <Qt>

#include <gtest/gtest.h>

using VLMS::ClickableLabel;

TEST(test_ui_ClickableLabel, ALeftClickIsReported)
{
    ClickableLabel label;
    label.setText(QStringLiteral("© 2026 Public Library of Ksour Essef"));
    label.resize(200, 20);

    QSignalSpy spy(&label, &ClickableLabel::clicked);
    QTest::mouseClick(&label, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));

    EXPECT_EQ(spy.count(), 1);
}

TEST(test_ui_ClickableLabel, ARightClickIsNotAClick)
{
    ClickableLabel label;
    label.resize(200, 20);

    QSignalSpy spy(&label, &ClickableLabel::clicked);
    QTest::mouseClick(&label, Qt::RightButton, Qt::NoModifier, QPoint(10, 10));

    EXPECT_EQ(spy.count(), 0);
}

// A press that wanders off the label before release is not a click, the way
// a push button behaves.
TEST(test_ui_ClickableLabel, AReleaseOutsideTheLabelIsNotAClick)
{
    ClickableLabel label;
    label.resize(200, 20);

    QSignalSpy spy(&label, &ClickableLabel::clicked);
    QTest::mousePress(&label, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QTest::mouseRelease(&label, Qt::LeftButton, Qt::NoModifier, QPoint(400, 200));

    EXPECT_EQ(spy.count(), 0);
}

TEST(test_ui_ClickableLabel, ThePointingHandSaysItCanBeClicked)
{
    ClickableLabel label;
    EXPECT_EQ(label.cursor().shape(), Qt::PointingHandCursor);
}
```

Register it — in `applications/vlms/test/CMakeLists.txt`, inside the `SRC` list, add after `src/test_catalog_local_number.cpp`:

```cmake
        src/test_clickable_label.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build -j 2>&1 | tail -20
```

Expected: FAIL to compile — `ui/ClickableLabel.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `applications/vlms/src/ui/ClickableLabel.h`:

```cpp
#pragma once

#include <QLabel>

class QMouseEvent;

namespace VLMS {

/**
 * A QLabel that reports a left-click.
 *
 * Only the footer's copyright line needs this, and it could as easily have
 * been an event filter inside MainWindow -- except that MainWindow cannot be
 * constructed in a test, because it reaches for qobject_cast<Application*>
 * (qApp). Anything put there is verifiable only by hand, so the click lives
 * here instead, where it has a test.
 */
class ClickableLabel final : public QLabel {
    Q_OBJECT

public:
    explicit ClickableLabel(QWidget* parent = nullptr);

signals:
    void clicked();

protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
};

}  // namespace VLMS
```

- [ ] **Step 4: Write the implementation**

Create `applications/vlms/src/ui/ClickableLabel.cpp`:

```cpp
#include "ui/ClickableLabel.h"

#include <QMouseEvent>

namespace VLMS {

ClickableLabel::ClickableLabel(QWidget* parent)
    : QLabel(parent)
{
    setCursor(Qt::PointingHandCursor);
}

void ClickableLabel::mouseReleaseEvent(QMouseEvent* event)
{
    // Only a release that lands back on the label counts, the way a push
    // button behaves: a press that wanders off before release is a change of
    // mind, not a click.
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        emit clicked();
    }
    QLabel::mouseReleaseEvent(event);
}

}  // namespace VLMS
```

Register it — in `applications/vlms/CMakeLists.txt`, add `src/ui/ClickableLabel.cpp` after
`src/ui/LanguageSelector.cpp`, and `src/ui/ClickableLabel.h` after `src/ui/LanguageSelector.h`.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build -j && (cd build && ctest -R 'test_ui_ClickableLabel' --output-on-failure)
```

Expected: PASS — 4 tests.

- [ ] **Step 6: Commit**

```bash
git add applications/vlms/src/ui/ClickableLabel.h \
        applications/vlms/src/ui/ClickableLabel.cpp \
        applications/vlms/test/src/test_clickable_label.cpp \
        applications/vlms/CMakeLists.txt \
        applications/vlms/test/CMakeLists.txt
git commit -m "$(cat <<'MSG'
Add a QLabel that reports a left-click.

The footer's copyright line opens the licence. The click lives in its own
widget rather than an event filter in MainWindow, which cannot be built in
a test: it reaches for qobject_cast<Application*>(qApp).

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
)"
```

---

### Task 3: `LicenceDialog`

**Files:**
- Create: `applications/vlms/src/ui/LicenceDialog.h`
- Create: `applications/vlms/src/ui/LicenceDialog.cpp`
- Create: `applications/vlms/test/src/test_licence_dialog.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (source and header lists)
- Modify: `applications/vlms/test/CMakeLists.txt` (add to `SRC`)

**Interfaces:**
- Consumes: the fifteen `licence.*` keys from Task 1, via `VLMS::T`.
- Produces: `LicenceDialog`, a `QDialog` in the **global namespace** (like `LoanExtendDialog`, not like `ClickableLabel`). Constructor `explicit LicenceDialog(QWidget* parent = nullptr)`. Test-only accessor `[[nodiscard]] QString documentText() const` returns the rendered licence as plain text. Task 4 constructs it and calls `exec()`.

- [ ] **Step 1: Write the failing test**

Create `applications/vlms/test/src/test_licence_dialog.cpp`:

```cpp
#include "UiTest.h"

#include "ui/LicenceDialog.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QTextBrowser>
#include <Qt>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using VLMS::Locale;
using VLMS::Strings;

namespace {

constexpr auto kArabic = "ar";
constexpr auto kFrench = "fr";
constexpr auto kEnglish = "en";

const std::vector<std::string>& clauseBodyKeys()
{
    static const std::vector<std::string> keys = {
        "licence.intro",
        "licence.clause.ownership.body",
        "licence.clause.data.body",
        "licence.clause.warranty.body",
        "licence.clause.liability.body",
        "licence.clause.support.body",
    };
    return keys;
}

}  // namespace

class test_ui_LicenceDialog : public ::testing::Test {
protected:
    void TearDown() override { Locale::setCode(Locale::kDefaultCode); }
};

TEST_F(test_ui_LicenceDialog, EveryClauseIsShownInTheChosenLanguage)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        LicenceDialog dialog;
        const QString text = dialog.documentText();

        for (const std::string& key : clauseBodyKeys()) {
            SCOPED_TRACE(key);
            EXPECT_TRUE(text.contains(qs(Strings::rawValue(locale, key))));
        }
        EXPECT_TRUE(text.contains(qs(Strings::rawValue(locale, "licence.contact.author"))));
    }
}

TEST_F(test_ui_LicenceDialog, TheContactDetailsAreTheSameInEveryLanguage)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        LicenceDialog dialog;
        const QString text = dialog.documentText();

        EXPECT_TRUE(text.contains(QStringLiteral("https://www.linkedin.com/in/mlbh/")));
        EXPECT_TRUE(text.contains(QStringLiteral("mohamed.ben-hassine@mailfence.com")));
    }
}

TEST_F(test_ui_LicenceDialog, TheCloseButtonComesFromTheApplicationTable)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        LicenceDialog dialog;
        auto* box = dialog.findChild<QDialogButtonBox*>();
        ASSERT_NE(box, nullptr);
        ASSERT_NE(box->button(QDialogButtonBox::Close), nullptr);
        EXPECT_EQ(box->button(QDialogButtonBox::Close)->text(),
                  qs(Strings::rawValue(locale, "common.close")));
    }
}

TEST_F(test_ui_LicenceDialog, OnlyTheArabicLicenceReadsRightToLeft)
{
    Locale::setCode(kArabic);
    LicenceDialog arabic;
    auto* arabicBody = arabic.findChild<QTextBrowser*>();
    ASSERT_NE(arabicBody, nullptr);
    EXPECT_EQ(arabicBody->layoutDirection(), Qt::RightToLeft);

    for (const char* locale : {kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        LicenceDialog dialog;
        auto* body = dialog.findChild<QTextBrowser*>();
        ASSERT_NE(body, nullptr);
        EXPECT_EQ(body->layoutDirection(), Qt::LeftToRight);
    }
}

// A licence is read, not edited, and a click on a line in it should not open
// a web browser on a library workstation.
TEST_F(test_ui_LicenceDialog, TheLicenceCanBeReadAndCopiedButNotChanged)
{
    LicenceDialog dialog;
    auto* body = dialog.findChild<QTextBrowser*>();
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->isReadOnly());
    EXPECT_FALSE(body->openExternalLinks());
    EXPECT_TRUE(body->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
}

TEST_F(test_ui_LicenceDialog, TheWindowIsTitledInTheChosenLanguage)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        Locale::setCode(locale);
        LicenceDialog dialog;
        EXPECT_EQ(dialog.windowTitle(), qs(Strings::rawValue(locale, "licence.title")));
    }
}
```

Register it — in `applications/vlms/test/CMakeLists.txt`, inside `SRC`, add after
`src/test_clickable_label.cpp`:

```cmake
        src/test_licence_dialog.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build -j 2>&1 | tail -20
```

Expected: FAIL to compile — `ui/LicenceDialog.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `applications/vlms/src/ui/LicenceDialog.h`:

```cpp
#pragma once

#include <QDialog>

class QTextBrowser;

/**
 * The licence, opened from the copyright line in the footer.
 *
 * Modal on purpose: the language selector sits in the header, unreachable
 * while this is up, so the text can never go stale underneath the reader.
 * That is why there is no retranslateUi here -- every open composes the
 * document afresh from the current Locale::code().
 */
class LicenceDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LicenceDialog(QWidget* parent = nullptr);

    /// The rendered licence as plain text. For tests; nothing in the
    /// application reads it.
    [[nodiscard]] QString documentText() const;

private:
    void buildUi();

    QTextBrowser* m_body = nullptr;
};
```

- [ ] **Step 4: Write the implementation**

Create `applications/vlms/src/ui/LicenceDialog.cpp`:

```cpp
#include "ui/LicenceDialog.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QDialogButtonBox>
#include <QString>
#include <QStringLiteral>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <Qt>

using VLMS::T;

namespace {

using VLMS::Locale;

// Not string-table keys. They read the same in all three languages, so a key
// would be three identical entries -- and identical entries would defeat
// test_core_LicenceStrings.TheArabicIsNotTheEnglish.
constexpr auto kLinkedIn = "https://www.linkedin.com/in/mlbh/";
constexpr auto kEmail = "mohamed.ben-hassine@mailfence.com";

QString paragraph(const QString& text)
{
    return QStringLiteral("<p>") + text.toHtmlEscaped() + QStringLiteral("</p>");
}

QString clause(const QString& head, const QString& body)
{
    return QStringLiteral("<h3>") + head.toHtmlEscaped() + QStringLiteral("</h3>")
           + paragraph(body);
}

QString licenceHtml()
{
    const QString direction = Locale::isRtl() ? QStringLiteral("rtl") : QStringLiteral("ltr");

    QString html = QStringLiteral("<div dir=\"%1\">").arg(direction);
    html += QStringLiteral("<h2>") + T("licence.title").toHtmlEscaped() + QStringLiteral("</h2>");
    html += paragraph(T("licence.intro"));
    html += clause(T("licence.clause.ownership.head"), T("licence.clause.ownership.body"));
    html += clause(T("licence.clause.data.head"), T("licence.clause.data.body"));
    html += clause(T("licence.clause.warranty.head"), T("licence.clause.warranty.body"));
    html += clause(T("licence.clause.liability.head"), T("licence.clause.liability.body"));
    html += clause(T("licence.clause.support.head"), T("licence.clause.support.body"));
    html += QStringLiteral("<h3>") + T("licence.contact.head").toHtmlEscaped()
            + QStringLiteral("</h3>");
    html += QStringLiteral("<p>") + T("licence.contact.author").toHtmlEscaped()
            + QStringLiteral("<br>") + QString::fromLatin1(kLinkedIn)
            + QStringLiteral("<br>") + QString::fromLatin1(kEmail) + QStringLiteral("</p>");
    html += QStringLiteral("</div>");
    return html;
}

}  // namespace

LicenceDialog::LicenceDialog(QWidget* parent)
    : QDialog(parent)
{
    buildUi();
}

void LicenceDialog::buildUi()
{
    setWindowTitle(T("licence.title"));
    resize(560, 620);

    auto* layout = new QVBoxLayout(this);

    // A QTextBrowser rather than a QLabel in a QScrollArea: the text scrolls,
    // and a librarian can select and copy a clause out of it.
    m_body = new QTextBrowser(this);
    m_body->setObjectName(QStringLiteral("licenceBody"));
    m_body->setFrameShape(QFrame::NoFrame);
    m_body->setOpenExternalLinks(false);
    m_body->setLayoutDirection(Locale::isRtl() ? Qt::RightToLeft : Qt::LeftToRight);
    m_body->setHtml(licenceHtml());
    layout->addWidget(m_body);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    VLMS::localizeButtonBox(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString LicenceDialog::documentText() const
{
    return m_body->toPlainText();
}
```

Register it — in `applications/vlms/CMakeLists.txt`, add `src/ui/LicenceDialog.cpp` after
`src/ui/ClickableLabel.cpp`, and `src/ui/LicenceDialog.h` after `src/ui/ClickableLabel.h`.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build -j && (cd build && ctest -R 'test_ui_LicenceDialog' --output-on-failure)
```

Expected: PASS — 6 tests.

If `TheCloseButtonComesFromTheApplicationTable` fails with Qt's own "Close" label, check that
`localizeButtonBox` is called *before* the box is shown; see the `ButtonLabelKeeper` note in
`UiHelpers.cpp`, which exists because Qt relabels standard buttons on `QEvent::LanguageChange`.

- [ ] **Step 6: Commit**

```bash
git add applications/vlms/src/ui/LicenceDialog.h \
        applications/vlms/src/ui/LicenceDialog.cpp \
        applications/vlms/test/src/test_licence_dialog.cpp \
        applications/vlms/CMakeLists.txt \
        applications/vlms/test/CMakeLists.txt
git commit -m "$(cat <<'MSG'
Show the licence in a dialog, in the language being read.

Modal, so the language cannot change underneath it and there is nothing to
retranslate: each open composes the document from the current locale. The
contact URL and address are constants rather than keys -- three identical
table entries would defeat the Arabic-is-not-the-English check.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
)"
```

---

### Task 4: Open it from the footer

**Files:**
- Modify: `applications/vlms/src/ui/MainWindow.h:11` (forward declaration), `:41` (member type)
- Modify: `applications/vlms/src/ui/MainWindow.cpp:214-215` (build), `:395-396` (retranslate)
- Modify: `CLAUDE.md` (session log entry)

**Interfaces:**
- Consumes: `VLMS::ClickableLabel` (Task 2), `LicenceDialog` (Task 3), `licence.tooltip` (Task 1).
- Produces: nothing further.

`MainWindow` has no test — it reaches for `qobject_cast<Application*>(qApp)` — so this task is
verified by driving the real application, not by ctest. That is expected, not a shortfall.

- [ ] **Step 1: Change the footer label's type**

In `applications/vlms/src/ui/MainWindow.h`, the forward declarations near the top already
include `class QLabel;`. Add `ClickableLabel` to the existing `VLMS` namespace block, which
currently reads:

```cpp
namespace VLMS {
class LanguageSelector;
}
```

so that it becomes:

```cpp
namespace VLMS {
class ClickableLabel;
class LanguageSelector;
}
```

Then change the member declaration from:

```cpp
    QLabel* m_footerLabel = nullptr;
```

to:

```cpp
    VLMS::ClickableLabel* m_footerLabel = nullptr;
```

- [ ] **Step 2: Build the label and wire the click**

In `applications/vlms/src/ui/MainWindow.cpp`, add these includes beside the existing ui
includes:

```cpp
#include "ui/ClickableLabel.h"
#include "ui/LicenceDialog.h"
```

Then replace:

```cpp
    m_footerLabel = new QLabel(footer);
    footerLayout->addWidget(m_footerLabel);
```

with:

```cpp
    m_footerLabel = new VLMS::ClickableLabel(footer);
    connect(m_footerLabel, &VLMS::ClickableLabel::clicked, this, [this]() {
        LicenceDialog(this).exec();
    });
    footerLayout->addWidget(m_footerLabel);
```

- [ ] **Step 3: Give it a tooltip**

In `MainWindow::retranslateUi`, replace:

```cpp
    m_footerLabel->setText(T("footer.copyright", "year",
                             std::to_string(Clock::today().year())));
```

with:

```cpp
    m_footerLabel->setText(T("footer.copyright", "year",
                             std::to_string(Clock::today().year())));
    // The label keeps its appearance -- no underline, no hover change. The
    // pointing hand (set by ClickableLabel) and this tooltip are the only
    // cues that it opens anything.
    m_footerLabel->setToolTip(T("licence.tooltip"));
```

- [ ] **Step 4: Build and run the whole suite**

```bash
cmake --build build -j && (cd build && ctest --output-on-failure)
```

Expected: PASS, every test. The count goes from **146 to 156**: the 10 new UI tests
(4 `ClickableLabel` + 6 `LicenceDialog`) each get their own ctest entry because the UI executable
is registered with `DISCOVER ON`, while the 3 new Core tests run inside the single existing
`test_vlms_core` entry and so do not change the count.

- [ ] **Step 5: Verify in the running application**

Green tests are not enough here, because nothing in ctest can construct `MainWindow`.
Launch the real application and confirm by eye. Per the project's screenshot recipe: force the
xcb platform, not Wayland; capture by window id; and run it in the foreground, because it dies
otherwise.

```bash
QT_QPA_PLATFORM=xcb ./build/bin/vlms
```

Confirm, and capture a screenshot of each:

1. Hovering the footer copyright line shows a pointing hand and the tooltip.
2. Clicking it opens the licence dialog.
3. The dialog reads correctly in **Arabic** (right-to-left, headings right-aligned), **French**, and **English** — switch with the three flag buttons in the header, then reopen.
4. The Close button reads موافق-style application wording, not Qt's own — i.e. إغلاق / Fermer / Close.
5. The dialog is legible in **dark mode** as well as light (toggle beside the flags).
6. The contact block shows the name, the LinkedIn URL and the email, as plain text.

- [ ] **Step 6: Add the session-log entry**

In `CLAUDE.md`, at the top of the Session log list (newest first), add:

```markdown
- 2026-09-22 — The footer's copyright line opens the licence. It is a *deed of gift*, not a
  software licence: the library owns the solution outright, so the "for this library only" and
  "no modification" clauses were dropped — neither can restrain an owner, and both contradicted
  the footer's `© {year} المكتبة العمومية بقصور الساف`, which is unchanged. What remains is the
  gift, the library's ownership of its own records, and no warranty / no liability / no duty to
  support. Not MIT: the library receives an installed Windows application, never the source, so
  a source licence would describe rights nobody is given. The text is fifteen `licence.*` keys in
  the three tables, so `test_core_StringsParity` guards it; `test_core_LicenceStrings` adds what
  parity cannot see — that the keys exist at all, and that the Arabic is not the English. The
  LinkedIn URL and email are constants in `LicenceDialog.cpp`, not keys, because three identical
  entries would defeat that check. The dialog is modal, so the language cannot change underneath
  it and there is no `retranslateUi`. The click lives in `ClickableLabel` rather than an event
  filter in `MainWindow`, which still cannot be built in a test. The label deliberately keeps its
  appearance — pointing hand and tooltip only, no underline, no keyboard access. Spec:
  `docs/superpowers/specs/2026-09-22-licence-dialog-design.md`.
```

- [ ] **Step 7: Commit**

```bash
git add applications/vlms/src/ui/MainWindow.h \
        applications/vlms/src/ui/MainWindow.cpp \
        CLAUDE.md
git commit -m "$(cat <<'MSG'
Open the licence from the footer's copyright line.

The label keeps its appearance: a pointing hand and a tooltip are the only
cues, by choice. MainWindow still has no test, so the wiring was checked by
driving the real application in all three languages.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
MSG
)"
```

---

## Self-review notes

Checked against the spec:

| Spec section | Task |
|---|---|
| The licence (5 clauses + preamble + contact) | 1, and the Appendix wording is reproduced verbatim in Steps 3–5 |
| Where the text lives (15 keys, constants not keys) | 1, 3 |
| The dialog (`QTextBrowser`, Close, 560×620, modal, RTL) | 3 |
| The click (`ClickableLabel`, cursor, tooltip, no keyboard) | 2, 4 |
| Build registration (3 CMakeLists) | 1, 2, 3 |
| Testing (Core strings, dialog, clickable label, by hand) | 1, 2, 3, 4 |
| Out of scope (no `LICENCE.md`, `footer.copyright` unchanged) | honoured — no task touches either |

Names used consistently across tasks: `ClickableLabel::clicked`, `LicenceDialog::documentText`,
`licenceHtml`, `m_footerLabel`, `m_body`. `ClickableLabel` is in `VLMS`; `LicenceDialog` is
in the global namespace, matching `LoanExtendDialog`.
