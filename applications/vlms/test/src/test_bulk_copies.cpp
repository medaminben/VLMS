#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"

#include "ui/catalog/BookCopiesTable.h"
#include "ui/catalog/FreeLocalNumberDelegate.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>

#include <QComboBox>
#include <QImage>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTest>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

namespace {

void tickRow(QTableWidget* table, int row)
{
    QTableWidgetItem* item = table->item(row, 0);
    ASSERT_NE(item, nullptr);
    item->setCheckState(Qt::Checked);
}

}  // namespace

class test_ui_BulkCopies : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_copies.reset();
        m_catalog.reset();
        m_db.reset();
    }

    QTableWidget* grid() const
    {
        return m_copies->findChild<QTableWidget*>(QStringLiteral("copiesTable"));
    }

    void clickRemove()
    {
        m_copies->retranslateUi();
        clickButtonWithText(m_copies.get(), qs(Core::Strings::t("book.copy.remove")));
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<BookCopiesTable> m_copies;
};

TEST_F(test_ui_BulkCopies, TwoTickedRowsAreRemovedAndTheHighlightedThirdStays)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.initialCopyCount = 3;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto ids = copyIdsOf(*m_db, bookId);
    m_copies = std::make_unique<BookCopiesTable>(*m_catalog);
    m_copies->loadCopies(bookId);
    QTableWidget* table = grid();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 3);
    table->selectRow(2);
    tickRow(table, 0);
    tickRow(table, 1);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickRemove(); }, qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, QStringLiteral("Remove 2 copies?"));
    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole + 1).toLongLong(), ids.at(2));
}

TEST_F(test_ui_BulkCopies, NothingTickedRemovesOnlyTheHighlightedRow)
{
    BookSeed seed = uniqueBookSeed(2);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    m_copies = std::make_unique<BookCopiesTable>(*m_catalog);
    m_copies->loadCopies(bookId);
    QTableWidget* table = grid();
    table->selectRow(0);

    clickRemove();

    EXPECT_EQ(table->rowCount(), 1);
}

TEST_F(test_ui_BulkCopies, ATickedOnLoanRowAndAReservedRowStay)
{
    BookSeed seed = uniqueBookSeed(3);
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    const auto ids = copyIdsOf(*m_db, bookId);
    const std::int64_t member = seedMember(*m_db, uniqueMemberSeed(3));
    ASSERT_GT(rawInsertLoan(*m_db, member, ids.at(0), "2026-09-01", "2026-09-15"), 0);

    m_copies = std::make_unique<BookCopiesTable>(*m_catalog);
    m_copies->loadCopies(bookId);
    m_copies->addReservedRow(QStringLiteral("arabic"), QStringLiteral("9001"),
                             QStringLiteral("AR-9001"));
    QTableWidget* table = grid();
    ASSERT_EQ(table->rowCount(), 3);
    tickRow(table, 0);
    tickRow(table, 1);
    tickRow(table, 2);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickRemove(); }, qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    ASSERT_EQ(table->rowCount(), 2);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole + 1).toLongLong(), ids.at(0));
    EXPECT_EQ(table->item(1, 0)->text(), QStringLiteral("9001"));
}

TEST_F(test_ui_BulkCopies, DoubleClickOnTheNumberStillOpensTheEditor)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(4));
    m_copies = std::make_unique<BookCopiesTable>(*m_catalog);
    m_copies->resize(800, 320);
    m_copies->show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(m_copies.get()));
    m_copies->loadCopies(bookId);

    QTableWidget* table = grid();
    table->setColumnWidth(0, 220);
    QApplication::processEvents();
    QTableWidgetItem* item = table->item(0, 0);
    ASSERT_NE(item, nullptr);
    item->setData(FreeLocalNumberDelegate::kFreeNumbersRole,
                  QStringList{item->text(), QStringLiteral("2")});

    const QRect cell = table->visualItemRect(item);
    ASSERT_GT(cell.width(), 40) << cell.x() << cell.y() << cell.width() << cell.height();

    QStyleOptionViewItem option;
    option.initFrom(table);
    option.rect = cell;
    option.features = QStyleOptionViewItem::HasCheckIndicator;
    option.checkState = Qt::Unchecked;
    option.widget = table;
    const QRect box = table->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option,
                                                     table);
    ASSERT_TRUE(cell.contains(box.center()));

    table->setFocus();
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, box.center());
    EXPECT_EQ(item->checkState(), Qt::Checked);
    EXPECT_EQ(table->indexWidget(table->model()->index(0, 0)), nullptr);

    // Qt only edits on a double-click when that click's press already landed
    // on the same cell. A click on the box must not count as that press.
    const QPoint onNumber(box.right() + 24, cell.center().y());
    ASSERT_TRUE(cell.contains(onNumber));
    ASSERT_FALSE(box.contains(onNumber));
    QTest::mousePress(table->viewport(), Qt::LeftButton, Qt::NoModifier, onNumber);
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, onNumber, 10);
    QApplication::processEvents();

    EXPECT_NE(qobject_cast<QComboBox*>(table->indexWidget(table->model()->index(0, 0))), nullptr);
}

TEST_F(test_ui_BulkCopies, TheCheckIndicatorIsPaintedInTheNumberCell)
{
    m_copies = std::make_unique<BookCopiesTable>(*m_catalog);
    m_copies->resize(640, 240);
    m_copies->show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(m_copies.get()));
    m_copies->addRow(QStringLiteral("ar"));
    QTableWidget* table = grid();
    QTableWidgetItem* item = table->item(0, 0);
    item->setCheckState(Qt::Checked);
    table->viewport()->repaint();
    QApplication::processEvents();

    QStyleOptionViewItem option;
    option.initFrom(table);
    option.rect = table->visualItemRect(item);
    option.features = QStyleOptionViewItem::HasCheckIndicator;
    option.checkState = Qt::Checked;
    option.widget = table;
    const QRect box = table->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option,
                                                     table);
    ASSERT_FALSE(box.isEmpty());

    const QImage image = table->viewport()->grab().toImage();
    const QColor base = table->palette().color(QPalette::Base);
    bool differs = false;
    for (int y = box.top(); y < box.bottom() && y < image.height(); ++y) {
        for (int x = box.left(); x < box.right() && x < image.width(); ++x) {
            if (x < 0 || y < 0) {
                continue;
            }
            if (image.pixelColor(x, y) != base) {
                differs = true;
            }
        }
    }
    EXPECT_TRUE(differs);
}
