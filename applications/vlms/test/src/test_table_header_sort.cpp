#include "ui/TableHeaderSort.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <gtest/gtest.h>

using namespace VLMS;

TEST(test_ui_TableHeaderSort, FirstClickAscendingThenToggles)
{
    QTableWidget table(0, 2);
    table.setHorizontalHeaderLabels({QStringLiteral("A"), QStringLiteral("B")});
    VLMS::TableHeaderSort sort(&table);
    sort.setColumnKeys({QStringLiteral("a"), QStringLiteral("b")});

    int lastColumn = -1;
    bool lastAsc = false;
    int fires = 0;
    QObject::connect(&sort, &VLMS::TableHeaderSort::sortChanged,
                     [&](int column, bool ascending) {
                         lastColumn = column;
                         lastAsc = ascending;
                         ++fires;
                     });

    emit table.horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(fires, 1);
    EXPECT_EQ(sort.column(), 0);
    EXPECT_TRUE(sort.ascending());
    EXPECT_EQ(sort.columnKey(), QStringLiteral("a"));
    EXPECT_EQ(table.horizontalHeader()->sortIndicatorSection(), 0);
    EXPECT_EQ(table.horizontalHeader()->sortIndicatorOrder(), Qt::AscendingOrder);

    emit table.horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(fires, 2);
    EXPECT_FALSE(sort.ascending());
    EXPECT_EQ(table.horizontalHeader()->sortIndicatorOrder(), Qt::DescendingOrder);

    emit table.horizontalHeader()->sectionClicked(1);
    EXPECT_EQ(fires, 3);
    EXPECT_EQ(sort.column(), 1);
    EXPECT_TRUE(sort.ascending());
    EXPECT_EQ(sort.columnKey(), QStringLiteral("b"));
}

TEST(test_ui_TableHeaderSort, WidgetSortRestoresSelection)
{
    QTableWidget table(2, 1);
    auto* keep = new QTableWidgetItem(QStringLiteral("b"));
    keep->setData(Qt::UserRole, 20);
    auto* other = new QTableWidgetItem(QStringLiteral("a"));
    other->setData(Qt::UserRole, 10);
    table.setItem(0, 0, keep);
    table.setItem(1, 0, other);
    table.selectRow(0);
    VLMS::enableWidgetTableSort(&table);

    emit table.horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table.item(0, 0)->data(Qt::UserRole).toInt(), 10);
    EXPECT_EQ(table.item(table.currentRow(), 0)->data(Qt::UserRole).toInt(), 20);
}
