#include "ui/ListPageFrame.h"
#include "ui/UiHelpers.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <gtest/gtest.h>

TEST(test_ui_ListPageFrame, ListModeExposesControllerModelAndViewer)
{
    VLMS::ListPageFrame frame;
    VLMS::ListPageFrame::ListConfig config;
    config.subtitle = QStringLiteral("catalog body");
    config.tableColumnCount = 3;
    config.tableColumnWidths = {80, 80, 80};
    config.imageObjectName = QStringLiteral("bookCover");
    config.imageBounds = VLMS::bookCoverPreviewBounds();
    config.detailsScrollObjectName = QStringLiteral("bookDetailsScroll");
    frame.buildList(config);

    EXPECT_NE(frame.filterColumn(), nullptr);
    EXPECT_TRUE(frame.filterColumn()->isVisibleTo(&frame));
    EXPECT_NE(frame.filterLayout(), nullptr);

    EXPECT_NE(frame.searchEdit(), nullptr);
    EXPECT_TRUE(frame.searchEdit()->isVisibleTo(&frame));

    EXPECT_NE(frame.table(), nullptr);
    EXPECT_TRUE(frame.table()->isVisibleTo(&frame));
    EXPECT_EQ(frame.table()->columnCount(), 3);

    EXPECT_NE(frame.pager(), nullptr);
    EXPECT_NE(frame.imageLabel(), nullptr);
    EXPECT_EQ(frame.imageLabel()->objectName(), QStringLiteral("bookCover"));
    EXPECT_NE(frame.previewPanel(), nullptr);
    EXPECT_NE(frame.detailsPanel(), nullptr);
    EXPECT_NE(frame.findChild<QWidget*>(QStringLiteral("bookDetailsScroll")), nullptr);
}

TEST(test_ui_ListPageFrame, DashboardModeKeepsButtonPadAndOverloadsViewer)
{
    VLMS::ListPageFrame frame;
    frame.buildDashboard(QStringLiteral("metrics body"));

    EXPECT_NE(frame.filterColumn(), nullptr);
    EXPECT_FALSE(frame.filterColumn()->isVisibleTo(&frame));

    EXPECT_NE(frame.searchEdit(), nullptr);
    EXPECT_FALSE(frame.searchEdit()->isVisibleTo(&frame));

    EXPECT_TRUE(frame.table() == nullptr || !frame.table()->isVisibleTo(&frame));
    EXPECT_TRUE(frame.previewPanel() == nullptr || !frame.previewPanel()->isVisibleTo(&frame));

    EXPECT_NE(frame.viewerHost(), nullptr);
    EXPECT_TRUE(frame.viewerHost()->isVisibleTo(&frame));
    EXPECT_NE(frame.buttonPad(), nullptr);
}

TEST(test_ui_ListPageFrame, ButtonPadPreservesInsertionOrder)
{
    VLMS::ListPageFrame frame;
    VLMS::ListPageFrame::ListConfig config;
    config.tableColumnCount = 1;
    config.tableColumnWidths = {80};
    config.imageObjectName = QStringLiteral("bookCover");
    config.imageBounds = VLMS::bookCoverPreviewBounds();
    frame.buildList(config);

    auto* first = VLMS::makePrimaryButton(QStringLiteral("Add"));
    auto* second = VLMS::makeSecondaryButton(QStringLiteral("Edit"));
    frame.addButton(first);
    frame.addButton(second);

    const auto buttons =
        frame.buttonPad()->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
    EXPECT_EQ(buttons.size(), 2);
    EXPECT_EQ(buttons.at(0), first);
    EXPECT_EQ(buttons.at(1), second);
}

TEST(test_ui_ListPageFrame, DashboardButtonKeepsItsOwnWidth)
{
    // Metrics has no search field to take the row's spare width, and its lone
    // Refresh button used to stretch across the whole window instead.
    VLMS::ListPageFrame frame;
    frame.buildDashboard(QStringLiteral("metrics body"));
    auto* refresh = VLMS::makeSecondaryButton(QStringLiteral("Refresh"));
    frame.addButton(refresh);
    frame.resize(1200, 700);
    frame.show();
    QApplication::processEvents();

    EXPECT_LT(refresh->width(), 300);
    EXPECT_GE(refresh->width(), refresh->sizeHint().width());
}
