#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"

#include "ui/catalog/BookCopiesTable.h"
#include "ui/catalog/FreeLocalNumberDelegate.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Core/Locale.h>

#include <QAbstractItemModel>
#include <QComboBox>
#include <QStringList>
#include <QStyleOptionViewItem>
#include <QTableWidget>

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using VLMS::Locale;
using namespace VLMS::Test;

class test_ui_FreeNumberPicker : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }
    static void TearDownTestSuite() { Locale::setCode(Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_table.reset();
        m_catalog.reset();
        m_db.reset();
    }

    /// Leaves the arabic stock holding 1 and 4, so 2 and 3 are free and the
    /// next incremented number is 5.
    void seedArabicGap()
    {
        BookSeed seed = uniqueBookSeed(1);
        seed.language = "ar";
        seed.initialCopyCount = 2;
        const std::int64_t bookId = seedBook(*m_db, seed);
        const auto copies = copyIdsOf(*m_db, bookId);
        ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(0), "1"));
        ASSERT_TRUE(rawSetCopyLocalId(*m_db, copies.at(1), "4"));
    }

    QTableWidget* gridOf(BookCopiesTable* widget) const
    {
        return widget->findChild<QTableWidget*>(QStringLiteral("copiesTable"));
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<BookCopiesTable> m_table;
};

TEST_F(test_ui_FreeNumberPicker, ANewRowOpensOnTheNextNumberAndOffersTheGapsBehindIt)
{
    seedArabicGap();
    m_table = std::make_unique<BookCopiesTable>(*m_catalog);
    m_table->addRow(QStringLiteral("ar"));

    QTableWidget* grid = gridOf(m_table.get());
    ASSERT_NE(grid, nullptr);
    ASSERT_EQ(grid->rowCount(), 1);
    QTableWidgetItem* cell = grid->item(0, 0);
    ASSERT_NE(cell, nullptr);

    // What a save would use if nobody opened the list: the incremented number.
    EXPECT_EQ(cell->text(), QStringLiteral("5"));

    // The gaps are there to be picked, never picked for the librarian.
    const QStringList offered = cell->data(FreeLocalNumberDelegate::kFreeNumbersRole).toStringList();
    EXPECT_EQ(offered, (QStringList{QStringLiteral("5"), QStringLiteral("2"), QStringLiteral("3")}));
}

TEST_F(test_ui_FreeNumberPicker, TheEditorIsAComboOfThoseNumbersAndStillTakesATypedOne)
{
    seedArabicGap();
    m_table = std::make_unique<BookCopiesTable>(*m_catalog);
    m_table->addRow(QStringLiteral("ar"));

    QTableWidget* grid = gridOf(m_table.get());
    QTableWidgetItem* cell = grid->item(0, 0);
    auto* delegate = qobject_cast<FreeLocalNumberDelegate*>(grid->itemDelegateForColumn(0));
    ASSERT_NE(delegate, nullptr);

    QWidget* editor = delegate->createEditor(grid->viewport(), QStyleOptionViewItem{},
                                             grid->model()->index(0, 0));
    ASSERT_NE(editor, nullptr);
    auto* combo = qobject_cast<QComboBox*>(editor);
    ASSERT_NE(combo, nullptr);
    EXPECT_TRUE(combo->isEditable());
    EXPECT_EQ(combo->count(), 3);
    EXPECT_EQ(combo->itemText(0), QStringLiteral("5"));
    EXPECT_EQ(combo->itemText(1), QStringLiteral("2"));

    combo->setCurrentText(QStringLiteral("99"));
    delegate->setModelData(combo, grid->model(), grid->model()->index(0, 0));
    EXPECT_EQ(cell->text(), QStringLiteral("99"));
    delete editor;
}

TEST_F(test_ui_FreeNumberPicker, BookCreationKeepsIncrementingAndNeverTakesAGap)
{
    seedArabicGap();

    // Several numbers minted at once with nobody looking at them: precisely
    // where a gap must not be taken silently.
    BookSeed fresh = uniqueBookSeed(9);
    fresh.language = "ar";
    fresh.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, fresh);

    const auto copies = VLMS_UNWRAP(m_catalog->listCopies(bookId));
    ASSERT_EQ(copies.size(), 2u);
    EXPECT_EQ(copies.at(0).localId, "5");
    EXPECT_EQ(copies.at(1).localId, "6");
    // 2 and 3 are still free -- book creation did not help itself to them.
    EXPECT_EQ(VLMS_UNWRAP(m_catalog->listFreeLocalNumbers("arabic", 100)),
              (std::vector<std::string>{"2", "3"}));
}

TEST_F(test_ui_FreeNumberPicker, ARowLoadedFromTheDatabaseGetsNoDropDown)
{
    BookSeed seed = uniqueBookSeed(2);
    seed.language = "ar";
    seed.initialCopyCount = 1;
    const std::int64_t bookId = seedBook(*m_db, seed);
    m_table = std::make_unique<BookCopiesTable>(*m_catalog);
    m_table->loadCopies(bookId);

    QTableWidget* grid = gridOf(m_table.get());
    ASSERT_EQ(grid->rowCount(), 1);
    // An existing copy's number is not a fresh choice, so the list stays shut.
    EXPECT_TRUE(grid->item(0, 0)->data(FreeLocalNumberDelegate::kFreeNumbersRole)
                    .toStringList()
                    .isEmpty());

    auto* delegate = qobject_cast<FreeLocalNumberDelegate*>(grid->itemDelegateForColumn(0));
    ASSERT_NE(delegate, nullptr);
    QWidget* editor = delegate->createEditor(grid->viewport(), QStyleOptionViewItem{},
                                             grid->model()->index(0, 0));
    EXPECT_EQ(qobject_cast<QComboBox*>(editor), nullptr);
    delete editor;
}
