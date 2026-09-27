#include "ui/ManualLocation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRect>
#include <QTemporaryDir>
#include <QUrl>

#include <gtest/gtest.h>

namespace {
void touch(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
}
}  // namespace

TEST(test_ui_ManualLocation, PrefersTheSourceTreeInTheCurrentLanguage)
{
    QTemporaryDir root, app;
    touch(root.path() + "/docs/manual/fr/index.html");
    touch(app.path() + "/manual/fr/index.html");
    EXPECT_EQ(VLMS::manualIndexPath(root.path(), app.path(), "fr"),
              root.path() + "/docs/manual/fr/index.html");
}

TEST(test_ui_ManualLocation, FindsTheInstalledCopyBesideTheExecutable)
{
    QTemporaryDir root, app;
    touch(app.path() + "/manual/en/index.html");
    EXPECT_EQ(VLMS::manualIndexPath(root.path(), app.path(), "en"),
              app.path() + "/manual/en/index.html");
}

TEST(test_ui_ManualLocation, FallsBackToArabicThenTheChooser)
{
    QTemporaryDir root, app;
    touch(app.path() + "/manual/ar/index.html");
    EXPECT_EQ(VLMS::manualIndexPath(root.path(), app.path(), "fr"),
              app.path() + "/manual/ar/index.html");
    QTemporaryDir root2, app2;
    touch(app2.path() + "/manual/index.html");
    EXPECT_EQ(VLMS::manualIndexPath(root2.path(), app2.path(), "fr"),
              app2.path() + "/manual/index.html");
}

TEST(test_ui_ManualLocation, EmptyWhenThereIsNoManual)
{
    QTemporaryDir root, app;
    EXPECT_TRUE(VLMS::manualIndexPath(root.path(), app.path(), "ar").isEmpty());
}

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

TEST(test_ui_ManualLocation, LargeScreenKeepsTheReadingSize)
{
    const QRect available(1920, 0, 1920, 1080);
    const QRect placed = VLMS::manualWindowRect(available);
    EXPECT_EQ(placed.size(), QSize(1200, 800));
    EXPECT_TRUE(available.contains(placed));
    EXPECT_EQ(placed.x(), 1920 + (1920 - 1200) / 2);
    EXPECT_EQ(placed.y(), (1080 - 800) / 2);
}

TEST(test_ui_ManualLocation, ShortScreenKeepsTheWindowInside)
{
    const QRect available(0, 0, 1280, 720);
    const QRect placed = VLMS::manualWindowRect(available);
    EXPECT_TRUE(available.contains(placed));
    EXPECT_EQ(placed.size(), QSize(1200, 720 - 64));
    EXPECT_EQ(placed.topLeft(), QPoint((1280 - 1200) / 2, 32));
    EXPECT_LE(placed.y() + placed.height(), available.height());
}

TEST(test_ui_ManualLocation, NarrowScreenShrinksBothSides)
{
    const QRect available(100, 40, 900, 640);
    const QRect placed = VLMS::manualWindowRect(available);
    EXPECT_TRUE(available.contains(placed));
    EXPECT_EQ(placed.size(), QSize(900 - 64, 640 - 64));
    EXPECT_EQ(placed.topLeft(), QPoint(100 + 32, 40 + 32));
}

TEST(test_ui_ManualLocation, TinyScreenUsesTheUsableRectangle)
{
    const QRect available(0, 0, 1280, 40);
    const QRect placed = VLMS::manualWindowRect(available);
    EXPECT_EQ(placed, available);
    EXPECT_TRUE(VLMS::manualWindowRect(QRect()).isNull());
}

TEST(test_ui_ManualLocation, LaunchAddsTheWindowRectangle)
{
    const QRect placed(40, 32, 1200, 656);
    const VLMS::ManualBrowserCommand command = VLMS::manualBrowserLaunch(
        {QStringLiteral("/usr/bin/chromium")},
        QStringLiteral("/var/manual/index.html"),
        QStringLiteral("/tmp/profile"),
        placed);
    ASSERT_EQ(command.arguments.size(), 6);
    EXPECT_EQ(command.arguments.at(4), QStringLiteral("--window-position=40,32"));
    EXPECT_EQ(command.arguments.at(5), QStringLiteral("--window-size=1200,656"));
}

TEST(test_ui_ManualLocation, InvalidWindowOmitsTheSizeFlags)
{
    const VLMS::ManualBrowserCommand command = VLMS::manualBrowserLaunch(
        {QStringLiteral("/usr/bin/chromium")},
        QStringLiteral("/var/manual/index.html"),
        QStringLiteral("/tmp/profile"),
        QRect());
    EXPECT_EQ(command.arguments.size(), 4);
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
