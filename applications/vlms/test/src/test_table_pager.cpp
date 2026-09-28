#include "ui/TablePager.h"

#include <QComboBox>
#include <QSignalSpy>

#include <gtest/gtest.h>

using namespace VLMS;

using VLMS::TablePager;

TEST(test_ui_TablePager, SetCurrentPageDoesNotEmit)
{
    TablePager pager;
    pager.setPageSize(2);
    pager.setTotalCount(10);
    QSignalSpy spy(&pager, &TablePager::pageChanged);
    pager.setCurrentPage(3);
    EXPECT_EQ(pager.currentPage(), 3);
    EXPECT_EQ(pager.offset(), 4);
    EXPECT_EQ(spy.count(), 0);
}

TEST(test_ui_TablePager, AFreshPagerShowsEveryRow)
{
    // ALL is the page size a librarian gets without asking: the lists are read
    // by scrolling and searching, not by walking pages.
    TablePager pager;
    pager.setTotalCount(137);

    EXPECT_EQ(pager.pageSize(), 137);
    EXPECT_EQ(pager.pageCount(), 1);
    EXPECT_EQ(pager.offset(), 0);
}

TEST(test_ui_TablePager, AFreshPagerOffersEveryRowInItsCombo)
{
    TablePager pager;
    auto* combo = pager.findChild<QComboBox*>();

    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->currentData().toInt(), TablePager::kShowAllSize);
}

TEST(test_ui_TablePager, PickingASizeLeavesShowAllBehind)
{
    TablePager pager;
    pager.setPageSize(20);
    pager.setTotalCount(137);

    EXPECT_EQ(pager.pageSize(), 20);
    EXPECT_EQ(pager.pageCount(), 7);
}
