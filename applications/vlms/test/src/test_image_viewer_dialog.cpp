#include "ModalTest.h"

#include "ui/ImageViewerDialog.h"

#include <VLMS/Core/Locale.h>

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

using VLMS::Locale;

class test_ui_ImageViewerDialog : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }

    void SetUp() override { ASSERT_TRUE(m_dir.isValid()); }

    QString writeImage(const QString& name, const QSize& size, const QColor& colour = Qt::darkCyan)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(colour);
        const QString path = m_dir.filePath(name);
        EXPECT_TRUE(image.save(path));
        return path;
    }

    static QSize available()
    {
        return QGuiApplication::primaryScreen()->availableGeometry().size();
    }

    QTemporaryDir m_dir;
};

TEST_F(test_ui_ImageViewerDialog, FittedSizeScalesUpAndDown)
{
    EXPECT_EQ(ImageViewerDialog::fittedSize({200, 260}, {620, 420}), QSize(323, 420));
    EXPECT_EQ(ImageViewerDialog::fittedSize({4000, 3000}, {620, 420}), QSize(560, 420));
    EXPECT_EQ(ImageViewerDialog::fittedSize({100, 10}, {620, 420}), QSize(620, 62));
    EXPECT_TRUE(ImageViewerDialog::fittedSize({}, {620, 420}).isEmpty());
}

TEST_F(test_ui_ImageViewerDialog, SmallImageOpensLargerThanItsFile)
{
    ImageViewerDialog viewer(QStringLiteral("Photo"), writeImage("small.png", {200, 260}), {});
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    const QSize shown = viewer.shownImageSize();
    EXPECT_GT(shown.width(), 200);
    EXPECT_GT(shown.height(), 260);
    // It fills the 80 % box on its limiting side, give or take the buttons and margins.
    EXPECT_GE(shown.height(), available().height() * 0.8 - 120);
}

TEST_F(test_ui_ImageViewerDialog, LargeImageShrinksToFitTheScreen)
{
    ImageViewerDialog viewer(QStringLiteral("Photo"), writeImage("large.png", {4000, 3000}), {});
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    const QSize shown = viewer.shownImageSize();
    EXPECT_LE(shown.width(), available().width());
    EXPECT_LE(shown.height(), available().height());
    EXPECT_NEAR(double(shown.width()) / shown.height(), 4.0 / 3.0, 0.02);
}

TEST_F(test_ui_ImageViewerDialog, ResizingTheWindowRescalesTheImage)
{
    ImageViewerDialog viewer(QStringLiteral("Photo"), writeImage("photo.png", {300, 400}), {});
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));
    const QSize before = viewer.shownImageSize();

    viewer.resize(viewer.width() / 2, viewer.height() / 2);

    QTRY_VERIFY(viewer.shownImageSize().height() < before.height());
}

TEST_F(test_ui_ImageViewerDialog, ChangeShowsThePickedImageAndStaysOpen)
{
    const QString first = writeImage("first.png", {300, 400});
    const QString second = writeImage("second.png", {400, 300}, Qt::darkRed);
    ImageViewerDialog viewer(QStringLiteral("Photo"), first, [second] { return second; });
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Change…"));

    EXPECT_TRUE(viewer.isVisible());
    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Changed);
    EXPECT_EQ(viewer.imagePath(), second);
    const QSize shown = viewer.shownImageSize();
    EXPECT_GT(shown.width(), shown.height()) << "the landscape image is the one on screen";
}

TEST_F(test_ui_ImageViewerDialog, CancelledChangeLeavesItUnchanged)
{
    const QString first = writeImage("first.png", {300, 400});
    ImageViewerDialog viewer(QStringLiteral("Photo"), first, [] { return QString(); });
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Change…"));

    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Unchanged);
    EXPECT_EQ(viewer.imagePath(), first);
}

TEST_F(test_ui_ImageViewerDialog, ChangeToAFileThatIsNoImageIsIgnored)
{
    const QString first = writeImage("first.png", {300, 400});
    const QString notAnImage = m_dir.filePath("notes.png");
    {
        QFile file(notAnImage);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("not a picture");
    }
    ImageViewerDialog viewer(QStringLiteral("Photo"), first, [notAnImage] { return notAnImage; });
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Change…"));

    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Unchanged);
    EXPECT_EQ(viewer.imagePath(), first);
}

TEST_F(test_ui_ImageViewerDialog, RemoveClosesTheViewer)
{
    ImageViewerDialog viewer(QStringLiteral("Photo"), writeImage("photo.png", {300, 400}), {});
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Remove"));

    EXPECT_FALSE(viewer.isVisible());
    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Removed);
}

TEST_F(test_ui_ImageViewerDialog, CloseClosesWithoutChange)
{
    ImageViewerDialog viewer(QStringLiteral("Photo"), writeImage("photo.png", {300, 400}), {});
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Close"));

    EXPECT_FALSE(viewer.isVisible());
    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Unchanged);
}

TEST_F(test_ui_ImageViewerDialog, TheWindowCanBeMaximised)
{
    ImageViewerDialog viewer(QStringLiteral("Photo"), writeImage("photo.png", {300, 400}), {});
    EXPECT_TRUE(viewer.windowFlags().testFlag(Qt::WindowMaximizeButtonHint));
}

TEST_F(test_ui_ImageViewerDialog, CloseAfterChangeKeepsThePickedImage)
{
    const QString first = writeImage("first.png", {300, 400});
    const QString second = writeImage("second.png", {400, 300}, Qt::darkRed);
    ImageViewerDialog viewer(QStringLiteral("Photo"), first, [second] { return second; });
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Change…"));
    clickButtonWithText(&viewer, QStringLiteral("Close"));

    EXPECT_FALSE(viewer.isVisible());
    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Changed);
    EXPECT_EQ(viewer.imagePath(), second);
}

TEST_F(test_ui_ImageViewerDialog, EscapeClosesAndKeepsAPickedImage)
{
    const QString first = writeImage("first.png", {300, 400});
    const QString second = writeImage("second.png", {400, 300}, Qt::darkRed);
    ImageViewerDialog viewer(QStringLiteral("Photo"), first, [second] { return second; });
    viewer.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&viewer));

    clickButtonWithText(&viewer, QStringLiteral("Change…"));
    QTest::keyClick(&viewer, Qt::Key_Escape);

    EXPECT_FALSE(viewer.isVisible());
    EXPECT_EQ(viewer.outcome(), ImageViewerDialog::Outcome::Changed);
    EXPECT_EQ(viewer.imagePath(), second);
}
