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
    label.setText(QStringLiteral("© 2026 VLMS"));
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
