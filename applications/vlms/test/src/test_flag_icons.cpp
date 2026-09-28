#include "ui/FlagIcons.h"

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QPoint>
#include <QSize>

#include <gtest/gtest.h>

#include <string>

using namespace VLMS;

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
    // Mirrors FlagIcons.cpp's kDimmedOpacity: the mask carries the dimming,
    // so every flag should fade to the same fraction with no colour shift.
    constexpr qreal kDimmedOpacity = 0.6;

    struct Sample {
        QString code;
        QPoint point;
    };
    // One opaque sample per flag, plus a second for Tunisia: its true centre
    // is red because the star's left arm reaches it, so (32, 20) checks the
    // white disc instead, where three stacked discs used to accumulate the
    // old per-draw opacity worst.
    const Sample samples[] = {
        {QStringLiteral("ar"), QPoint(32, 32)},
        {QStringLiteral("ar"), QPoint(32, 20)},
        {QStringLiteral("fr"), QPoint(32, 32)},
        {QStringLiteral("en"), QPoint(32, 32)},
    };

    for (const Sample& sample : samples) {
        SCOPED_TRACE(sample.code.toStdString() + " @ (" +
                     std::to_string(sample.point.x()) + "," +
                     std::to_string(sample.point.y()) + ")");
        const QImage full = rendered(sample.code);
        const QImage dim = rendered(sample.code, 64, true);

        EXPECT_EQ(full.size(), dim.size());

        const QColor fullColor = full.pixelColor(sample.point);
        const QColor dimColor = dim.pixelColor(sample.point);

        ASSERT_EQ(fullColor.alpha(), 255);
        EXPECT_NEAR(dimColor.alpha(), qRound(255 * kDimmedOpacity), 2);

        // Un-premultiplied RGB must survive dimming unchanged -- a shift here
        // is the pink/purple discolouration a per-draw opacity produced by
        // compositing Tunisia's and the UK's overlapping layers repeatedly.
        EXPECT_NEAR(dimColor.red(), fullColor.red(), 2);
        EXPECT_NEAR(dimColor.green(), fullColor.green(), 2);
        EXPECT_NEAR(dimColor.blue(), fullColor.blue(), 2);
    }
}

TEST(test_ui_FlagIcons, TheIconIsPaintedAtTheSizeAsked)
{
    EXPECT_EQ(languageFlagIcon(QStringLiteral("en"), 16).pixmap(16, 16).size(), QSize(16, 16));
    EXPECT_EQ(languageFlagIcon(QStringLiteral("en"), 48).pixmap(48, 48).size(), QSize(48, 48));
    // A high-DPI ratio paints more pixels into the same logical square. How Qt
    // then hands that pixmap back is Qt's business, not this test's.
    EXPECT_FALSE(languageFlagIcon(QStringLiteral("en"), 34, 2.0).isNull());
}
