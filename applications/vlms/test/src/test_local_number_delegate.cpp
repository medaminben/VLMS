#include "ui/catalog/LocalNumberDelegate.h"
#include "ui/Theme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QImage>
#include <QPainter>
#include <QBrush>
#include <QFont>
#include <QPixmap>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTest>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using namespace VLMS;

class test_ui_LocalNumberDelegate : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Two rows so tests can move the click from one multi-number cell to
        // another; existing single-row tests only ever touch row 0.
        m_table = std::make_unique<QTableWidget>(2, 1);
        m_delegate = std::make_unique<LocalNumberDelegate>();
        m_table->setItemDelegateForColumn(0, m_delegate.get());
        m_table->setItem(0, 0, new QTableWidgetItem);
        m_table->setItem(1, 0, new QTableWidgetItem);
    }

    void setNumbers(const QStringList& numbers)
    {
        m_table->item(0, 0)->setData(LocalNumberDelegate::kNumbersRole, numbers);
    }

    void setOnLoan(const QStringList& numbers)
    {
        m_table->item(0, 0)->setData(LocalNumberDelegate::kOnLoanRole, numbers);
    }

    void clickFirstCell()
    {
        const QRect cell = m_table->visualItemRect(m_table->item(0, 0));
        QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, cell.center());
    }

    std::unique_ptr<QTableWidget> m_table;
    std::unique_ptr<LocalNumberDelegate> m_delegate;
};

TEST_F(test_ui_LocalNumberDelegate, NoNumbersReadAsADash)
{
    EXPECT_EQ(LocalNumberDelegate::cellText({}), QString::fromUtf8("—"));
}

TEST_F(test_ui_LocalNumberDelegate, OneNumberReadsAsItself)
{
    EXPECT_EQ(LocalNumberDelegate::cellText({QStringLiteral("1042")}),
              QStringLiteral("1042"));
}

TEST_F(test_ui_LocalNumberDelegate, SeveralNumbersReadAsTheFirstAndTheRestAsACount)
{
    const QStringList numbers{QStringLiteral("1042"), QStringLiteral("1043"),
                              QStringLiteral("1044")};
    EXPECT_EQ(LocalNumberDelegate::cellText(numbers), QStringLiteral("1042 (+2)"));
}

TEST_F(test_ui_LocalNumberDelegate, ClickingACellWithSeveralNumbersOpensOneCombo)
{
    setNumbers({QStringLiteral("10"), QStringLiteral("11")});
    EXPECT_EQ(m_table->viewport()->findChildren<QComboBox*>().size(), 0);

    const QRect cell = m_table->visualItemRect(m_table->item(0, 0));
    QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, cell.center());

    const auto combos = m_table->viewport()->findChildren<QComboBox*>();
    ASSERT_EQ(combos.size(), 1);
    EXPECT_EQ(combos.front()->count(), 2);
    EXPECT_EQ(combos.front()->itemText(0), QStringLiteral("10"));
    EXPECT_EQ(combos.front()->itemText(1), QStringLiteral("11"));
}

TEST_F(test_ui_LocalNumberDelegate, ClickingACellWithOneNumberOpensNothing)
{
    setNumbers({QStringLiteral("10")});
    const QRect cell = m_table->visualItemRect(m_table->item(0, 0));
    QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, cell.center());
    EXPECT_EQ(m_table->viewport()->findChildren<QComboBox*>().size(), 0);
}

TEST_F(test_ui_LocalNumberDelegate, ClickingACellWithNoNumbersOpensNothing)
{
    setNumbers({});
    const QRect cell = m_table->visualItemRect(m_table->item(0, 0));
    QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, cell.center());
    EXPECT_EQ(m_table->viewport()->findChildren<QComboBox*>().size(), 0);
}

TEST_F(test_ui_LocalNumberDelegate, ClickingAnotherMultiNumberRowReplacesTheOpenCombo)
{
    setNumbers({QStringLiteral("10"), QStringLiteral("11")});
    m_table->item(1, 0)->setData(
        LocalNumberDelegate::kNumbersRole,
        QStringList{QStringLiteral("20"), QStringLiteral("21"), QStringLiteral("22")});

    const QRect firstCell = m_table->visualItemRect(m_table->item(0, 0));
    QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, firstCell.center());
    ASSERT_EQ(m_table->viewport()->findChildren<QComboBox*>().size(), 1);

    const QRect secondCell = m_table->visualItemRect(m_table->item(1, 0));
    QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, secondCell.center());
    // The stale combo's deleteLater() only schedules its destruction; let the
    // event loop run the deferred delete before counting survivors.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    const auto combos = m_table->viewport()->findChildren<QComboBox*>();
    ASSERT_EQ(combos.size(), 1);
    EXPECT_EQ(combos.front()->count(), 3);
    EXPECT_EQ(combos.front()->itemText(0), QStringLiteral("20"));
    EXPECT_EQ(combos.front()->itemText(1), QStringLiteral("21"));
    EXPECT_EQ(combos.front()->itemText(2), QStringLiteral("22"));
}

TEST_F(test_ui_LocalNumberDelegate, TheSelectedNumberIsWhatTheCellShows)
{
    const QStringList numbers{QStringLiteral("10"), QStringLiteral("11")};
    EXPECT_EQ(LocalNumberDelegate::cellText(numbers, QStringLiteral("11")),
              QStringLiteral("11 (+1)"));
}

TEST_F(test_ui_LocalNumberDelegate, ANumberThisRowDoesNotHoldLeavesTheCellOnTheFirst)
{
    const QStringList numbers{QStringLiteral("10"), QStringLiteral("11")};
    EXPECT_EQ(LocalNumberDelegate::cellText(numbers, QStringLiteral("99")),
              QStringLiteral("10 (+1)"));
}

TEST_F(test_ui_LocalNumberDelegate, PickingANumberMakesItTheOneTheCellShows)
{
    setNumbers({QStringLiteral("10"), QStringLiteral("11")});
    const QRect cell = m_table->visualItemRect(m_table->item(0, 0));
    QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, cell.center());

    auto* combo = m_table->viewport()->findChild<QComboBox*>();
    ASSERT_NE(combo, nullptr);
    combo->setCurrentIndex(1);

    const QTableWidgetItem* item = m_table->item(0, 0);
    EXPECT_EQ(item->data(LocalNumberDelegate::kSelectedRole).toString(), QStringLiteral("11"));
    EXPECT_EQ(LocalNumberDelegate::cellText(
                  item->data(LocalNumberDelegate::kNumbersRole).toStringList(),
                  item->data(LocalNumberDelegate::kSelectedRole).toString()),
              QStringLiteral("11 (+1)"));
    // Picking marks a copy; it does not reshuffle the row's numbers, so the
    // next drop-down still reads in accession order.
    EXPECT_EQ(item->data(LocalNumberDelegate::kNumbersRole).toStringList(),
              (QStringList{QStringLiteral("10"), QStringLiteral("11")}));
    EXPECT_TRUE(item->text().isEmpty());
}

TEST_F(test_ui_LocalNumberDelegate, ReopeningTheComboStartsOnTheNumberAlreadyPicked)
{
    setNumbers({QStringLiteral("10"), QStringLiteral("11"), QStringLiteral("12")});
    m_table->item(0, 0)->setData(LocalNumberDelegate::kSelectedRole, QStringLiteral("12"));

    const QRect cell = m_table->visualItemRect(m_table->item(0, 0));
    QTest::mouseClick(m_table->viewport(), Qt::LeftButton, {}, cell.center());

    auto* combo = m_table->viewport()->findChild<QComboBox*>();
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->currentText(), QStringLiteral("12"));
    // Opening it is not picking: the row is left as it was found.
    EXPECT_EQ(m_table->item(0, 0)->data(LocalNumberDelegate::kSelectedRole).toString(),
              QStringLiteral("12"));
}

namespace {

/// Renders row 0's multi-number cell into a QPixmap under the real dark
/// theme's style, palette and stylesheet -- the same three Application.cpp
/// applies, in the same order, before any window exists -- and returns the
/// darkest-background band of the painted chrome as a QImage crop.
///
/// The chrome's own frame sits a couple of pixels inside the cell rect, and
/// the digits/arrow are drawn further in still (vertically centred, so
/// nowhere near the chrome's top edge). Rows 2..9 below the cell's top are
/// past the frame stroke and above any glyph, so every pixel sampled there
/// is either the chrome's border or its fill -- never anti-aliased text or
/// arrow, which would legitimately be light-on-dark and must not trip this
/// check.
class DarkModeRender {
public:
    explicit DarkModeRender(QTableWidget* table)
        : m_table(table)
        , m_previousStyleName(QApplication::style()->objectName())
        , m_previousPalette(QApplication::palette())
        , m_previousStyleSheet(qApp->styleSheet())
    {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(VLMS::applicationPalette(VLMS::ThemeMode::Dark));
        qApp->setStyleSheet(VLMS::applicationStylesheet(VLMS::ThemeMode::Dark));
        m_table->setPalette(QApplication::palette());
        m_table->viewport()->setAutoFillBackground(true);
        m_table->resize(300, 120);
        // An unshown viewport's render() on Qt 6.4 (CI) leaves more of the
        // destination pixmap showing through than 6.8 (this machine). Show it
        // so the dark surface is what antialiasing blends against.
        m_table->show();
        m_table->ensurePolished();
        QApplication::processEvents();
    }

    ~DarkModeRender()
    {
        m_table->hide();
        qApp->setStyleSheet(m_previousStyleSheet);
        QApplication::setPalette(m_previousPalette);
        QApplication::setStyle(m_previousStyleName);
    }

    /// The highest (lightest) pixel lightness found in the chrome's frame
    /// band for row 0's cell, 0..255.
    int maxFrameBandLightness() const
    {
        const QRect cell = m_table->visualItemRect(m_table->item(0, 0));
        QPixmap pixmap(m_table->viewport()->size());
        // Not white: QWidget::render() of an offscreen viewport leaves a sliver
        // of the destination showing, and the rounded chrome antialiases
        // against it. A white fill made that sliver read as a light box in a
        // dark table -- the failure CI reported (lightness 152 / 168 vs 110).
        pixmap.fill(m_table->palette().color(QPalette::Base));
        QPainter painter(&pixmap);
        m_table->viewport()->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        painter.end();
        const QImage image = pixmap.toImage();

        int maxLightness = 0;
        // Inset past the 1px outline, the radius-4 corners, and the sliver
        // QWidget::render() leaves unpainted at the viewport's left/top edge.
        // The digits sit further in still (vertically centred).
        for (int y = cell.top() + 5; y <= cell.top() + 8; ++y) {
            for (int x = cell.left() + 8; x < cell.right() - 8; ++x) {
                maxLightness = std::max(maxLightness, image.pixelColor(x, y).lightness());
            }
        }
        return maxLightness;
    }

private:
    QTableWidget* m_table;
    QString m_previousStyleName;
    QPalette m_previousPalette;
    QString m_previousStyleSheet;
};

}  // namespace

TEST_F(test_ui_LocalNumberDelegate, MultiNumberCellStaysDarkInDarkMode)
{
    setNumbers({QStringLiteral("10"), QStringLiteral("11")});
    const DarkModeRender render(m_table.get());

    // The theme's darkest legitimate frame/fill roles in dark mode (border
    // #334155, surface #111c2e) sit at lightness 68 and 32; its lightest
    // *text* role (#e2e8f0) sits at 230. 110 sits well clear of the former
    // and far short of the latter, so this only trips if the frame/fill
    // band itself is painted light -- a cell rendering as a bright box in a
    // dark table, not the arrow or digits doing their job of being legible.
    EXPECT_LT(render.maxFrameBandLightness(), 110)
        << "the multi-number cell's frame/fill reads lighter than the dark "
           "table around it";
}

TEST_F(test_ui_LocalNumberDelegate, MultiNumberCellStaysDarkInDarkModeWhenSelected)
{
    setNumbers({QStringLiteral("10"), QStringLiteral("11")});
    const DarkModeRender render(m_table.get());
    m_table->selectRow(0);

    EXPECT_LT(render.maxFrameBandLightness(), 110)
        << "the selected multi-number cell's frame/fill reads lighter than "
           "the dark table around it";
}

namespace {

/// How many of an image's pixels lean red and how many lean green, so a test
/// can say which colour the digits carry without pinning an exact RGB the
/// theme is free to retune.
///
/// The threshold is deliberately coarse: every neutral role in both themes is
/// a slate grey whose channels sit within a few points of each other, and the
/// chrome the delegate paints around a multi-number cell is drawn from those
/// same roles. Only the numbers are painted from copyAvailableColor() /
/// copyOnLoanColor(), and both separate their channels by well over 40.
struct Ink {
    int reddish = 0;
    int greenish = 0;
};

Ink inkOf(const QImage& image, const QRect& area)
{
    constexpr int kLean = 40;
    Ink ink;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.red() - pixel.green() > kLean && pixel.red() - pixel.blue() > kLean) {
                ++ink.reddish;
            } else if (pixel.green() - pixel.red() > kLean
                       && pixel.green() - pixel.blue() > kLean) {
                ++ink.greenish;
            }
        }
    }
    return ink;
}

Ink inkOf(const QImage& image) { return inkOf(image, image.rect()); }

/// Puts the whole application into one theme for the life of the object, and
/// puts it back afterwards.
///
/// Both halves have to move together: the delegate reads Theme::mode() for its
/// two colours, while every neutral role around them comes from the palette
/// and the sheet. Setting one without the other renders a state the
/// application never shows.
class ThemedApp {
public:
    explicit ThemedApp(const VLMS::ThemeMode mode)
        : m_previousStyleName(QApplication::style()->objectName())
        , m_previousPalette(QApplication::palette())
        , m_previousStyleSheet(qApp->styleSheet())
        , m_previousMode(VLMS::Theme::mode())
    {
        VLMS::Theme::setMode(mode);
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QApplication::setPalette(VLMS::applicationPalette(mode));
        qApp->setStyleSheet(VLMS::applicationStylesheet(mode));
    }

    ~ThemedApp()
    {
        qApp->setStyleSheet(m_previousStyleSheet);
        QApplication::setPalette(m_previousPalette);
        QApplication::setStyle(m_previousStyleName);
        VLMS::Theme::setMode(m_previousMode);
    }

private:
    QString m_previousStyleName;
    QPalette m_previousPalette;
    QString m_previousStyleSheet;
    VLMS::ThemeMode m_previousMode;
};

/// Renders row 0's cell in `mode` and reports its ink.
class CellInk {
public:
    CellInk(QTableWidget* table, const VLMS::ThemeMode mode)
        : m_themed(mode)
    {
        table->setPalette(QApplication::palette());
        table->viewport()->setAutoFillBackground(true);
        table->resize(300, 120);
        table->show();
        table->ensurePolished();
        QApplication::processEvents();

        const QRect cell = table->visualItemRect(table->item(0, 0));
        QPixmap pixmap(table->viewport()->size());
        pixmap.fill(table->palette().color(QPalette::Base));
        QPainter painter(&pixmap);
        table->viewport()->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        painter.end();
        table->hide();
        m_ink = inkOf(pixmap.toImage(), cell);
    }

    int reddish() const { return m_ink.reddish; }
    int greenish() const { return m_ink.greenish; }

private:
    ThemedApp m_themed;
    Ink m_ink;
};

}  // namespace

TEST_F(test_ui_LocalNumberDelegate, TheLeadIsTheOnlyPartTheStateColours)
{
    const QStringList numbers{QStringLiteral("505"), QStringLiteral("506"),
                              QStringLiteral("507")};
    EXPECT_EQ(LocalNumberDelegate::leadNumber(numbers), QStringLiteral("505"));
    EXPECT_EQ(LocalNumberDelegate::restSuffix(numbers), QStringLiteral(" (+2)"));
    EXPECT_EQ(LocalNumberDelegate::leadNumber(numbers) + LocalNumberDelegate::restSuffix(numbers),
              LocalNumberDelegate::cellText(numbers));
}

TEST_F(test_ui_LocalNumberDelegate, TheLeadFollowsTheNumberTheCellShows)
{
    const QStringList numbers{QStringLiteral("505"), QStringLiteral("4635")};
    EXPECT_EQ(LocalNumberDelegate::leadNumber(numbers, QStringLiteral("4635")),
              QStringLiteral("4635"));
}

TEST_F(test_ui_LocalNumberDelegate, ASingleCopyOnTheShelfIsWrittenInGreen)
{
    setNumbers({QStringLiteral("505")});
    const CellInk ink(m_table.get(), VLMS::ThemeMode::Light);

    EXPECT_GT(ink.greenish(), 0) << "an available copy's number should be green";
    EXPECT_EQ(ink.reddish(), 0);
}

TEST_F(test_ui_LocalNumberDelegate, ASingleCopyOutOnLoanIsWrittenInRed)
{
    setNumbers({QStringLiteral("505")});
    setOnLoan({QStringLiteral("505")});
    const CellInk ink(m_table.get(), VLMS::ThemeMode::Light);

    EXPECT_GT(ink.reddish(), 0) << "a copy out on loan should be red";
    EXPECT_EQ(ink.greenish(), 0);
}

TEST_F(test_ui_LocalNumberDelegate, TheStateColourSurvivesDarkMode)
{
    setNumbers({QStringLiteral("505")});
    setOnLoan({QStringLiteral("505")});
    const CellInk ink(m_table.get(), VLMS::ThemeMode::Dark);

    EXPECT_GT(ink.reddish(), 0) << "the dark-mode red must still read as red";
    EXPECT_EQ(ink.greenish(), 0);
}

TEST_F(test_ui_LocalNumberDelegate, TheStateColourSurvivesSelectingTheRow)
{
    // A number that turned slate the moment the librarian clicked its row would
    // be useless: the colour is meant to be read while the row is the current
    // one, not only while it is not.
    setNumbers({QStringLiteral("505")});
    setOnLoan({QStringLiteral("505")});
    m_table->selectRow(0);
    const CellInk ink(m_table.get(), VLMS::ThemeMode::Light);

    EXPECT_GT(ink.reddish(), 0) << "a selected row must keep its state colour";
    EXPECT_EQ(ink.greenish(), 0);
}

TEST_F(test_ui_LocalNumberDelegate, OnlyTheLeadCopysStateIsPainted)
{
    // Three copies, and the two the cell does not name are out. The cell speaks
    // for 505 alone, so it is green; the "(+2)" says nothing about the rest.
    setNumbers({QStringLiteral("505"), QStringLiteral("506"), QStringLiteral("507")});
    setOnLoan({QStringLiteral("506"), QStringLiteral("507")});
    const CellInk ink(m_table.get(), VLMS::ThemeMode::Light);

    EXPECT_GT(ink.greenish(), 0);
    EXPECT_EQ(ink.reddish(), 0) << "the count of the other copies carries no state";
}

TEST_F(test_ui_LocalNumberDelegate, ABookWithNoCopiesIsNotColoured)
{
    setNumbers({});
    const CellInk ink(m_table.get(), VLMS::ThemeMode::Light);

    EXPECT_EQ(ink.greenish(), 0) << "the em dash names no copy, so it has no state";
    EXPECT_EQ(ink.reddish(), 0);
}

TEST_F(test_ui_LocalNumberDelegate, EveryDropDownEntryCarriesItsOwnState)
{
    setNumbers({QStringLiteral("505"), QStringLiteral("506")});
    setOnLoan({QStringLiteral("506")});
    clickFirstCell();

    const auto combos = m_table->viewport()->findChildren<QComboBox*>();
    ASSERT_EQ(combos.size(), 1);
    const QComboBox* combo = combos.first();
    ASSERT_EQ(combo->count(), 2);

    EXPECT_EQ(combo->itemData(0, Qt::ForegroundRole).value<QBrush>().color(),
              VLMS::copyAvailableColor());
    EXPECT_EQ(combo->itemData(1, Qt::ForegroundRole).value<QBrush>().color(),
              VLMS::copyOnLoanColor());
    EXPECT_FALSE(combo->itemData(0, Qt::FontRole).isValid());
    EXPECT_TRUE(combo->itemData(1, Qt::FontRole).value<QFont>().italic());
}

TEST_F(test_ui_LocalNumberDelegate, TheDropDownEntriesAreActuallyPaintedInTheirState)
{
    // The roles above are set on the items whether or not anything honours
    // them. Under the application stylesheet they were not: the theme's
    // "QComboBox QAbstractItemView { color: ... }" rule reaches
    // QStyleSheetStyle::drawControl, which reconfigures the option's palette
    // and paints over the item's brush, so every entry rendered slate while the
    // data test above still passed. This one renders the popup and reads the
    // pixels back, which is the only level that can tell the two apart.
    const ThemedApp themed(VLMS::ThemeMode::Light);
    setNumbers({QStringLiteral("505"), QStringLiteral("506")});
    setOnLoan({QStringLiteral("506")});
    clickFirstCell();

    const auto combos = m_table->viewport()->findChildren<QComboBox*>();
    ASSERT_EQ(combos.size(), 1);
    QAbstractItemView* view = combos.first()->view();
    ASSERT_NE(view, nullptr);
    view->resize(120, 80);
    view->ensurePolished();

    QPixmap pixmap(view->viewport()->size());
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);
    view->viewport()->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();

    const Ink ink = inkOf(pixmap.toImage());
    EXPECT_GT(ink.greenish, 0) << "the entry for the copy on the shelf should be green";
    EXPECT_GT(ink.reddish, 0) << "the entry for the copy out on loan should be red";
}
