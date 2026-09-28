#include "ui/catalog/CategoryManagerDialog.h"
#include <VLMS/Repositories/CatalogRepository.h>
#include "TestDatabase.h"
#include "TestSeed.h"
#include <QHeaderView>
#include <QTableWidget>
#include <gtest/gtest.h>

using namespace VLMS;

TEST(test_ui_CategorySort, HeaderClickSortsByCode)
{
    ::Test::TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    CatalogRepository repository(db.session(), db.resourcesDirectory());
    ASSERT_GT(::Test::seedCategory(db, "Z9", "Zebra"), 0);
    ASSERT_GT(::Test::seedCategory(db, "A1", "Apple"), 0);

    CategoryManagerDialog dialog(repository);
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("A1"));
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("Z9"));
}
