# Language Flag Buttons Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the header's language combo box with three exclusive circular flag buttons (Tunisia, France, United Kingdom) sitting beside the theme toggle at the same 34 px diameter.

**Architecture:** Two new units under `applications/vlms/src/ui/`. `FlagIcons` paints each flag with `QPainter` and returns a circular `QIcon` — no new assets, the way `themeModeIcon` already draws the sun and crescent. `LanguageSelector` is a `QWidget` holding three checkable `QPushButton`s in an exclusive `QButtonGroup`. `MainWindow` drops `m_languageCombo` and connects to `LanguageSelector::languageSelected`.

**Tech Stack:** C++17, Qt 6 Widgets, CMake, GoogleTest.

**Spec:** `docs/superpowers/specs/2026-09-21-language-flag-buttons-design.md`

## Global Constraints

- Flag geometry is transcribed from the Wikimedia Commons SVGs. Do not redraw by eye; the exact path data is in each task.
- Colours are the flags' own (`#e70013`, `#002654`, `#CE1126`, `#012169`, `#C8102E`), not theme tokens. Only the button chrome uses tokens.
- Button diameter is 34 px, matching `QPushButton#themeToggle` in `Theme.cpp:187-196`.
- Icon is 26 px when active, 22 px otherwise; inactive flags paint at 60% opacity.
- UI strings come from `Strings.cpp` via `VLMS::T`; the keys `lang.ar`, `lang.fr`, `lang.en` already exist and stay.
- New widget classes live in `namespace VLMS` (as `ListPageFrame` does) and are registered in `applications/vlms/CMakeLists.txt`.
- Build and test with the existing tree: `cmake --build build --target test_vlms_ui -j8`, then `./build/bin/test_vlms_ui --gtest_filter=...`.
- Tests must never construct `MainWindow` or `Application` — see the comment at `applications/vlms/test/CMakeLists.txt:1-3`.

---

### Task 1: FlagIcons — paint the three flags

**Files:**
- Create: `applications/vlms/src/ui/FlagIcons.h`
- Create: `applications/vlms/src/ui/FlagIcons.cpp`
- Create: `applications/vlms/test/src/test_flag_icons.cpp`
- Modify: `applications/vlms/CMakeLists.txt` (add both files to `vlms_ui`)
- Modify: `applications/vlms/test/CMakeLists.txt` (add the test to `test_vlms_ui`)

**Interfaces:**
- Consumes: nothing.
- Produces: `QIcon VLMS::languageFlagIcon(const QString& code, int pixelSize, qreal ratio = 1.0, bool dimmed = false)`. Returns a null `QIcon` for any code other than `"ar"`, `"fr"`, `"en"`.

- [ ] **Step 1: Write the failing test**

Create `applications/vlms/test/src/test_flag_icons.cpp`:

```cpp
#include "ui/FlagIcons.h"

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QSize>

#include <gtest/gtest.h>

using VLMS::languageFlagIcon;

namespace {

QImage rendered(const QString& code, int size = 64, bool dimmed = false)
{
    return languageFlagIcon(code, size, 1.0, dimmed).pixmap(size, size).toImage();
}

}  // namespace

TEST(test_ui_FlagIcons, EveryUiLanguageHasAFlag)
{
    for (const QString& code : {QStringLiteral("ar"), QStringLiteral("fr"), QStringLiteral("en")}) {
        SCOPED_TRACE(code.toStdString());
        EXPECT_FALSE(languageFlagIcon(code, 34).isNull());
    }
}

TEST(test_ui_FlagIcons, AnUnknownCodeHasNoFlag)
{
    EXPECT_TRUE(languageFlagIcon(QStringLiteral("de"), 34).isNull());
    EXPECT_TRUE(languageFlagIcon(QString(), 34).isNull());
}

TEST(test_ui_FlagIcons, TheFlagsAreToldApartByColour)
{
    // Tunisia's white disc, sampled above the centre: the star's left arm
    // reaches the middle, so the centre pixel is red, not white.
    EXPECT_EQ(rendered(QStringLiteral("ar")).pixelColor(32, 20), QColor(Qt::white));
    EXPECT_EQ(rendered(QStringLiteral("fr")).pixelColor(32, 32), QColor(Qt::white));
    EXPECT_EQ(rendered(QStringLiteral("en")).pixelColor(32, 32), QColor(0xC8, 0x10, 0x2E));
}

TEST(test_ui_FlagIcons, TunisiaShowsRedOutsideTheDiscAndTheStarInside)
{
    const QImage flag = rendered(QStringLiteral("ar"));
    // Just inside the circle's edge, on the horizontal centre line: the red
    // margin the 1.5x zoom leaves around the disc.
    EXPECT_EQ(flag.pixelColor(3, 32), QColor(0xE7, 0x00, 0x13));
    // The star sits right of centre and is red on white.
    EXPECT_EQ(flag.pixelColor(38, 32), QColor(0xE7, 0x00, 0x13));
}

TEST(test_ui_FlagIcons, FranceIsThreeEqualBands)
{
    const QImage flag = rendered(QStringLiteral("fr"));
    // Sampled on the centre line, one point inside each third.
    EXPECT_EQ(flag.pixelColor(12, 32), QColor(0x00, 0x26, 0x54));
    EXPECT_EQ(flag.pixelColor(32, 32), QColor(Qt::white));
    EXPECT_EQ(flag.pixelColor(52, 32), QColor(0xCE, 0x11, 0x26));
}

TEST(test_ui_FlagIcons, TheFlagIsCutToACircle)
{
    for (const QString& code : {QStringLiteral("ar"), QStringLiteral("fr"), QStringLiteral("en")}) {
        SCOPED_TRACE(code.toStdString());
        const QImage flag = rendered(code);
        // Corners fall outside the circle, so nothing is painted there.
        EXPECT_EQ(flag.pixelColor(0, 0).alpha(), 0);
        EXPECT_EQ(flag.pixelColor(63, 0).alpha(), 0);
        EXPECT_EQ(flag.pixelColor(0, 63).alpha(), 0);
        EXPECT_EQ(flag.pixelColor(63, 63).alpha(), 0);
        // The centre is opaque.
        EXPECT_EQ(flag.pixelColor(32, 32).alpha(), 255);
    }
}

TEST(test_ui_FlagIcons, ADimmedFlagIsTheSameFlagFaded)
{
    const QImage full = rendered(QStringLiteral("fr"));
    const QImage dim = rendered(QStringLiteral("fr"), 64, true);

    EXPECT_EQ(full.size(), dim.size());
    EXPECT_LT(dim.pixelColor(32, 32).alpha(), full.pixelColor(32, 32).alpha());
    EXPECT_GT(dim.pixelColor(32, 32).alpha(), 0);
}

TEST(test_ui_FlagIcons, TheIconIsPaintedAtTheSizeAsked)
{
    EXPECT_EQ(languageFlagIcon(QStringLiteral("en"), 16).pixmap(16, 16).size(), QSize(16, 16));
    EXPECT_EQ(languageFlagIcon(QStringLiteral("en"), 48).pixmap(48, 48).size(), QSize(48, 48));
    // A high-DPI ratio paints more pixels into the same logical square. How Qt
    // then hands that pixmap back is Qt's business, not this test's.
    EXPECT_FALSE(languageFlagIcon(QStringLiteral("en"), 34, 2.0).isNull());
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

In `applications/vlms/test/CMakeLists.txt`, add `src/test_flag_icons.cpp` to the `SRC` list of `test_vlms_ui`, directly after `src/test_theme_modes.cpp`.

Run: `cmake --build build --target test_vlms_ui -j8`
Expected: FAIL — `fatal error: ui/FlagIcons.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `applications/vlms/src/ui/FlagIcons.h`:

```cpp
#pragma once

#include <QIcon>
#include <QString>

namespace VLMS {

/// The flag for a UI language code -- "ar" (Tunisia), "fr" (France), "en" (the
/// United Kingdom) -- drawn rather than shipped as artwork, the way
/// themeModeIcon draws the sun and crescent. The application links only
/// Core/Gui/Widgets, so an SVG asset would pull the qsvg plugin into every
/// deployment; the geometry is transcribed from the Wikimedia Commons file for
/// each flag instead, all of which are public domain.
///
/// Returns a null icon for any other code.
/// \param ratio the target widget's device pixel ratio.
/// \param dimmed paints at reduced opacity, for a language that is not in use.
QIcon languageFlagIcon(const QString& code, int pixelSize, qreal ratio = 1.0,
                       bool dimmed = false);

}  // namespace VLMS
```

- [ ] **Step 4: Write the implementation**

Create `applications/vlms/src/ui/FlagIcons.cpp`:

```cpp
#include "ui/FlagIcons.h"

#include <QImage>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRectF>

namespace VLMS {
namespace {

// What a language you are not using is worth: present enough to aim at, quiet
// enough that the picked flag is the only one that reads at a glance.
constexpr qreal kDimmedOpacity = 0.6;

// Flag_of_France.svg (900x600). Scaled to the square rather than centre
// cropped: a crop of a 3:2 tricolour shows the white band at double width.
void paintFrance(QPainter& painter, qreal side)
{
    const qreal band = side / 3.0;
    painter.fillRect(QRectF(0.0, 0.0, band, side), QColor(0x00, 0x26, 0x54));
    painter.fillRect(QRectF(band, 0.0, band, side), QColor(Qt::white));
    painter.fillRect(QRectF(band * 2.0, 0.0, side - band * 2.0, side),
                     QColor(0xCE, 0x11, 0x26));
}

// Flag_of_the_United_Kingdom_(3-5).svg, viewBox "0 0 50 30". Scaled
// non-uniformly to the square so the whole design survives: cropping a 3:5
// flag to a circle throws away the diagonals and leaves a fat cross.
void paintUnitedKingdom(QPainter& painter, qreal side)
{
    const QColor red(0xC8, 0x10, 0x2E);

    painter.save();
    painter.scale(side / 50.0, side / 30.0);
    painter.fillRect(QRectF(0.0, 0.0, 50.0, 30.0), QColor(0x01, 0x21, 0x69));

    const QLineF saltire[2] = {QLineF(0.0, 0.0, 50.0, 30.0),
                               QLineF(50.0, 0.0, 0.0, 30.0)};

    QPen white(QColor(Qt::white));
    white.setWidthF(6.0);
    painter.setPen(white);
    painter.drawLines(saltire, 2);

    // The red saltire is counterchanged -- offset within each arm rather than
    // centred on it. The source does that by clipping the same lines to four
    // triangles: "M25,15h25v15zv15h-25zh-25v-15zv-15h25z".
    QPainterPath counterchange;
    const QPointF centre(25.0, 15.0);
    const QPointF corners[4][2] = {
        {QPointF(50.0, 15.0), QPointF(50.0, 30.0)},
        {QPointF(25.0, 30.0), QPointF(0.0, 30.0)},
        {QPointF(0.0, 15.0), QPointF(0.0, 0.0)},
        {QPointF(25.0, 0.0), QPointF(50.0, 0.0)},
    };
    for (const auto& triangle : corners) {
        counterchange.moveTo(centre);
        counterchange.lineTo(triangle[0]);
        counterchange.lineTo(triangle[1]);
        counterchange.closeSubpath();
    }

    painter.save();
    painter.setClipPath(counterchange);
    QPen redPen(red);
    redPen.setWidthF(4.0);
    painter.setPen(redPen);
    painter.drawLines(saltire, 2);
    painter.restore();

    // St George's cross, one path filled red and stroked white:
    // "M-1 11h22v-12h8v12h22v8h-22v12h-8v-12h-22z".
    QPainterPath cross;
    cross.moveTo(-1.0, 11.0);
    cross.lineTo(21.0, 11.0);
    cross.lineTo(21.0, -1.0);
    cross.lineTo(29.0, -1.0);
    cross.lineTo(29.0, 11.0);
    cross.lineTo(51.0, 11.0);
    cross.lineTo(51.0, 19.0);
    cross.lineTo(29.0, 19.0);
    cross.lineTo(29.0, 31.0);
    cross.lineTo(21.0, 31.0);
    cross.lineTo(21.0, 19.0);
    cross.lineTo(-1.0, 19.0);
    cross.closeSubpath();

    QPen crossPen(QColor(Qt::white));
    crossPen.setWidthF(2.0);
    painter.setPen(crossPen);
    painter.setBrush(red);
    painter.drawPath(cross);
    painter.restore();
}

// Flag_of_Tunisia.svg, viewBox "-60 -40 120 80". Uniform scale -- a stretch
// would turn the disc into an ellipse -- zoomed 1.5x about the centre, which
// grows the disc from half the circle to three quarters of it and leaves the
// red margin half its original width.
void paintTunisia(QPainter& painter, qreal side)
{
    static constexpr qreal kZoom = 1.5;
    const QColor red(0xE7, 0x00, 0x13);

    painter.save();
    painter.translate(side / 2.0, side / 2.0);
    painter.scale(side * kZoom / 80.0, side * kZoom / 80.0);

    painter.setPen(Qt::NoPen);
    painter.fillRect(QRectF(-60.0, -40.0, 120.0, 80.0), red);

    // The crescent is cut, not stroked: a white disc, a red disc inside it,
    // and a white disc pushed right, which leaves the horns sharp.
    painter.setBrush(QColor(Qt::white));
    painter.drawEllipse(QPointF(0.0, 0.0), 20.0, 20.0);
    painter.setBrush(red);
    painter.drawEllipse(QPointF(0.0, 0.0), 15.0, 15.0);
    painter.setBrush(QColor(Qt::white));
    painter.drawEllipse(QPointF(4.0, 0.0), 12.0, 12.0);

    // "M-5 0l16.281-5.29L1.22 8.56V-8.56L11.28 5.29z" -- a pentagram, so it
    // needs the winding fill rule; Qt's default would hollow out the middle.
    QPainterPath star;
    star.setFillRule(Qt::WindingFill);
    star.moveTo(-5.0, 0.0);
    star.lineTo(11.281, -5.29);
    star.lineTo(1.22, 8.56);
    star.lineTo(1.22, -8.56);
    star.lineTo(11.28, 5.29);
    star.closeSubpath();
    painter.setBrush(red);
    painter.drawPath(star);

    painter.restore();
}

}  // namespace

QIcon languageFlagIcon(const QString& code, int pixelSize, qreal ratio, bool dimmed)
{
    void (*paint)(QPainter&, qreal) = nullptr;
    if (code == QLatin1String("ar")) {
        paint = &paintTunisia;
    } else if (code == QLatin1String("fr")) {
        paint = &paintFrance;
    } else if (code == QLatin1String("en")) {
        paint = &paintUnitedKingdom;
    } else {
        return {};
    }

    const int side = qMax(1, qRound(pixelSize * ratio));
    QPixmap canvas(side, side);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (dimmed) {
        painter.setOpacity(kDimmedOpacity);
    }
    paint(painter, static_cast<qreal>(side));

    // The circle is cut with a mask rather than a clip path: QPainter clipping
    // is aliased, and a stair-stepped edge is the one thing that would give
    // away that these are painted rather than drawn artwork.
    QImage mask(side, side, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter maskPainter(&mask);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        maskPainter.setPen(Qt::NoPen);
        maskPainter.setBrush(QColor(Qt::black));
        maskPainter.drawEllipse(QRectF(0.0, 0.0, side, side));
    }
    painter.setOpacity(1.0);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(0, 0, mask);
    painter.end();

    canvas.setDevicePixelRatio(ratio);
    return QIcon(canvas);
}

}  // namespace VLMS
```

- [ ] **Step 5: Register the sources**

In `applications/vlms/CMakeLists.txt`, add `src/ui/FlagIcons.cpp` after `src/ui/Theme.cpp` in the source list, and `src/ui/FlagIcons.h` after `src/ui/Theme.h` in the header list.

- [ ] **Step 6: Run the tests to verify they pass**

Run:
```bash
cmake --build build --target test_vlms_ui -j8 && \
  ./build/bin/test_vlms_ui --gtest_filter='test_ui_FlagIcons.*'
```
Expected: PASS, 8 tests.

If a colour probe is off by a pixel, print the actual colour and move the probe — do not loosen the assertion to `EXPECT_TRUE`.

- [ ] **Step 7: Commit**

```bash
git add applications/vlms/src/ui/FlagIcons.h \
        applications/vlms/src/ui/FlagIcons.cpp \
        applications/vlms/test/src/test_flag_icons.cpp \
        applications/vlms/CMakeLists.txt \
        applications/vlms/test/CMakeLists.txt
git commit -m "Draw the three UI-language flags instead of shipping artwork."
```

---

### Task 2: LanguageSelector — the exclusive button row

**Files:**
- Create: `applications/vlms/src/ui/LanguageSelector.h`
- Create: `applications/vlms/src/ui/LanguageSelector.cpp`
- Create: `applications/vlms/test/src/test_language_selector.cpp`
- Modify: `applications/vlms/CMakeLists.txt`
- Modify: `applications/vlms/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `VLMS::languageFlagIcon` from Task 1.
- Produces: `VLMS::LanguageSelector` with `currentLanguage() const`, `setCurrentLanguage(const QString&)`, `retranslateUi()`, and the signal `languageSelected(const QString& code)`. Buttons are named `languageButton` and carry an `active` property plus a `langCode` property.

- [ ] **Step 1: Write the failing test**

Create `applications/vlms/test/src/test_language_selector.cpp`:

```cpp
#include "ui/LanguageSelector.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>

#include <QPushButton>
#include <QSignalSpy>

#include <gtest/gtest.h>

using VLMS::LanguageSelector;
using VLMS::Locale;

namespace {

QPushButton* buttonFor(LanguageSelector& selector, const QString& code)
{
    for (QPushButton* button : selector.findChildren<QPushButton*>()) {
        if (button->property("langCode").toString() == code) {
            return button;
        }
    }
    return nullptr;
}

}  // namespace

TEST(test_ui_LanguageSelector, ItOffersTheThreeUiLanguages)
{
    LanguageSelector selector;
    for (const QString& code : {QStringLiteral("ar"), QStringLiteral("fr"), QStringLiteral("en")}) {
        SCOPED_TRACE(code.toStdString());
        QPushButton* button = buttonFor(selector, code);
        ASSERT_NE(button, nullptr);
        EXPECT_TRUE(button->isCheckable());
        EXPECT_FALSE(button->icon().isNull());
    }
}

TEST(test_ui_LanguageSelector, OnlyOneLanguageIsEverPicked)
{
    LanguageSelector selector;
    selector.setCurrentLanguage(QStringLiteral("ar"));

    buttonFor(selector, QStringLiteral("fr"))->click();

    EXPECT_EQ(selector.currentLanguage(), QStringLiteral("fr"));
    EXPECT_TRUE(buttonFor(selector, QStringLiteral("fr"))->isChecked());
    EXPECT_FALSE(buttonFor(selector, QStringLiteral("ar"))->isChecked());
    EXPECT_FALSE(buttonFor(selector, QStringLiteral("en"))->isChecked());
}

TEST(test_ui_LanguageSelector, PickingALanguageAnnouncesItsCode)
{
    LanguageSelector selector;
    selector.setCurrentLanguage(QStringLiteral("ar"));
    QSignalSpy spy(&selector, &LanguageSelector::languageSelected);

    buttonFor(selector, QStringLiteral("en"))->click();

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), QStringLiteral("en"));
}

TEST(test_ui_LanguageSelector, ClickingTheLanguageAlreadyInUseChangesNothing)
{
    LanguageSelector selector;
    selector.setCurrentLanguage(QStringLiteral("fr"));
    QSignalSpy spy(&selector, &LanguageSelector::languageSelected);

    buttonFor(selector, QStringLiteral("fr"))->click();

    EXPECT_EQ(spy.count(), 0);
    // And it cannot be toggled off: the row always has a language.
    EXPECT_TRUE(buttonFor(selector, QStringLiteral("fr"))->isChecked());
    EXPECT_EQ(selector.currentLanguage(), QStringLiteral("fr"));
}

TEST(test_ui_LanguageSelector, SettingTheLanguageDoesNotAnnounceIt)
{
    // The window sets the selector from the locale on every retranslate; that
    // must not loop back into a locale change.
    LanguageSelector selector;
    QSignalSpy spy(&selector, &LanguageSelector::languageSelected);

    selector.setCurrentLanguage(QStringLiteral("en"));

    EXPECT_EQ(spy.count(), 0);
    EXPECT_EQ(selector.currentLanguage(), QStringLiteral("en"));
    EXPECT_TRUE(buttonFor(selector, QStringLiteral("en"))->isChecked());
}

TEST(test_ui_LanguageSelector, AnUnknownCodeLeavesTheSelectionAlone)
{
    LanguageSelector selector;
    selector.setCurrentLanguage(QStringLiteral("fr"));

    selector.setCurrentLanguage(QStringLiteral("de"));

    EXPECT_EQ(selector.currentLanguage(), QStringLiteral("fr"));
}

TEST(test_ui_LanguageSelector, ThePickedFlagIsMarkedForTheStylesheet)
{
    LanguageSelector selector;
    selector.setCurrentLanguage(QStringLiteral("ar"));

    EXPECT_TRUE(buttonFor(selector, QStringLiteral("ar"))->property("active").toBool());
    EXPECT_FALSE(buttonFor(selector, QStringLiteral("fr"))->property("active").toBool());
    // The picked flag is drawn larger than the two it sits beside.
    EXPECT_GT(buttonFor(selector, QStringLiteral("ar"))->iconSize().width(),
              buttonFor(selector, QStringLiteral("fr"))->iconSize().width());
}

TEST(test_ui_LanguageSelector, TooltipsFollowTheUiLanguage)
{
    const std::string restore = Locale::code();
    LanguageSelector selector;

    Locale::setCode("en");
    selector.retranslateUi();
    EXPECT_EQ(buttonFor(selector, QStringLiteral("fr"))->toolTip(), QStringLiteral("Français"));

    Locale::setCode("ar");
    selector.retranslateUi();
    EXPECT_EQ(buttonFor(selector, QStringLiteral("ar"))->toolTip(),
              QString::fromUtf8("العربية"));
    // A screen reader gets nothing but this.
    EXPECT_FALSE(buttonFor(selector, QStringLiteral("en"))->accessibleName().isEmpty());

    Locale::setCode(restore);
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

Add `src/test_language_selector.cpp` to the `SRC` list in `applications/vlms/test/CMakeLists.txt`, after `src/test_flag_icons.cpp`.

Run: `cmake --build build --target test_vlms_ui -j8`
Expected: FAIL — `fatal error: ui/LanguageSelector.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `applications/vlms/src/ui/LanguageSelector.h`:

```cpp
#pragma once

#include <QString>
#include <QWidget>

class QButtonGroup;

namespace VLMS {

/**
 * The three UI languages as circular flag buttons, exactly one pressed.
 *
 * Its own widget rather than a row built inside MainWindow, because MainWindow
 * reaches for qobject_cast<Application*>(qApp) and so cannot be constructed in
 * a test; this can.
 */
class LanguageSelector final : public QWidget {
    Q_OBJECT

public:
    /// The diameter of QPushButton#themeToggle, which this row sits beside.
    static constexpr int kButtonSize = 34;
    static constexpr int kActiveIconSize = 26;
    static constexpr int kIdleIconSize = 22;

    explicit LanguageSelector(QWidget* parent = nullptr);

    QString currentLanguage() const;
    /// Moves the selection without announcing it, for following the locale.
    /// Ignores a code the row does not offer.
    void setCurrentLanguage(const QString& code);
    /// Re-reads the tooltips and repaints the flags, which a theme change
    /// needs as much as a language change does.
    void retranslateUi();

signals:
    void languageSelected(const QString& code);

private:
    void updateButtons();

    QButtonGroup* m_group = nullptr;
    QString m_current;
};

}  // namespace VLMS
```

- [ ] **Step 4: Write the implementation**

Create `applications/vlms/src/ui/LanguageSelector.cpp`:

```cpp
#include "ui/LanguageSelector.h"

#include <VLMS/Core/Strings.h>
#include "ui/FlagIcons.h"
#include "ui/UiHelpers.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSize>
#include <QStringList>

namespace VLMS {
namespace {

// Layout order. The header inherits the application direction, so Arabic
// mirrors the row without a second list.
const QStringList& languageCodes()
{
    static const QStringList codes{QStringLiteral("ar"), QStringLiteral("fr"),
                                   QStringLiteral("en")};
    return codes;
}

}  // namespace

LanguageSelector::LanguageSelector(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("languageSelector"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    for (const QString& code : languageCodes()) {
        auto* button = new QPushButton(this);
        button->setObjectName(QStringLiteral("languageButton"));
        button->setProperty("langCode", code);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::StrongFocus);
        button->setFixedSize(kButtonSize, kButtonSize);
        m_group->addButton(button);
        layout->addWidget(button);
    }

    m_current = languageCodes().constFirst();

    connect(m_group, &QButtonGroup::buttonClicked, this, [this](QAbstractButton* button) {
        const QString code = button->property("langCode").toString();
        // An exclusive group keeps the pressed button pressed, so a second
        // click on the language already in use is not a change.
        if (code == m_current) {
            return;
        }
        m_current = code;
        updateButtons();
        emit languageSelected(code);
    });

    retranslateUi();
}

QString LanguageSelector::currentLanguage() const
{
    return m_current;
}

void LanguageSelector::setCurrentLanguage(const QString& code)
{
    if (!languageCodes().contains(code)) {
        return;
    }
    m_current = code;
    updateButtons();
}

void LanguageSelector::retranslateUi()
{
    for (QAbstractButton* button : m_group->buttons()) {
        const QString code = button->property("langCode").toString();
        const QString name = T(ss(QStringLiteral("lang.%1").arg(code)));
        // The flags carry no text, so the name of the language lives here and
        // in the accessible name, which is all a screen reader is given.
        button->setToolTip(name);
        button->setAccessibleName(name);
    }
    updateButtons();
}

void LanguageSelector::updateButtons()
{
    for (QAbstractButton* button : m_group->buttons()) {
        const QString code = button->property("langCode").toString();
        const bool active = (code == m_current);
        const int iconSize = active ? kActiveIconSize : kIdleIconSize;

        button->setChecked(active);
        button->setProperty("active", active);
        button->setIcon(languageFlagIcon(code, iconSize, devicePixelRatioF(), !active));
        button->setIconSize(QSize(iconSize, iconSize));
        refreshWidgetStyle(button);
    }
}

}  // namespace VLMS
```

- [ ] **Step 5: Register the sources**

In `applications/vlms/CMakeLists.txt`, add `src/ui/LanguageSelector.cpp` after `src/ui/FlagIcons.cpp`, and `src/ui/LanguageSelector.h` after `src/ui/FlagIcons.h`.

- [ ] **Step 6: Run the tests to verify they pass**

Run:
```bash
cmake --build build --target test_vlms_ui -j8 && \
  ./build/bin/test_vlms_ui --gtest_filter='test_ui_LanguageSelector.*'
```
Expected: PASS, 8 tests.

Note on `ss` and `T`: both come from `VLMS/Core/Strings.h` and are used the same way in `MainWindow.cpp:407`. If the compiler cannot find them, add `using VLMS::T;` / `using VLMS::ss;` — inside `namespace VLMS` they should resolve unqualified.

- [ ] **Step 7: Commit**

```bash
git add applications/vlms/src/ui/LanguageSelector.h \
        applications/vlms/src/ui/LanguageSelector.cpp \
        applications/vlms/test/src/test_language_selector.cpp \
        applications/vlms/CMakeLists.txt \
        applications/vlms/test/CMakeLists.txt
git commit -m "Offer the UI languages as one exclusive row of flags."
```

---

### Task 3: Style the buttons

**Files:**
- Modify: `applications/vlms/src/ui/Theme.cpp` (after the `QPushButton#themeToggle` rules, around line 202)
- Modify: `applications/vlms/test/src/test_theme_modes.cpp`

**Interfaces:**
- Consumes: the `languageButton` object name and `active` property from Task 2.
- Produces: nothing new in code.

- [ ] **Step 1: Write the failing test**

Append to `applications/vlms/test/src/test_theme_modes.cpp`:

```cpp
TEST(test_ui_ThemeModes, TheLanguageButtonsAreRoundAndMarkTheActiveOne)
{
    for (const ThemeMode mode : {ThemeMode::Light, ThemeMode::Dark}) {
        SCOPED_TRACE(static_cast<int>(mode));
        const QString sheet = applicationStylesheet(mode);
        EXPECT_TRUE(sheet.contains(QLatin1String("QPushButton#languageButton")));
        EXPECT_TRUE(sheet.contains(QLatin1String("QPushButton#languageButton[active=\"true\"]")));
        // Round, and pinned to the diameter the radius assumes -- the same
        // treatment QPushButton#themeToggle gets right above it.
        EXPECT_TRUE(sheet.contains(QLatin1String("border-radius: 17px")));
    }
}
```

- [ ] **Step 2: Run it to verify it fails**

Run:
```bash
cmake --build build --target test_vlms_ui -j8 && \
  ./build/bin/test_vlms_ui --gtest_filter='test_ui_ThemeModes.TheLanguageButtonsAreRoundAndMarkTheActiveOne'
```
Expected: FAIL — the sheet has no `QPushButton#languageButton` rule.

- [ ] **Step 3: Add the rules**

In `applications/vlms/src/ui/Theme.cpp`, immediately after the `QPushButton#themeToggle:hover` block (which ends around line 202) and before `QWidget#appFooter`, insert:

```
/* The language flags, sized and rounded like the theme toggle beside them. The
   border is transparent rather than absent so that marking the active one does
   not move the icon by a pixel. */
QPushButton#languageButton {
  background: transparent;
  border: 1px solid transparent;
  border-radius: 17px;
  min-width: 34px;
  max-width: 34px;
  min-height: 34px;
  max-height: 34px;
  padding: 0px;
}

QPushButton#languageButton:hover {
  background: @hoverBg;
}

QPushButton#languageButton[active="true"] {
  background: @pressedBg;
  border-color: @focusBorder;
}

QPushButton#languageButton:focus {
  border: 2px solid @focusBorder;
}
```

- [ ] **Step 4: Run the whole theme suite**

Run:
```bash
cmake --build build --target test_vlms_ui -j8 && \
  ./build/bin/test_vlms_ui --gtest_filter='test_ui_ThemeModes.*'
```
Expected: PASS, all tests. `NoPlaceholderSurvivesSubstitution` and `ModesProduceDifferentSheets` must still pass — the new rules use only `@hoverBg`, `@pressedBg` and `@focusBorder`, all of which are in the token table at `Theme.cpp:930-944`, and add no literal hex colour to either sheet.

- [ ] **Step 5: Commit**

```bash
git add applications/vlms/src/ui/Theme.cpp \
        applications/vlms/test/src/test_theme_modes.cpp
git commit -m "Round the language buttons and fill the one in use."
```

---

### Task 4: Wire it into the header

**Files:**
- Modify: `applications/vlms/src/ui/MainWindow.h:24,43`
- Modify: `applications/vlms/src/ui/MainWindow.cpp:17,97-105,126,336-350,402-413`

**Interfaces:**
- Consumes: `VLMS::LanguageSelector` from Task 2.
- Produces: nothing new.

There is no test for this task — `MainWindow` cannot be constructed in the suite. Its correctness is checked by Task 5, running the application.

- [ ] **Step 1: Change the header**

In `applications/vlms/src/ui/MainWindow.h`:

Replace the forward declaration `class QComboBox;` with nothing if no other member needs it (check: `m_languageCombo` is the only `QComboBox` in this class), and add to the existing forward declarations:

```cpp
namespace VLMS {
class LanguageSelector;
}
```

Change the slot declaration on line 24 from:

```cpp
    void onLanguageChanged(int index);
```

to:

```cpp
    void onLanguageChanged(const QString& code);
```

Change the member on line 43 from:

```cpp
    QComboBox* m_languageCombo = nullptr;
```

to:

```cpp
    VLMS::LanguageSelector* m_languageSelector = nullptr;
```

- [ ] **Step 2: Build the row in buildUi**

In `applications/vlms/src/ui/MainWindow.cpp`, replace `#include <QComboBox>` (line 17) with `#include "ui/LanguageSelector.h"` placed with the other project includes, after `#include "ui/catalog/CatalogPage.h"`.

Replace lines 97-105:

```cpp
    m_languageCombo = new QComboBox(header);
    m_languageCombo->setObjectName(QStringLiteral("languageCombo"));
    m_languageCombo->setMinimumWidth(132);
    for (const QString& code : {QStringLiteral("ar"), QStringLiteral("fr"), QStringLiteral("en")}) {
        m_languageCombo->addItem(
            VLMS::T(ss(QStringLiteral("lang.%1").arg(code))),
            code);
    }
    headerLayout->addWidget(m_languageCombo);
```

with:

```cpp
    m_languageSelector = new VLMS::LanguageSelector(header);
    headerLayout->addWidget(m_languageSelector);
```

- [ ] **Step 3: Reconnect the signal**

Replace line 126:

```cpp
    connect(m_languageCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onLanguageChanged);
```

with:

```cpp
    connect(m_languageSelector, &VLMS::LanguageSelector::languageSelected,
            this, &MainWindow::onLanguageChanged);
```

- [ ] **Step 4: Simplify the slot**

Replace the whole of `MainWindow::onLanguageChanged` (lines 336-350) with:

```cpp
void MainWindow::onLanguageChanged(const QString& code)
{
    auto* app = qobject_cast<Application*>(QApplication::instance());
    if (app == nullptr) {
        return;
    }

    if (code.isEmpty() || code == app->uiLocale()) {
        return;
    }

    app->setUiLocale(code);
}
```

- [ ] **Step 5: Follow the locale in retranslateUi**

Replace lines 402-413 (from `const QString currentLocale = ...` through `m_languageCombo->blockSignals(false);`) with:

```cpp
    m_languageSelector->setCurrentLanguage(VLMS::qs(VLMS::Locale::code()));
    m_languageSelector->retranslateUi();
```

`setCurrentLanguage` does not emit, so the `blockSignals` dance the combo needed is gone.

- [ ] **Step 6: Build the application and the whole test suite**

Run:
```bash
cmake --build build -j8 && ./build/bin/test_vlms_ui
```
Expected: the application links, and every UI test passes. If the compiler reports `ss` or `T` unused in `MainWindow.cpp`, leave the `using` declarations alone — other call sites still use them.

- [ ] **Step 7: Commit**

```bash
git add applications/vlms/src/ui/MainWindow.h \
        applications/vlms/src/ui/MainWindow.cpp
git commit -m "Put the flag row in the header where the combo box was."
```

---

### Task 5: See it running

**Files:**
- Modify: `CLAUDE.md` (Session log)

- [ ] **Step 1: Run the whole suite**

Run:
```bash
cmake --build build -j8 && ctest --test-dir build --output-on-failure
```
Expected: every test passes. Report the actual counts; do not claim success without the output.

- [ ] **Step 2: Launch the application and screenshot it**

Follow the screenshot recipe: run under xcb, not Wayland, capture by window id, and keep the process in the foreground.

```bash
QT_QPA_PLATFORM=xcb ./build/bin/vlms
```

Check, in the window: the three flags sit to the left of the theme toggle at the same diameter; the Arabic flag is filled and ringed; clicking the French flag switches the whole UI to French and moves the fill; the theme toggle still works and the flags stay legible in dark mode.

- [ ] **Step 3: Note it in the session log**

Add to the top of the Session log in `CLAUDE.md`:

```markdown
- 2026-09-21 — The header language combo is now three circular flag buttons (`LanguageSelector`,
  exclusive `QButtonGroup`) beside the theme toggle; the active one is filled and ringed via the
  `active` property, like `navLink`. Flags are painted in `FlagIcons.cpp`, transcribed from the
  public-domain Commons SVGs — no new assets, because the app links only Core/Gui/Widgets and an
  SVG would pull in the qsvg plugin. France and the UK are scaled to the square; Tunisia is zoomed
  1.5x uniformly so its disc is not an ellipse. Spec:
  `docs/superpowers/specs/2026-09-21-language-flag-buttons-design.md`.
```

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md
git commit -m "Note the language flag row in the session log."
```
