#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/catalog/CatalogPage.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>

#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <gtest/gtest.h>

#include <memory>

using VLMS::Locale;
using namespace VLMS::Test;

class test_ui_CatalogSort : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_page.reset();
        m_catalog.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::unique_ptr<CatalogPage> m_page;
};

TEST_F(test_ui_CatalogSort, ClickingTitleHeaderSortsAscending)
{
    BookSeed zebra = uniqueBookSeed(1);
    zebra.title = "zebra";
    ASSERT_GT(seedBook(*m_db, zebra), 0);
    BookSeed apple = uniqueBookSeed(2);
    apple.title = "Apple";
    ASSERT_GT(seedBook(*m_db, apple), 0);

    m_page = std::make_unique<CatalogPage>(*m_catalog, *m_circulation);
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("Apple"));
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("zebra"));
}
