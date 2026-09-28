#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/Theme.h"
#include "ui/UiHelpers.h"
#include "ui/catalog/CatalogPage.h"
#include "ui/circulation/CirculationPage.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>

#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QTableWidget>

#include <gtest/gtest.h>

#include <cstdint>

using VLMS::Locale;
using VLMS::Theme;
using VLMS::ThemeMode;
using namespace VLMS;
using namespace Test;

namespace {

/// What Application::setDarkTheme does, minus the signal: MainWindow cannot
/// be built in a test, so the test calls the page's retranslateUi itself,
/// which is all MainWindow::onThemeChanged does for a page.
void applyMode(ThemeMode mode)
{
    Theme::setMode(mode);
    qApp->setPalette(VLMS::applicationPalette());
    qApp->setStyleSheet(VLMS::applicationStylesheet());
    QApplication::processEvents();
}

int placeholderLightness(const QLabel* label)
{
    const QImage image = label->pixmap().toImage();
    if (image.isNull()) {
        return -1;
    }
    // The centre of the artwork is the card, the placeholder's largest tone.
    return QColor(image.pixel(image.width() / 2, image.height() / 2)).lightness();
}

/// Arabic and right to left, light, restored afterwards whatever happens.
class test_ui_PreviewTheme : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_original = Theme::mode();
        Locale::setCode("ar");
        qApp->setLayoutDirection(Qt::RightToLeft);
        applyMode(ThemeMode::Light);
        ASSERT_TRUE(m_db.isValid()) << m_db.lastError();
        for (int i = 1; i <= 5; ++i) {
            seedBook(m_db, uniqueBookSeed(i));
        }
    }

    void TearDown() override
    {
        applyMode(m_original);
        qApp->setLayoutDirection(Qt::LeftToRight);
        Locale::setCode("en");
    }

    ThemeMode m_original = ThemeMode::Light;
    TestDatabase m_db;
    Repositories::CatalogRepository m_catalog{m_db.session(), m_db.resourcesDirectory()};
    Repositories::MemberRepository m_memberRepo{m_db.session(), m_db.resourcesDirectory()};
    Repositories::CirculationRepository m_circulation{m_db.session()};
};

}  // namespace

TEST_F(test_ui_PreviewTheme, ArabicCataloguePlaceholderTurnsDarkWithTheTheme)
{
    CatalogPage page(m_catalog, m_circulation);
    page.resize(1440, 800);
    page.show();
    QApplication::processEvents();
    auto* table = page.findChild<QTableWidget*>();
    auto* cover = page.findChild<QLabel*>(QStringLiteral("bookCover"));
    ASSERT_NE(table, nullptr);
    ASSERT_NE(cover, nullptr);
    ASSERT_GT(placeholderLightness(cover), 200);

    applyMode(ThemeMode::Dark);
    page.retranslateUi();
    QApplication::processEvents();

    // The selection survives the refresh, and the card is repainted dark.
    EXPECT_FALSE(table->selectedItems().isEmpty());
    EXPECT_LT(placeholderLightness(cover), 100);
}

TEST_F(test_ui_PreviewTheme, ArabicCirculationPlaceholderTurnsDarkWithTheTheme)
{
    const std::int64_t member = seedMember(m_db, uniqueMemberSeed(1));
    const std::int64_t book = seedBook(m_db, uniqueBookSeed(9));
    ASSERT_GT(rawInsertLoan(m_db, member, copyIdsOf(m_db, book).front(), "2026-09-01",
                            "2026-09-15"),
              0);
    CirculationPage page(m_circulation, m_catalog, m_memberRepo);
    page.resize(1440, 800);
    page.show();
    QApplication::processEvents();
    auto* cover = page.findChild<QLabel*>(QStringLiteral("bookCover"));
    ASSERT_NE(cover, nullptr);
    auto* table = page.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_GT(placeholderLightness(cover), 200);

    applyMode(ThemeMode::Dark);
    page.retranslateUi();
    QApplication::processEvents();

    EXPECT_FALSE(table->selectedItems().isEmpty());
    EXPECT_LT(placeholderLightness(cover), 100);
}

TEST_F(test_ui_PreviewTheme, SelectTableRowSelectsTheWholeRowRightToLeft)
{
    // Two narrow columns in a wide table: the sections stop well short of the
    // viewport's right edge, which is where QTableView::selectRow looks.
    QTableWidget table(3, 2);
    table.resize(800, 300);
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 2; ++column) {
            table.setItem(row, column, new QTableWidgetItem(QString::number(row)));
        }
    }
    table.setColumnWidth(0, 60);
    table.setColumnWidth(1, 60);
    table.setSelectionBehavior(QAbstractItemView::SelectRows);
    table.show();
    QApplication::processEvents();

    VLMS::selectTableRow(&table, 1);

    EXPECT_EQ(table.selectedItems().size(), 2);
    EXPECT_EQ(table.currentRow(), 1);
}

TEST_F(test_ui_PreviewTheme, ThemeSwitchKeepsTheSameBookSelected)
{
    for (const Qt::LayoutDirection direction : {Qt::RightToLeft, Qt::LeftToRight}) {
        qApp->setLayoutDirection(direction);
        applyMode(ThemeMode::Light);
        CatalogPage page(m_catalog, m_circulation);
        page.resize(1440, 800);
        page.show();
        QApplication::processEvents();
        auto* table = page.findChild<QTableWidget*>();
        ASSERT_NE(table, nullptr);
        const int before = table->currentRow();
        ASSERT_EQ(before, 0);
        const qint64 id = table->item(before, 0)->data(Qt::UserRole).toLongLong();

        applyMode(ThemeMode::Dark);
        page.retranslateUi();
        QApplication::processEvents();

        ASSERT_GE(table->currentRow(), 0) << "direction " << direction;
        EXPECT_EQ(table->item(table->currentRow(), 0)->data(Qt::UserRole).toLongLong(), id)
            << "direction " << direction;
        EXPECT_EQ(table->selectionModel()->selectedRows().size(), 1) << "direction " << direction;
    }
}
