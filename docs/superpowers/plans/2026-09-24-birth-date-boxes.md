# Birth date dropdowns Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Add Member and Edit Member free-text date of birth with three compact unlabeled dropdowns (day, month, year) whose saved value is still `yyyy-MM-dd`.

**Architecture:** `BirthDateEdit` owns three `QComboBox`es and no labels. The row order is day, month, year and follows the parent, so Arabic puts the day on the right. Each combo is left to right, five rows visible, and only as wide as `00` or `0000` plus the popup arrow. `MemberEditorDialog` keeps the same three warning keys. The repository checks are not edited. The line-edit `BirthDateEdit` already in the tree is replaced, not extended.

**Tech Stack:** C++20 / Qt 6 Widgets (`QComboBox`); GoogleTest via `ctest` on `/home/amin/Dokumente/dev/VLMS/build`.

Spec: `docs/superpowers/specs/2026-09-24-birth-date-boxes-design.md`.

## Global Constraints

- Three dropdowns, reading order day, month, year, on the same row as Sex. No Day/Month/Year captions. No hyphen labels. The only label is `member.field.dateOfBirth`, already on the form.
- Each combo is only as wide as its sample plus the popup arrow: day and month sample `00`, year sample `0000`. Small gap. They do not stretch to fill the form cell. The Sex combo is unchanged.
- Each combo is `Qt::LeftToRight`. The row follows the parent. RTL: day is to the right of year. LTR: day is to the left of year. Do not set `BirthDateEdit`'s own layout direction.
- Day items `01`–`31`, month `01`–`12`, year `1916`–`2016` inclusive. `setMaxVisibleItems(5)`. Opening the popup scrolls the selected value into view. The popup scrollbar stays on the right even in Arabic.
- A new member starts with all three unset (`currentIndex() == -1`, empty current text). Do not default to `01` / `01` / `1916`.
- `setIsoDate` does not move focus. A digit part outside the list is inserted and selected. `isoDate()` is `yyyy-MM-dd` when all three are set, otherwise empty.
- Warning keys stay. Any unset combo → `member.dateOfBirthRequired`, first unset in day, month, year order. All set and not a real date → `member.dateOfBirthInvalid`, day. Real date after today → `member.dateOfBirthInFuture`, year. Repository checks are unchanged.
- Delete `member.field.dateOfBirthHint` from all three tables. Do not edit the warning sentences.
- Do not touch loan date edits, manual capture, or screenshots. No custom item delegate.
- Do not commit. Build only with `cmake --build /home/amin/Dokumente/dev/VLMS/build --target test_vlms_ui`. Judge with `ctest --test-dir /home/amin/Dokumente/dev/VLMS/build`. Do not wipe `build/`. Do not kill `build/bin/vlms`. Do not edit `.vscode/settings.json` or `libraries/Core/test/src/test_circulation_timezone.cpp`.
- `BirthDateEdit.cpp`, `BirthDateEdit.h`, and `test_birth_date_edit.cpp` already exist as the discarded line-edit widget. Replace their contents. The CMake entries are already present; do not add them again.

## Decisions

- `isoDate` joins year, month, day with `-` only when every combo has `currentIndex() >= 0`.
- `setIsoDate` splits on `-` into year, month, day. A part is used only when it is non-empty and all ASCII digits. `findText` selects it; otherwise it is inserted in numeric order and selected. A missing or non-digit part leaves that combo at index `-1`. Calling `setIsoDate` first restores the canonical lists so a previous insert does not accumulate.
- Popup: a private `QComboBox` subclass forces the view and the popup widget to `Qt::LeftToRight` inside `showPopup`, then `scrollTo` the current index with `EnsureVisible`. That is not an item delegate. `maxVisibleItems` is 5 on the combo itself.
- Width: after `ensurePolished` and a temporary resize, fixed width is `horizontalAdvance(sample)` plus (`widget width` minus `SC_ComboBoxEditField` width), which is the arrow, frame, and stylesheet padding. `QSizePolicy::Fixed` on each combo and on `BirthDateEdit`. Layout spacing is 4. Contents margins are 0.
- `failureFor` in the dialog tests destroys the dialog, so focus assertions keep the dialog alive.
- Names are checked before the date. A new dialog reaches `dateOfBirthRequired` only after the first two `QLineEdit`s (first name, last name) are non-empty.

## File structure

```
applications/vlms/src/ui/members/BirthDateEdit.h       replace (Task 1)
applications/vlms/src/ui/members/BirthDateEdit.cpp      replace (Task 1)
applications/vlms/test/src/test_birth_date_edit.cpp     replace (Task 1)
applications/vlms/src/ui/members/MemberEditorDialog.h   Task 2
applications/vlms/src/ui/members/MemberEditorDialog.cpp Task 2
applications/vlms/test/src/test_dialog_translations.cpp Task 2
libraries/Core/src/Strings.cpp                               Task 2
```

---

### Task 1: Dropdown row

**Files:**
- Modify: `applications/vlms/src/ui/members/BirthDateEdit.h` (replace)
- Modify: `applications/vlms/src/ui/members/BirthDateEdit.cpp` (replace)
- Modify: `applications/vlms/test/src/test_birth_date_edit.cpp` (replace)

**Interfaces:**
- Consumes: nothing.
- Produces:

```cpp
class BirthDateEdit final : public QWidget {
    Q_OBJECT
public:
    explicit BirthDateEdit(QWidget* parent = nullptr);
    [[nodiscard]] QComboBox* dayCombo() const;
    [[nodiscard]] QComboBox* monthCombo() const;
    [[nodiscard]] QComboBox* yearCombo() const;
    void setIsoDate(const QString& iso);
    [[nodiscard]] QString isoDate() const;
};
```

- [ ] **Step 1: Write the failing test**

Replace `applications/vlms/test/src/test_birth_date_edit.cpp` with the tests below. The header still declares `QLineEdit*` accessors, so this file must fail to compile.

```cpp
#include "ui/members/BirthDateEdit.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QPoint>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QVBoxLayout>

#include <gtest/gtest.h>

namespace {

void showWindow(QWidget& window)
{
    window.resize(420, 80);
    window.show();
    if (window.layout() != nullptr) {
        window.layout()->activate();
    }
    QApplication::processEvents();
}

QStringList texts(const QComboBox* box)
{
    QStringList values;
    for (int i = 0; i < box->count(); ++i) {
        values.append(box->itemText(i));
    }
    return values;
}

int editFieldWidth(const QComboBox* box)
{
    QStyleOptionComboBox option;
    option.initFrom(box);
    option.rect = box->rect();
    option.editable = false;
    option.subControls = QStyle::SC_All;
    return box->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, box).width();
}

}  // namespace

TEST(test_ui_BirthDateEdit, ListsAreCanonicalAndFiveRowsAreVisible)
{
    BirthDateEdit edit;
    EXPECT_EQ(texts(edit.dayCombo()).size(), 31);
    EXPECT_EQ(texts(edit.dayCombo()).first(), QStringLiteral("01"));
    EXPECT_EQ(texts(edit.dayCombo()).last(), QStringLiteral("31"));
    EXPECT_EQ(texts(edit.monthCombo()).size(), 12);
    EXPECT_EQ(texts(edit.monthCombo()).first(), QStringLiteral("01"));
    EXPECT_EQ(texts(edit.monthCombo()).last(), QStringLiteral("12"));
    EXPECT_EQ(texts(edit.yearCombo()).size(), 101);
    EXPECT_EQ(texts(edit.yearCombo()).first(), QStringLiteral("1916"));
    EXPECT_EQ(texts(edit.yearCombo()).last(), QStringLiteral("2016"));
    EXPECT_EQ(edit.dayCombo()->maxVisibleItems(), 5);
    EXPECT_EQ(edit.monthCombo()->maxVisibleItems(), 5);
    EXPECT_EQ(edit.yearCombo()->maxVisibleItems(), 5);
    EXPECT_EQ(edit.dayCombo()->currentIndex(), -1);
    EXPECT_TRUE(edit.dayCombo()->currentText().isEmpty());
    EXPECT_EQ(edit.monthCombo()->currentIndex(), -1);
    EXPECT_EQ(edit.yearCombo()->currentIndex(), -1);
    EXPECT_TRUE(edit.isoDate().isEmpty());
    EXPECT_TRUE(edit.findChildren<QLabel*>().isEmpty());
    EXPECT_EQ(edit.dayCombo()->layoutDirection(), Qt::LeftToRight);
    EXPECT_EQ(edit.monthCombo()->layoutDirection(), Qt::LeftToRight);
    EXPECT_EQ(edit.yearCombo()->layoutDirection(), Qt::LeftToRight);
}

TEST(test_ui_BirthDateEdit, CombosAreOnlyAsWideAsTheirSamples)
{
    QWidget window;
    window.setStyleSheet(QStringLiteral("QComboBox { border: 1px solid black; padding: 8px 12px; }"));
    auto* edit = new BirthDateEdit(&window);
    auto* layout = new QVBoxLayout(&window);
    layout->addWidget(edit);
    showWindow(window);

    EXPECT_EQ(edit->dayCombo()->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    EXPECT_EQ(edit->monthCombo()->width(), edit->dayCombo()->width());
    EXPECT_GT(edit->yearCombo()->width(), edit->dayCombo()->width());
    EXPECT_GE(editFieldWidth(edit->dayCombo()), edit->dayCombo()->fontMetrics().horizontalAdvance(QStringLiteral("00")));
    EXPECT_GE(editFieldWidth(edit->monthCombo()), edit->monthCombo()->fontMetrics().horizontalAdvance(QStringLiteral("00")));
    EXPECT_GE(editFieldWidth(edit->yearCombo()), edit->yearCombo()->fontMetrics().horizontalAdvance(QStringLiteral("0000")));
    EXPECT_EQ(edit->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
}

TEST(test_ui_BirthDateEdit, SetIsoDateSelectsPartsAndDoesNotMoveFocus)
{
    BirthDateEdit edit;
    showWindow(edit);
    edit.monthCombo()->setFocus();
    ASSERT_EQ(QApplication::focusWidget(), edit.monthCombo());

    edit.setIsoDate(QStringLiteral("1990-05-12"));
    EXPECT_EQ(edit.dayCombo()->currentText(), QStringLiteral("12"));
    EXPECT_EQ(edit.monthCombo()->currentText(), QStringLiteral("05"));
    EXPECT_EQ(edit.yearCombo()->currentText(), QStringLiteral("1990"));
    EXPECT_EQ(edit.isoDate(), QStringLiteral("1990-05-12"));
    EXPECT_EQ(QApplication::focusWidget(), edit.monthCombo());
    EXPECT_EQ(edit.yearCombo()->count(), 101);

    edit.setIsoDate(QStringLiteral("1890-05-12"));
    EXPECT_EQ(edit.yearCombo()->currentText(), QStringLiteral("1890"));
    EXPECT_GE(edit.yearCombo()->findText(QStringLiteral("1890")), 0);
    EXPECT_EQ(edit.isoDate(), QStringLiteral("1890-05-12"));

    edit.setIsoDate(QStringLiteral("not a date"));
    EXPECT_EQ(edit.dayCombo()->currentIndex(), -1);
    EXPECT_EQ(edit.monthCombo()->currentIndex(), -1);
    EXPECT_EQ(edit.yearCombo()->currentIndex(), -1);
    EXPECT_TRUE(edit.isoDate().isEmpty());
    EXPECT_EQ(QApplication::focusWidget(), edit.monthCombo());
}

TEST(test_ui_BirthDateEdit, UnderARightToLeftParentTheDayIsOnTheRight)
{
    QWidget window;
    window.setLayoutDirection(Qt::RightToLeft);
    auto* layout = new QVBoxLayout(&window);
    auto* edit = new BirthDateEdit(&window);
    layout->addWidget(edit);
    showWindow(window);

    EXPECT_EQ(edit->layoutDirection(), Qt::RightToLeft);
    const int dayX = edit->dayCombo()->mapTo(&window, QPoint(0, 0)).x();
    const int yearX = edit->yearCombo()->mapTo(&window, QPoint(0, 0)).x();
    EXPECT_GT(dayX, yearX);
}

TEST(test_ui_BirthDateEdit, ThePopupScrollbarStaysOnTheRightAndShowsTheSelection)
{
    QWidget window;
    window.setLayoutDirection(Qt::RightToLeft);
    auto* layout = new QVBoxLayout(&window);
    auto* edit = new BirthDateEdit(&window);
    layout->addWidget(edit);
    showWindow(window);

    edit->yearCombo()->setCurrentText(QStringLiteral("2016"));
    edit->yearCombo()->showPopup();
    QApplication::processEvents();
    QAbstractItemView* view = edit->yearCombo()->view();
    ASSERT_NE(view, nullptr);
    QScrollBar* bar = view->verticalScrollBar();
    ASSERT_NE(bar, nullptr);
    EXPECT_GT(bar->mapToGlobal(QPoint(0, 0)).x(), view->viewport()->mapToGlobal(QPoint(0, 0)).x());
    const QRect visual = view->visualRect(view->model()->index(edit->yearCombo()->currentIndex(), 0));
    EXPECT_TRUE(view->viewport()->rect().intersects(visual));
    edit->yearCombo()->hidePopup();
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build /home/amin/Dokumente/dev/VLMS/build --target test_vlms_ui -j
```

Expected: compile error. `dayCombo` is not a member of the line-edit widget.

- [ ] **Step 3: Replace the widget**

Replace `BirthDateEdit.h`:

```cpp
#pragma once

#include <QWidget>

class QComboBox;

class BirthDateEdit final : public QWidget {
    Q_OBJECT

public:
    explicit BirthDateEdit(QWidget* parent = nullptr);

    [[nodiscard]] QComboBox* dayCombo() const;
    [[nodiscard]] QComboBox* monthCombo() const;
    [[nodiscard]] QComboBox* yearCombo() const;

    void setIsoDate(const QString& iso);
    [[nodiscard]] QString isoDate() const;

private:
    class Combo;

    void fillCanonical();
    void selectOrInsert(QComboBox* box, const QString& text);
    [[nodiscard]] static bool allDigits(const QString& text);

    QComboBox* m_day = nullptr;
    QComboBox* m_month = nullptr;
    QComboBox* m_year = nullptr;
};
```

Replace `BirthDateEdit.cpp` with a widget that:

- Builds a `QHBoxLayout` with margins 0 and spacing 4, adding day, then month, then year. No labels.
- Does not call `setLayoutDirection` on itself.
- Each box is the private `Combo`, `Qt::LeftToRight`, `setMaxVisibleItems(5)`, `QSizePolicy::Fixed`, non-editable.
- `Combo::showPopup` sets the view and `view()->window()` to `Qt::LeftToRight`, calls `QComboBox::showPopup`, sets those directions again, then `scrollTo` the current index with `QAbstractItemView::EnsureVisible` when the index is non-negative.
- Fills day `01`–`31`, month `01`–`12`, year `1916`–`2016` with `QString::number(i).rightJustified(2, u'0')` for day and month and `QString::number(year)` for years, then `setCurrentIndex(-1)`.
- Sets each combo's fixed width from `horizontalAdvance` of `00` or `0000` plus the polished combo's chrome (`width() - SC_ComboBoxEditField.width()`), measured after a temporary `resize` so the arrow rect is real. Sets `BirthDateEdit` to `QSizePolicy::Fixed` horizontally.
- `setIsoDate` calls `fillCanonical()` first (so an old inserted year disappears), then selects year, month, day from the split parts. `selectOrInsert` inserts an all-digit missing value in numeric order. It never calls `setFocus`.
- `isoDate` returns empty unless all three indexes are `>= 0`, otherwise `year-month-day` current texts.

Use `QSignalBlocker` around `fillCanonical` and `selectOrInsert` so filling the lists is not a user edit. Do not block across `setIsoDate` in a way that skips the final `setCurrentIndex`; the blocker may cover the whole `setIsoDate` because focus is what must stay put, and signals are not part of the spec.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build /home/amin/Dokumente/dev/VLMS/build --target test_vlms_ui -j
ctest --test-dir /home/amin/Dokumente/dev/VLMS/build --output-on-failure -R '^test_ui_BirthDateEdit\.'
```

Expected: every `test_ui_BirthDateEdit` test PASSes.

- [ ] **Step 5: Do not commit**

---

### Task 2: Dialog warnings and the hint key

**Files:**
- Modify: `applications/vlms/src/ui/members/MemberEditorDialog.h`
- Modify: `applications/vlms/src/ui/members/MemberEditorDialog.cpp`
- Modify: `applications/vlms/test/src/test_dialog_translations.cpp`
- Modify: `libraries/Core/src/Strings.cpp`

**Interfaces:**
- Consumes: `dayCombo`, `monthCombo`, `yearCombo`, `setIsoDate`, `isoDate`.
- Produces: `m_dateOfBirthEdit` is a `BirthDateEdit*`. `memberInput().dateOfBirth` is `ss(isoDate())`. The hint key is absent from all three tables.

- [ ] **Step 1: Write the failing tests**

In `test_dialog_translations.cpp`, include `ui/members/BirthDateEdit.h`. Add helpers in the anonymous namespace:

```cpp
BirthDateEdit* birthDate(MemberEditorDialog& dialog)
{
    return dialog.findChild<BirthDateEdit*>();
}

void fillNames(MemberEditorDialog& dialog)
{
    const QList<QLineEdit*> edits = dialog.findChildren<QLineEdit*>();
    ASSERT_GE(edits.size(), 2);
    edits.at(0)->setText(QStringLiteral("Amina"));
    edits.at(1)->setText(QStringLiteral("Ben Salah"));
}
```

`fillNames` cannot use `ASSERT_GE` if it returns void from a non-test function. Use a test-local lambda, or return `false` from the helper. Write it inline in the new-dialog test:

```cpp
TEST_F(test_ui_DialogTranslations, ANewMemberDateOfBirthIsUnsetAndRequired)
{
    MemberEditorDialog dialog(*m_repository);
    const QList<QLineEdit*> edits = dialog.findChildren<QLineEdit*>();
    ASSERT_GE(edits.size(), 2);
    edits.at(0)->setText(QStringLiteral("Amina"));
    edits.at(1)->setText(QStringLiteral("Ben Salah"));
    auto* birth = birthDate(dialog);
    ASSERT_NE(birth, nullptr);
    EXPECT_EQ(birth->dayCombo()->currentIndex(), -1);
    EXPECT_EQ(birth->monthCombo()->currentIndex(), -1);
    EXPECT_EQ(birth->yearCombo()->currentIndex(), -1);
    const auto failure = dialog.firstValidationFailure();
    EXPECT_EQ(failure.messageKey, QStringLiteral("member.dateOfBirthRequired"));
    EXPECT_EQ(failure.field, birth->dayCombo());
    EXPECT_TRUE(Strings::rawValue(kArabic, "member.field.dateOfBirthHint").empty());
    EXPECT_TRUE(Strings::rawValue(kFrench, "member.field.dateOfBirthHint").empty());
    EXPECT_TRUE(Strings::rawValue(kEnglish, "member.field.dateOfBirthHint").empty());
}

TEST_F(test_ui_DialogTranslations, ThirtyFirstOfFebruaryFocusesTheDay)
{
    MemberEditorDialog dialog(*m_repository);
    const QList<QLineEdit*> edits = dialog.findChildren<QLineEdit*>();
    ASSERT_GE(edits.size(), 2);
    edits.at(0)->setText(QStringLiteral("Amina"));
    edits.at(1)->setText(QStringLiteral("Ben Salah"));
    auto* birth = birthDate(dialog);
    ASSERT_NE(birth, nullptr);
    birth->dayCombo()->setCurrentText(QStringLiteral("31"));
    birth->monthCombo()->setCurrentText(QStringLiteral("02"));
    birth->yearCombo()->setCurrentText(QStringLiteral("1990"));
    const auto failure = dialog.firstValidationFailure();
    EXPECT_EQ(failure.messageKey, QStringLiteral("member.dateOfBirthInvalid"));
    EXPECT_EQ(failure.field, birth->dayCombo());
}

TEST_F(test_ui_DialogTranslations, AChosenBirthDateIsReturnedAsIso)
{
    MemberRecord member = validMember();
    member.dateOfBirth = "1990-05-12";
    MemberEditorDialog dialog(*m_repository, member);
    EXPECT_TRUE(dialog.firstValidationFailure().messageKey.isEmpty());
    EXPECT_EQ(dialog.memberInput().dateOfBirth, "1990-05-12");
}

TEST_F(test_ui_DialogTranslations, AnOutOfRangeStoredYearStaysSelected)
{
    MemberRecord member = validMember();
    member.dateOfBirth = "1890-05-12";
    MemberEditorDialog dialog(*m_repository, member);
    auto* birth = birthDate(dialog);
    ASSERT_NE(birth, nullptr);
    EXPECT_EQ(birth->yearCombo()->currentText(), QStringLiteral("1890"));
    EXPECT_EQ(birth->monthCombo()->currentText(), QStringLiteral("05"));
    EXPECT_EQ(birth->dayCombo()->currentText(), QStringLiteral("12"));
}
```

Replace `AnEmptyDateOfBirthIsRefused` so an empty stored date expects `member.dateOfBirthRequired` and `failure.field == birthDate(dialog)->dayCombo()`. Delete the `QLineEdit` cast and the hint placeholder assertion.

Replace `ARefusedDateOfBirthPointsAtItsOwnField`: `not a date` leaves the three combos unset and the failure is `member.dateOfBirthRequired` on the day combo.

Change `ABareYearOfBirthIsRefused` to expect `member.dateOfBirthRequired` (year `1987` is in range; day and month stay unset).

Leave `AnImpossibleDateOfBirthIsRefused`, `ABirthDateAfterTodayIsRefused`, and `AValidMemberProducesNoFailure` checking the same keys.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build /home/amin/Dokumente/dev/VLMS/build --target test_vlms_ui -j
ctest --test-dir /home/amin/Dokumente/dev/VLMS/build --output-on-failure -R '^test_ui_DialogTranslations\.(ANewMemberDateOfBirthIsUnsetAndRequired|ThirtyFirstOfFebruaryFocusesTheDay|AChosenBirthDateIsReturnedAsIso|AnOutOfRangeStoredYearStaysSelected|AnEmptyDateOfBirthIsRefused)$'
```

Expected: FAIL because the dialog still builds a `QLineEdit` and the hint key still exists. A compile error is the wrong failure; `BirthDateEdit` from Task 1 is already on the include path.

- [ ] **Step 3: Wire the dialog and delete the hint**

Forward-declare `class BirthDateEdit;` in the dialog header and change `m_dateOfBirthEdit` to `BirthDateEdit*`.

In the cpp, include `ui/members/BirthDateEdit.h`. Construct `new BirthDateEdit(m_fieldsPanel)` in place of the `QLineEdit`. Keep the same `addPairedRow` with the sex combo. Do not add a caption.

`firstValidationFailure` date block, after the two name checks:

```cpp
    QComboBox* const day = m_dateOfBirthEdit->dayCombo();
    QComboBox* const month = m_dateOfBirthEdit->monthCombo();
    QComboBox* const year = m_dateOfBirthEdit->yearCombo();
    if (day->currentIndex() < 0) {
        return {QStringLiteral("member.dateOfBirthRequired"), day};
    }
    if (month->currentIndex() < 0) {
        return {QStringLiteral("member.dateOfBirthRequired"), month};
    }
    if (year->currentIndex() < 0) {
        return {QStringLiteral("member.dateOfBirthRequired"), year};
    }
    const QString iso = m_dateOfBirthEdit->isoDate();
    if (!MemberRepository::isValidDateOfBirth(ss(iso))) {
        return {QStringLiteral("member.dateOfBirthInvalid"), day};
    }
    if (MemberRepository::isDateOfBirthInFuture(ss(iso))) {
        return {QStringLiteral("member.dateOfBirthInFuture"), year};
    }
```

Delete `setPlaceholderText` from `retranslateUi`. `loadMember` calls `setIsoDate`. `memberInput` uses `ss(m_dateOfBirthEdit->isoDate())`.

Delete `member.field.dateOfBirthHint` from the Arabic, French, and English tables, and delete the Arabic comment that belongs to that hint. Leave the invalid-date sentences, including their left-to-right marks.

- [ ] **Step 4: Run the tests**

```bash
cmake --build /home/amin/Dokumente/dev/VLMS/build --target test_vlms_ui --target test_vlms_core -j
ctest --test-dir /home/amin/Dokumente/dev/VLMS/build --output-on-failure -R '^test_ui_BirthDateEdit\.|^test_ui_DialogTranslations\.|^test_core_StringsParity\.'
```

Expected: every matched test PASSes.

- [ ] **Step 5: Do not commit**
