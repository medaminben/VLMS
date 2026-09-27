# Manual button Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use subagent-driven-development (recommended) or executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Help word in the page row with a round "?" beside the theme toggle, and open the manual in a browser window this application starts itself so a second click brings that window forward.

**Architecture:** `manualIndexPath` stays as it is. Two new functions in `ManualLocation` build the ordered browser list and the four launch arguments; `MainWindow` filters that list to programs that exist, then `QProcess::startDetached` runs the first one. The same command is issued on every click. Chromium and Edge join the window they already have for that `--app` address and leave the page alone; nothing in this application tracks the window. Arabic and French are two addresses, so they are two windows. The painted "?" is an icon next to `themeModeIcon`, and the stylesheet names `manualButton` on the same rules as `themeToggle`.

**Tech Stack:** C++20 / Qt 6 Widgets (`QPushButton`, `QProcess`, `QStandardPaths`, `QUrl`); GoogleTest via `ctest`; the existing Python manual generator `scripts/build_manual.py`; the existing offscreen capture tool `scripts/manual/capture.sh`.

Spec: `docs/superpowers/specs/2026-09-23-manual-button-design.md` (read it; this plan replaces the "Opening the manual from the application" behaviour of `docs/superpowers/specs/2026-09-23-user-manual-wiki-design.md`).

## Global Constraints

- The Help word leaves the page row. The circle is the only way to open the manual.
- `QPushButton` object name `manualButton`, placed immediately after the theme toggle. The cluster reads flags, theme, "?".
- The `themeToggle` stylesheet rules gain `manualButton` in the same selectors: 34px circle, 1px border, radius 17px.
- The mark is a painted "?" at 18px, in the theme icon's muted ink (`textMuted`), redrawn when the theme changes. The button carries no text.
- Tooltip is the existing `help.tooltip`. Accessible name is the existing `nav.help`. Do not change those three existing keys (`nav.help`, `help.tooltip`, `help.notFound`).
- Linux candidates, in order: `chromium`, `chromium-browser`, `google-chrome`, `google-chrome-stable`. Windows candidates, in order: `%ProgramFiles%\Microsoft\Edge\Application\msedge.exe`, `%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe`, then `msedge` on `PATH`. Firefox is never a candidate.
- Arguments, in order: `--user-data-dir=<profile>`, `--app=<file URL of the index>`, `--no-first-run`, `--disable-extensions`.
- Profile directory: `manual-browser` under `QStandardPaths::AppDataLocation` (`VLMS` / `VLMS`, already set in `Application`).
- An empty index shows `help.notFound` and does not look for a browser. An empty browser list, or `QProcess::startDetached` returning false, shows `help.noBrowser`.
- `help.noBrowser` sentences, copied verbatim: ar `تعذّر العثور على متصفح لفتح دليل الاستخدام.`, fr `Aucun navigateur n'a été trouvé pour ouvrir le manuel d'utilisation.`, en `No browser was found to open the user manual.`
- `manualIndexPath` is not edited.
- A second click runs the same command again. Do not add window-tracking, Firefox, a PDF, or any browser outside the lists above.
- British English in comments and in the English manual sentences. Do not rename identifiers.
- Implementers do not commit. The user commits only when they ask. There is no `git commit` step.
- One build directory for this plan: `build-sdd-manual-button` (gitignored via `/build-*/`). Judge C++ tests with `ctest` from that directory, never by running a test binary directly.
- The capture tool runs only through `scripts/manual/capture.sh` (`QT_QPA_PLATFORM=offscreen`). It reads `database/vlms.db` only via `prepare_sandbox.py`. Never write the live database, and never copy `resources/members/**`.

## Decisions the spec left unnamed

These are fixed here so a later task does not choose again.

- `manualBrowserLaunch` returns `VLMS::ManualBrowserCommand` (`QString program`, `QStringList arguments`). An empty browser list returns a default command: empty program and empty argument list.
- The file URL is `QUrl::fromLocalFile(indexPath).toString(QUrl::FullyEncoded)`. A space becomes `%20`. `QUrl::toLocalFile()` on that URL returns the original path, which is what "the space stays intact" means. Do not put a raw space in `--app=`.
- `MainWindow` keeps an absolute candidate when `QFileInfo::exists` is true, and keeps `QStandardPaths::findExecutable`'s returned path for a bare name. The launch program is that kept path, not the bare name.
- The warning title for both `help.notFound` and `help.noBrowser` stays `T("nav.help")`, the title the missing-manual warning already uses.
- The six Help sentences are rewritten as specified in Task 4. Getting-started item 1 loses the Help clause. Getting-started item 3 (the theme button, which the "?" sits beside) gains the sentence that the "?" opens this manual. The ten callouts on `gs-window` stay as they are; the "?" is in the picture because the grab is the whole window, and it does not get an eleventh number.
- The install comment in `applications/vlms/CMakeLists.txt` is updated in the same words as `cmake/WindowsPackaging.cmake`. The spec names only the packaging comment; the CMakeLists comment currently says the same stale thing ("the header's Help button").
- The "?" is drawn with the application font Cairo (`kFontFamily` in `Theme.cpp`), centred in an 18px icon, same `setIconSize` as the theme toggle. Cursor and focus match the theme toggle (`PointingHandCursor`, `StrongFocus`).

## File structure

```
applications/vlms/src/ui/ManualLocation.h    declarations (Task 1)
applications/vlms/src/ui/ManualLocation.cpp   candidates + launch (Task 1)
applications/vlms/test/src/test_manual_location.cpp   (Task 1)
libraries/Core/src/Strings.cpp                     help.noBrowser × 3 (Task 2)
libraries/Core/test/src/test_strings_parity.cpp    exact sentences (Task 2)
applications/vlms/src/ui/Theme.h              manualButtonIcon (Task 3)
applications/vlms/src/ui/Theme.cpp            stylesheet + icon (Task 3)
applications/vlms/src/ui/MainWindow.h/.cpp    drop m_helpNav, add the circle (Task 3)
cmake/WindowsPackaging.cmake                       comment (Task 3)
applications/vlms/CMakeLists.txt              install comment only (Task 3)
docs/manual/content/{ar,en,fr}/index.md            (Task 4)
docs/manual/content/{ar,en,fr}/getting-started.md  (Task 4)
docs/manual/{ar,en,fr}/*.html                      rewritten by build_manual.py (Task 4)
docs/manual/assets/shots/{ar,en,fr}/<id>.png       twelve full-window shots (Task 5)
```

No new source file. `manualIndexPath`'s body is not touched. Capture code is not touched.

---

### Task 1: Browser list and launch command

**Files:**
- Modify: `applications/vlms/src/ui/ManualLocation.h`
- Modify: `applications/vlms/src/ui/ManualLocation.cpp`
- Test: `applications/vlms/test/src/test_manual_location.cpp` (append; leave the four existing tests)

**Interfaces:**
- Consumes: nothing new. `manualIndexPath` stays.
- Produces:

```cpp
namespace VLMS {

struct ManualBrowserCommand {
    QString program;
    QStringList arguments;
};

/// Linux (windows == false): chromium, chromium-browser, google-chrome,
/// google-chrome-stable. The two directory arguments are ignored.
/// Windows: <programFiles>/Microsoft/Edge/Application/msedge.exe,
/// <programFilesX86>/Microsoft/Edge/Application/msedge.exe, then the bare name msedge.
[[nodiscard]] QStringList manualBrowserCandidates(bool windows,
                                                  const QString& programFiles,
                                                  const QString& programFilesX86);

/// program is browsers.first(). arguments are the four flags in the spec.
/// An empty browsers list returns an empty program and an empty argument list.
/// The --app value is QUrl::fromLocalFile(indexPath).toString(QUrl::FullyEncoded).
[[nodiscard]] ManualBrowserCommand manualBrowserLaunch(const QStringList& browsers,
                                                       const QString& indexPath,
                                                       const QString& profileDir);

}
```

- [x] **Step 1: Configure the build once**

From the repository root:

```bash
cmake -S . -B build-sdd-manual-button -DCMAKE_BUILD_TYPE=Debug
```

Expected: the configure step finishes and `build-sdd-manual-button` exists. Later tasks reuse this directory. Do not pass `-DVLMS_MANUAL_CAPTURE` here; Task 5's `capture.sh` configures its own `build-manual`.

- [x] **Step 2: Write the failing tests**

Append to `applications/vlms/test/src/test_manual_location.cpp`. Add `#include <QUrl>` next to the existing Qt includes. Do not change the four tests already in the file.

```cpp
TEST(test_ui_ManualLocation, LinuxCandidatesAreTheFourBrowsersInOrder)
{
    const QStringList candidates = VLMS::manualBrowserCandidates(
        false, QStringLiteral("C:/Program Files"), QStringLiteral("C:/Program Files (x86)"));
    ASSERT_EQ(candidates.size(), 4);
    EXPECT_EQ(candidates.at(0), QStringLiteral("chromium"));
    EXPECT_EQ(candidates.at(1), QStringLiteral("chromium-browser"));
    EXPECT_EQ(candidates.at(2), QStringLiteral("google-chrome"));
    EXPECT_EQ(candidates.at(3), QStringLiteral("google-chrome-stable"));
    for (const QString& entry : candidates) {
        EXPECT_FALSE(entry.contains(QStringLiteral("Program Files")));
        EXPECT_FALSE(entry.contains(QStringLiteral("firefox"), Qt::CaseInsensitive));
    }
}

TEST(test_ui_ManualLocation, WindowsCandidatesAreEdgeThenTheBareName)
{
    const QString files = QStringLiteral("C:/Program Files");
    const QString filesX86 = QStringLiteral("C:/Program Files (x86)");
    const QStringList candidates = VLMS::manualBrowserCandidates(true, files, filesX86);
    ASSERT_EQ(candidates.size(), 3);
    EXPECT_EQ(candidates.at(0),
              QDir(files).filePath(QStringLiteral("Microsoft/Edge/Application/msedge.exe")));
    EXPECT_EQ(candidates.at(1),
              QDir(filesX86).filePath(QStringLiteral("Microsoft/Edge/Application/msedge.exe")));
    EXPECT_EQ(candidates.at(2), QStringLiteral("msedge"));
}

TEST(test_ui_ManualLocation, LaunchUsesTheFirstBrowserAndTheFourArguments)
{
    const QString index = QStringLiteral("/var/manual/index.html");
    const VLMS::ManualBrowserCommand command = VLMS::manualBrowserLaunch(
        {QStringLiteral("/usr/bin/chromium"), QStringLiteral("/usr/bin/google-chrome")},
        index,
        QStringLiteral("/home/lib/manual-browser"));
    EXPECT_EQ(command.program, QStringLiteral("/usr/bin/chromium"));
    const QString url = QUrl::fromLocalFile(index).toString(QUrl::FullyEncoded);
    EXPECT_EQ(command.arguments,
              QStringList({QStringLiteral("--user-data-dir=/home/lib/manual-browser"),
                           QStringLiteral("--app=") + url,
                           QStringLiteral("--no-first-run"),
                           QStringLiteral("--disable-extensions")}));
}

TEST(test_ui_ManualLocation, EmptyBrowserListReturnsAnEmptyProgram)
{
    const VLMS::ManualBrowserCommand command = VLMS::manualBrowserLaunch(
        {}, QStringLiteral("/var/manual/index.html"), QStringLiteral("/tmp/profile"));
    EXPECT_TRUE(command.program.isEmpty());
    EXPECT_TRUE(command.arguments.isEmpty());
}

TEST(test_ui_ManualLocation, FileUrlKeepsASpaceInThePath)
{
    const QString index = QStringLiteral("/tmp/user manual/index.html");
    const VLMS::ManualBrowserCommand command = VLMS::manualBrowserLaunch(
        {QStringLiteral("/usr/bin/chromium")}, index, QStringLiteral("/tmp/profile"));
    ASSERT_EQ(command.arguments.size(), 4);
    const QString appArg = command.arguments.at(1);
    ASSERT_TRUE(appArg.startsWith(QStringLiteral("--app=")));
    const QString url = appArg.mid(QStringLiteral("--app=").size());
    EXPECT_TRUE(url.contains(QStringLiteral("%20")));
    EXPECT_FALSE(url.contains(QLatin1Char(' ')));
    EXPECT_EQ(QUrl(url).toLocalFile(), index);
}
```

- [x] **Step 3: Run the tests to verify they fail**

```bash
cmake --build build-sdd-manual-button --target test_vlms_ui -j"$(nproc)"
```

Expected: FAIL at compile time. `manualBrowserCandidates` and `manualBrowserLaunch` are not declared in `VLMS`. The four existing tests are not the failure.

- [x] **Step 4: Write the minimal implementation**

Replace `applications/vlms/src/ui/ManualLocation.h` with:

```cpp
#pragma once

#include <QString>
#include <QStringList>

namespace VLMS {

/// The manual page to open for `locale`: the first of
/// <projectRoot>/docs/manual/<locale>/index.html, <appDir>/manual/<locale>/index.html,
/// the same two for "ar", then <projectRoot>/docs/manual/index.html and
/// <appDir>/manual/index.html. Empty when none exists.
[[nodiscard]] QString manualIndexPath(const QString& projectRoot,
                                      const QString& appDir,
                                      const QString& locale);

struct ManualBrowserCommand {
    QString program;
    QStringList arguments;
};

/// Ordered browsers for one platform. `windows` false is the Linux list and
/// ignores the two directories. `windows` true is Edge under each Program Files
/// directory, then the bare name `msedge`. Existence is not checked here.
[[nodiscard]] QStringList manualBrowserCandidates(bool windows,
                                                  const QString& programFiles,
                                                  const QString& programFilesX86);

/// The first entry of `browsers`, and the four arguments that open `indexPath`
/// in the private profile. An empty list returns an empty program.
[[nodiscard]] ManualBrowserCommand manualBrowserLaunch(const QStringList& browsers,
                                                       const QString& indexPath,
                                                       const QString& profileDir);

}  // namespace VLMS
```

In `applications/vlms/src/ui/ManualLocation.cpp`, add `#include <QDir>` and `#include <QUrl>` next to the existing includes. Leave the body of `manualIndexPath` unchanged. After that function, still inside `namespace VLMS`, add:

```cpp
QStringList manualBrowserCandidates(bool windows,
                                    const QString& programFiles,
                                    const QString& programFilesX86)
{
    if (!windows) {
        return {QStringLiteral("chromium"),
                QStringLiteral("chromium-browser"),
                QStringLiteral("google-chrome"),
                QStringLiteral("google-chrome-stable")};
    }
    return {QDir(programFiles).filePath(QStringLiteral("Microsoft/Edge/Application/msedge.exe")),
            QDir(programFilesX86).filePath(QStringLiteral("Microsoft/Edge/Application/msedge.exe")),
            QStringLiteral("msedge")};
}

ManualBrowserCommand manualBrowserLaunch(const QStringList& browsers,
                                         const QString& indexPath,
                                         const QString& profileDir)
{
    if (browsers.isEmpty()) {
        return {};
    }
    const QString url = QUrl::fromLocalFile(indexPath).toString(QUrl::FullyEncoded);
    ManualBrowserCommand command;
    command.program = browsers.first();
    command.arguments = {QStringLiteral("--user-data-dir=") + profileDir,
                         QStringLiteral("--app=") + url,
                         QStringLiteral("--no-first-run"),
                         QStringLiteral("--disable-extensions")};
    return command;
}
```

- [x] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build-sdd-manual-button --target test_vlms_ui -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ctest --test-dir build-sdd-manual-button -R 'test_ui_ManualLocation' --output-on-failure
```

Expected: every `test_ui_ManualLocation` test PASS, including the four that were already there (`PrefersTheSourceTreeInTheCurrentLanguage`, `FindsTheInstalledCopyBesideTheExecutable`, `FallsBackToArabicThenTheChooser`, `EmptyWhenThereIsNoManual`).

- [x] **Step 6: Do not commit**

Leave the working tree uncommitted.

---

### Task 2: The missing-browser sentence

**Files:**
- Modify: `libraries/Core/src/Strings.cpp` (three tables, the line after `help.notFound` in each)
- Test: `libraries/Core/test/src/test_strings_parity.cpp` (one new test)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: string key `help.noBrowser` in the Arabic, French, and English tables, with the three sentences in Global Constraints. `Strings::rawValue("ar"|"fr"|"en", "help.noBrowser")` returns them. Parity (`TablesHaveIdenticalKeySets`) stays green because the key is in all three tables.

- [x] **Step 1: Write the failing test**

In `libraries/Core/test/src/test_strings_parity.cpp`, insert this test immediately after `MemberSexLabelCoversAllSexCodes` (that test ends just before `ArchiveKeysHaveSpecifiedWording`). `kArabic`, `kFrench`, and `kEnglish` are already in the file's anonymous namespace.

```cpp
TEST_F(test_core_StringsParity, NoBrowserSentences)
{
    EXPECT_EQ(Strings::rawValue(kArabic, "help.noBrowser"),
              "تعذّر العثور على متصفح لفتح دليل الاستخدام.");
    EXPECT_EQ(Strings::rawValue(kFrench, "help.noBrowser"),
              "Aucun navigateur n'a été trouvé pour ouvrir le manuel d'utilisation.");
    EXPECT_EQ(Strings::rawValue(kEnglish, "help.noBrowser"),
              "No browser was found to open the user manual.");
}
```

Copy the three sentence literals from this plan. Do not retype the Arabic from memory: the `ذّ` in `تعذّر` is the same character sequence already used by `help.notFound` in `Strings.cpp`.

- [x] **Step 2: Run the test to verify it fails**

```bash
cmake --build build-sdd-manual-button --target test_vlms_core -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ctest --test-dir build-sdd-manual-button -R '^test_vlms_core$' --output-on-failure
```

Expected: FAIL in `test_core_StringsParity.NoBrowserSentences`. `rawValue` returns an empty string for a missing key, so each `EXPECT_EQ` shows the sentence against `""`. The ctest entry is the whole Core binary (`TIMEOUT 300`). Other failures are not part of this task; stop if one appears.

- [x] **Step 3: Add the key to all three tables**

In `libraries/Core/src/Strings.cpp`, immediately after each `help.notFound` line, add the matching entry. The three sites today are the Arabic table (after the line `{"help.notFound", "تعذّر العثور على دليل الاستخدام."}`), the French table (after `{"help.notFound", "Le manuel d'utilisation est introuvable."}`), and the English table (after `{"help.notFound", "The user manual could not be found."}`).

Arabic:

```cpp
        {"help.noBrowser", "تعذّر العثور على متصفح لفتح دليل الاستخدام."},
```

French:

```cpp
        {"help.noBrowser", "Aucun navigateur n'a été trouvé pour ouvrir le manuel d'utilisation."},
```

English:

```cpp
        {"help.noBrowser", "No browser was found to open the user manual."},
```

Do not edit `nav.help`, `help.tooltip`, or `help.notFound`.

- [x] **Step 4: Run the test to verify it passes**

```bash
cmake --build build-sdd-manual-button --target test_vlms_core -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ctest --test-dir build-sdd-manual-button -R '^test_vlms_core$' --output-on-failure
```

Expected: PASS. `NoBrowserSentences` is green, and `TablesHaveIdenticalKeySets` is green because the new key is in every table.

- [x] **Step 5: Do not commit**

---

### Task 3: The circle, and opening the manual from it

**Files:**
- Modify: `applications/vlms/src/ui/Theme.h` (declare `manualButtonIcon` immediately after `themeModeIcon`)
- Modify: `applications/vlms/src/ui/Theme.cpp` (stylesheet rules at the `QPushButton#themeToggle` block, about lines 196–211; icon function immediately after `themeModeIcon`, which ends just before `themedArtwork`)
- Modify: `applications/vlms/src/ui/MainWindow.h`
- Modify: `applications/vlms/src/ui/MainWindow.cpp`
- Modify: `cmake/WindowsPackaging.cmake` (the `manual/` line in the installed-layout comment)
- Modify: `applications/vlms/CMakeLists.txt` (the comment above `install(DIRECTORY .../docs/manual/` only; the install rule itself stays)

**Interfaces:**
- Consumes: `ManualBrowserCommand`, `manualBrowserCandidates`, `manualBrowserLaunch` from Task 1. `help.noBrowser` from Task 2. `manualIndexPath`, `showWarning` (`ui/UiHelpers.h`), `T("nav.help")`, `T("help.tooltip")`, `T("help.notFound")`.
- Produces: `QIcon manualButtonIcon(ThemeMode mode, int pixelSize, qreal ratio = 1.0);` in `Theme.h`. `MainWindow` member `m_manualButton`. No `m_helpNav`.

There is no new gtest. `applications/vlms/test/CMakeLists.txt` says the UI tests never construct `MainWindow`, because it reaches for `qobject_cast<Application*>(qApp)`. The spec's automated checks are Tasks 1 and 2. This task is verified by compiling the application and by the existing location tests still passing. The second-click check is Task 6.

- [x] **Step 1: Add the icon declaration**

In `applications/vlms/src/ui/Theme.h`, immediately after the `themeModeIcon` declaration, add:

```cpp
/// The "?" drawn on the manual button, in the same muted ink as themeModeIcon.
/// Painted rather than set as button text, so the circle's stylesheet never
/// has to centre a glyph. \param ratio the target widget's device pixel ratio.
QIcon manualButtonIcon(ThemeMode mode, int pixelSize, qreal ratio = 1.0);
```

- [x] **Step 2: Name the circle in the stylesheet**

In `applications/vlms/src/ui/Theme.cpp`, replace the two `themeToggle` rules (the block whose comment begins "Round, and fixed at the diameter") with the same rules listing both object names. Do not change the property values.

```css
QPushButton#themeToggle,
QPushButton#manualButton {
  background: transparent;
  border: 1px solid @border;
  border-radius: 17px;
  min-width: 34px;
  max-width: 34px;
  min-height: 34px;
  max-height: 34px;
  padding: 0px;
}

QPushButton#themeToggle:hover,
QPushButton#manualButton:hover {
  color: @textPrimary;
  background: @hoverBg;
  border-color: @focusBorder;
}
```

The comment above the block stays. `QPushButton#languageButton` below it stays.

- [x] **Step 3: Paint the question mark**

In `Theme.cpp`, immediately after `themeModeIcon` (the function that ends with `return QIcon(canvas);` just before `themedArtwork`), add:

```cpp
QIcon manualButtonIcon(ThemeMode mode, int pixelSize, qreal ratio)
{
    const Palette& palette = (mode == ThemeMode::Dark) ? kDarkPalette : kLightPalette;
    const QColor ink(QString::fromLatin1(palette.textMuted));

    const int side = qMax(1, qRound(pixelSize * ratio));
    QPixmap canvas(side, side);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font(QString::fromLatin1(kFontFamily));
    font.setPixelSize(side);
    painter.setFont(font);
    painter.setPen(ink);
    painter.drawText(QRect(0, 0, side, side), Qt::AlignCenter, QStringLiteral("?"));
    painter.end();

    canvas.setDevicePixelRatio(ratio);
    return QIcon(canvas);
}
```

`kFontFamily` is already `"Cairo"` at the top of `Theme.cpp`. `QPainter`, `QFont`, and `QPixmap` are already included in this file (the theme icon uses them). Light ink is `#64748b`, dark ink is `#94a3b8`, the same `textMuted` values `themeModeIcon` reads.

- [x] **Step 4: Replace the Help word with the circle**

In `applications/vlms/src/ui/MainWindow.h`:

- Declare `void updateManualButton();` next to `void updateThemeToggle();`.
- Next to `QPushButton* m_themeToggle`, add `QPushButton* m_manualButton = nullptr;`.
- Delete `QPushButton* m_helpNav = nullptr;`.

In `applications/vlms/src/ui/MainWindow.cpp`:

- Remove `#include <QDesktopServices>`.
- Add `#include <QFileInfo>`, `#include <QProcess>`, and `#include <QStandardPaths>`.

In the anonymous namespace at the top of the cpp (next to `kHeaderPadding`), add the filter. An absolute path is kept when the file exists. A bare name is kept as the path `findExecutable` returns.

```cpp
QStringList existingBrowsers(const QStringList& candidates)
{
    QStringList kept;
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isAbsolute()) {
            if (info.exists()) {
                kept << candidate;
            }
        } else {
            const QString found = QStandardPaths::findExecutable(candidate);
            if (!found.isEmpty()) {
                kept << found;
            }
        }
    }
    return kept;
}
```

In `buildUi`, delete `m_helpNav = VLMS::makeNavButton({});` and take `m_helpNav` out of the loop, so the loop is only the five page buttons:

```cpp
    for (QPushButton* button :
         {m_catalogNav, m_membersNav, m_circulationNav, m_archiveNav, m_metricsNav}) {
        headerLayout->addWidget(button);
    }
```

Immediately after `headerLayout->addWidget(m_themeToggle);` and before the theme toggle's `connect`, create the circle:

```cpp
    m_manualButton = new QPushButton(header);
    m_manualButton->setObjectName(QStringLiteral("manualButton"));
    m_manualButton->setCursor(Qt::PointingHandCursor);
    m_manualButton->setFocusPolicy(Qt::StrongFocus);
    m_manualButton->setText({});
    headerLayout->addWidget(m_manualButton);
```

Delete the `connect(m_helpNav, ...)` lambda (the one that calls `QDesktopServices::openUrl`). Replace it with:

```cpp
    connect(m_manualButton, &QPushButton::clicked, this, [this]() {
        const QString path = VLMS::manualIndexPath(
            VLMS::qs(VLMS::Paths::projectRoot()),
            QCoreApplication::applicationDirPath(),
            VLMS::qs(VLMS::Locale::code()));
        if (path.isEmpty()) {
            VLMS::showWarning(this, T("nav.help"), T("help.notFound"));
            return;
        }
#if defined(Q_OS_WIN)
        constexpr bool kWindows = true;
#else
        constexpr bool kWindows = false;
#endif
        const QStringList browsers = existingBrowsers(VLMS::manualBrowserCandidates(
            kWindows,
            qEnvironmentVariable("ProgramFiles"),
            qEnvironmentVariable("ProgramFiles(x86)")));
        const QString profile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/manual-browser");
        const VLMS::ManualBrowserCommand command =
            VLMS::manualBrowserLaunch(browsers, path, profile);
        if (command.program.isEmpty()
            || !QProcess::startDetached(command.program, command.arguments)) {
            VLMS::showWarning(this, T("nav.help"), T("help.noBrowser"));
        }
    });
```

The empty-path `return` is what keeps a missing manual from calling `findExecutable`. Do not create the profile directory; the browser creates `--user-data-dir`. Do not pass the command through a shell: `startDetached(program, arguments)` keeps a space in `Program Files` inside the executable path.

Add `updateManualButton` immediately after `updateThemeToggle`:

```cpp
void MainWindow::updateManualButton()
{
    if (m_manualButton == nullptr) {
        return;
    }

    static constexpr int kIconSize = 18;
    m_manualButton->setText({});
    m_manualButton->setIcon(VLMS::manualButtonIcon(
        VLMS::Theme::mode(), kIconSize, m_manualButton->devicePixelRatioF()));
    m_manualButton->setIconSize(QSize(kIconSize, kIconSize));
    m_manualButton->setToolTip(T("help.tooltip"));
    m_manualButton->setAccessibleName(T("nav.help"));
}
```

In `retranslateUi`, delete the two `m_helpNav` lines (`setText(T("nav.help"))` and `setToolTip(T("help.tooltip"))`). Immediately after `updateThemeToggle();`, call `updateManualButton();`. `onThemeChanged` already calls `retranslateUi`, so a theme change redraws the "?" in the other ink. Do not add a second connection.

`updateNavigation` does not mention Help and must not gain a case for this button. The circle never becomes the active page.

- [x] **Step 5: Name the button in the install comments**

In `cmake/WindowsPackaging.cmake`, replace the layout line:

```cmake
#   manual/                 (user manual, opened by Help)
```

with:

```cmake
#   manual/                 (user manual, opened by the ? beside the theme toggle)
```

In `applications/vlms/CMakeLists.txt`, replace the comment above `install(DIRECTORY ${CMAKE_SOURCE_DIR}/docs/manual/`:

```cmake
    # The user manual, opened by the header's Help button. Only the generated pages and
    # their assets: the Markdown sources and the template stay in the repository.
```

with:

```cmake
    # The user manual, opened by the ? beside the theme toggle. Only the generated pages and
    # their assets: the Markdown sources and the template stay in the repository.
```

Leave the `install(DIRECTORY ...)` rule, the `PATTERN` exclusions, and the `DESTINATION` as they are.

- [x] **Step 6: Build and re-run the location tests**

```bash
cmake --build build-sdd-manual-button --target vlms test_vlms_ui -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ctest --test-dir build-sdd-manual-button -R 'test_ui_ManualLocation' --output-on-failure
```

Expected: both targets link, and every `test_ui_ManualLocation` test PASS. A remaining `m_helpNav` or `QDesktopServices` reference fails the build; that is the check that the Help word is gone.

- [x] **Step 7: Do not commit**

---

### Task 4: The manual's own words

**Files:**
- Modify: `docs/manual/content/ar/index.md` (the sentence under `## الشاشات`)
- Modify: `docs/manual/content/en/index.md` (the sentence under `## The screens`)
- Modify: `docs/manual/content/fr/index.md` (the sentence under `## Les écrans`)
- Modify: `docs/manual/content/ar/getting-started.md` (items 1 and 3 of the numbered list)
- Modify: `docs/manual/content/en/getting-started.md` (items 1 and 3)
- Modify: `docs/manual/content/fr/getting-started.md` (items 1 and 3)
- Generated, not hand-edited: `docs/manual/{ar,en,fr}/index.html` and `getting-started.html`, rewritten by `scripts/build_manual.py`

**Interfaces:**
- Consumes: nothing from the C++ tasks. The sentences describe the button Task 3 builds.
- Produces: the six Help sentences no longer say that a word in the page row opens the manual. Generated HTML matches the Markdown.

The backticks stay. The generator turns them into the control style (`span.ui`). The mark on the button is the Latin `?` in every language, because that is what `manualButtonIcon` draws.

- [x] **Step 1: Replace the six sentences and the three theme-button lines**

`docs/manual/content/en/index.md`, the paragraph under `## The screens`. Replace:

```markdown
`Help`, beside those screens, opens this manual.
```

with:

```markdown
The `?` beside the theme button opens this manual.
```

`docs/manual/content/fr/index.md`. Replace:

```markdown
`Aide`, à côté de ces écrans, ouvre ce manuel.
```

with:

```markdown
Le `?` à côté du bouton de thème ouvre ce manuel.
```

`docs/manual/content/ar/index.md`. Replace:

```markdown
`الدليل`، إلى جانب هذه الشاشات، يفتح هذا الدليل.
```

with:

```markdown
`?`، بجانب زر السمة، يفتح هذا الدليل.
```

`docs/manual/content/en/getting-started.md`, items 1 and 3. Replace item 1:

```markdown
1. `Catalogue` — the screen you are on. `Members`, `Circulation`, `Archive`, and `Metrics` sit beside it. `Help` opens this manual.
```

with:

```markdown
1. `Catalogue` — the screen you are on. `Members`, `Circulation`, `Archive`, and `Metrics` sit beside it.
```

Replace item 3:

```markdown
3. The theme button. It turns the dark theme on and off.
```

with:

```markdown
3. The theme button, and the `?` beside it. The theme button turns the dark theme on and off. The `?` opens this manual.
```

`docs/manual/content/fr/getting-started.md`. Replace item 1:

```markdown
1. `Catalogue` — l'écran affiché. `Adhérents`, `Circulation`, `Archives` et `Indicateurs` sont à côté. `Aide` ouvre ce manuel.
```

with:

```markdown
1. `Catalogue` — l'écran affiché. `Adhérents`, `Circulation`, `Archives` et `Indicateurs` sont à côté.
```

Replace item 3:

```markdown
3. Le bouton de thème. Il allume ou éteint le thème sombre.
```

with:

```markdown
3. Le bouton de thème, et le `?` à côté. Le bouton de thème allume ou éteint le thème sombre. Le `?` ouvre ce manuel.
```

`docs/manual/content/ar/getting-started.md`. Replace item 1:

```markdown
1. `الفهرس` — الشاشة التي أنت فيها. بجانبها `الأعضاء` و`الإعارة` و`الأرشيف` و`المؤشرات`. `الدليل` يفتح هذا الدليل.
```

with:

```markdown
1. `الفهرس` — الشاشة التي أنت فيها. بجانبها `الأعضاء` و`الإعارة` و`الأرشيف` و`المؤشرات`.
```

Replace item 3:

```markdown
3. زر السمة. يشغّل السمة الداكنة أو يطفئها.
```

with:

```markdown
3. زر السمة، وبجانبه `?`. زر السمة يشغّل السمة الداكنة أو يطفئها. `?` يفتح هذا الدليل.
```

Do not edit any other manual page. Items 2 and 4–10 stay, so the ten callout numbers still match.

- [x] **Step 2: Rebuild the HTML and check it**

```bash
python3 scripts/build_manual.py
python3 scripts/build_manual.py --check
```

Expected: the generator rewrites the pages and `--check` exits 0.

Then confirm the old wording is gone and the new wording is in both the sources and the generated pages:

```bash
rg -n 'beside those screens|à côté de ces écrans|إلى جانب هذه الشاشات|`Help` opens|`Aide` ouvre|`الدليل` يفتح' docs/manual/content docs/manual/en docs/manual/fr docs/manual/ar
rg -n 'beside the theme button|à côté du bouton de thème|بجانب زر السمة' docs/manual/content docs/manual/en docs/manual/fr docs/manual/ar
```

Expected: the first search prints nothing. The second search prints the new index sentence in each language's `content/<lang>/index.md` and in `docs/manual/<lang>/index.html`.

- [x] **Step 3: Do not commit**

---

### Task 5: Recapture the shots that show the header

**Files:**
- Regenerated: the twelve full-window shots below, in `docs/manual/assets/shots/ar/`, `en/`, and `fr/`
- Do not modify: `applications/vlms/manual_capture/**`. `gs-window` still callouts the Catalogue nav button, the flag selector, and `themeToggle` by object name. The Help word was a `navLink`; removing it does not remove those targets.

**Interfaces:**
- Consumes: the circle from Task 3. `capture.sh` rebuilds `manual_capture` against the current tree, so the grab includes `manualButton`.
- Produces: updated PNGs. The HTML from Task 4 already points at these filenames; do not run `build_manual.py` again.

These twelve ids are the shots whose save path is `capture.grab()` of the main window (sometimes with callouts). Table crops, dialog grabs, and `met-full` (the dashboard widget only) do not show the page row or the flag cluster, so they are not retaken.

`gs-window`, `gs-dark`, `cat-overview`, `mem-overview`, `circ-overview`, `circ-overdue`, `circ-search-name`, `arc-books`, `arc-copies`, `arc-loans`, `arc-members`, `met-overview`.

- [x] **Step 1: Recapture those ids in all three languages**

From the repository root. `capture.sh` prepares a scrubbed sandbox from `database/vlms.db` (read-only) and runs `manual_capture` offscreen. The extra arguments are forwarded to the tool as `--only`.

```bash
bash scripts/manual/capture.sh --only gs-window,gs-dark,cat-overview,mem-overview,circ-overview,circ-overdue,circ-search-name,arc-books,arc-copies,arc-loans,arc-members,met-overview
```

Expected: exit 0. Each language prints no `unknown shot` and no `refusing` line. The twelve PNGs under each of `docs/manual/assets/shots/ar`, `en`, and `fr` have a newer mtime than before the command.

- [x] **Step 2: Look at the header in one light shot and one dark shot**

Open these four images (the Read tool reads PNGs):

- `docs/manual/assets/shots/en/gs-window.png`
- `docs/manual/assets/shots/en/gs-dark.png`
- `docs/manual/assets/shots/ar/gs-window.png`
- `docs/manual/assets/shots/fr/cat-overview.png`

Expected in each: the page row is Catalogue, Members, Circulation, Archive, Metrics, with no Help / Aide / الدليل word after Metrics. At the other end of the header the cluster is the three flags, the theme circle, and a "?" circle. The dark shot's "?" is the lighter muted ink. If the Help word is still there, `manual_capture` was built from a tree that still has `m_helpNav`; rebuild is `capture.sh`'s job, so fix Task 3 and run Step 1 again.

- [x] **Step 3: Do not commit**

---

### Task 6: Second click, by eye

waived 2026-09-23 — the user kept the button as built; a second click need not focus the open page.

**Files:** none.

**Interfaces:**
- Consumes: the button and the launch command from Task 3, and a manual index that `manualIndexPath` can find (the pages Task 4 wrote under `docs/manual/<lang>/index.html`).
- Produces: a note in the session, not a test. The unit tests cannot see the browser window. Do not add a test that starts a browser.

- [x] **Step 1: Open the application on the desktop**

Use the binary from Task 3. Do not set `QT_QPA_PLATFORM=offscreen`.

```bash
cmake --build build-sdd-manual-button --target vlms -j"$(nproc)"
./build-sdd-manual-button/bin/vlms
```

If that path is not where the binary landed, take the `vlms` executable the build just wrote. A machine with no display cannot do this step: stop and say the eye check was not done. Do not claim the window came forward.

- [x] **Step 2: Click twice**

Click the "?" circle. The manual's index opens in its own window (Chromium or Chrome on this machine; Edge on Windows). Open a page past the index, such as Getting started, and leave it there.

Click the "?" circle again.

Expected: the window already open comes forward. The page stays on Getting started. It does not load the index again, and a second window does not appear. The browser log line from the spec's trial, when it is visible, is "Opening in existing browser session".

If the first click shows `help.noBrowser`, none of `chromium`, `chromium-browser`, `google-chrome`, or `google-chrome-stable` is on `PATH`. That warning is the specified result. Install nothing as part of this task, and say the second-click check could not be run.

Switching the application to French or Arabic and clicking "?" opens a second window for that language's index. A further click in that language brings that window forward. This is the same command with a different index path; do not write code for it.

- [x] **Step 3: Do not commit**
