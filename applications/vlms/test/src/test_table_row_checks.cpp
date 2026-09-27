#include "ui/TableHeaderSort.h"
#include "ui/TableRowChecks.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QImage>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTest>

#include <gtest/gtest.h>

namespace {

void addRow(QTableWidget* table, int row, qint64 id, const QString& title)
{
    auto* item = new QTableWidgetItem(title);
    item->setData(Qt::UserRole, id);
    table->setItem(row, 0, item);
    table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("other")));
}

QPoint cellIndicator(QTableWidget* table, int row)
{
    QStyleOptionViewItem option;
    option.initFrom(table);
    option.rect = table->visualRect(table->model()->index(row, 0));
    option.features = QStyleOptionViewItem::HasCheckIndicator;
    option.checkState = Qt::Unchecked;
    const QRect check = table->style()->subElementRect(
        QStyle::SE_ItemViewItemCheckIndicator, &option, table);
    return check.center();
}

class test_ui_TableRowChecks : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_table = std::make_unique<QTableWidget>(3, 2);
        m_table->setHorizontalHeaderLabels({QStringLiteral("Title"), QStringLiteral("Other")});
        m_table->resize(480, 200);
        addRow(m_table.get(), 0, 10, QStringLiteral("Alpha"));
        addRow(m_table.get(), 1, 20, QStringLiteral("Beta"));
        addRow(m_table.get(), 2, 30, QStringLiteral("Gamma"));
        m_checks = std::make_unique<TableRowChecks>(m_table.get());
        m_sort = std::make_unique<VLMS::TableHeaderSort>(m_table.get());
        QObject::connect(m_sort.get(), &VLMS::TableHeaderSort::sortChanged, [this](int, bool) {
            ++m_sorts;
        });
        m_checks->clear();
        m_table->selectRow(0);
        m_table->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(m_table.get()));
    }

    void clickViewport(const QPoint& pos)
    {
        QTest::mouseClick(m_table->viewport(), Qt::LeftButton, Qt::NoModifier, pos);
    }

    void clickHeader(int x)
    {
        QTest::mouseClick(m_table->horizontalHeader()->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(x, m_table->horizontalHeader()->height() / 2));
    }

    std::unique_ptr<QTableWidget> m_table;
    std::unique_ptr<TableRowChecks> m_checks;
    std::unique_ptr<VLMS::TableHeaderSort> m_sort;
    int m_sorts = 0;
};

}  // namespace

TEST_F(test_ui_TableRowChecks, TickingARowDoesNotMoveTheHighlight)
{
    clickViewport(cellIndicator(m_table.get(), 1));

    EXPECT_EQ(m_table->currentRow(), 0);
    EXPECT_EQ(m_checks->checkedIds(), QList<qint64>({20}));
    EXPECT_EQ(m_checks->checkedRows(), QList<int>({1}));
    EXPECT_EQ(m_sorts, 0);
}

TEST_F(test_ui_TableRowChecks, HeaderBoxSelectsEveryOnScreenRowAndDoesNotSort)
{
    const int left = m_table->horizontalHeader()->sectionViewportPosition(0);
    clickHeader(left + 8);

    EXPECT_EQ(m_sorts, 0);
    EXPECT_EQ(m_checks->checkedIds(), (QList<qint64>{10, 20, 30}));

    clickViewport(cellIndicator(m_table.get(), 1));
    EXPECT_EQ(m_checks->checkedIds(), (QList<qint64>{10, 30}));

    clickHeader(left + 8);
    EXPECT_EQ(m_sorts, 0);
    EXPECT_EQ(m_checks->checkedIds(), (QList<qint64>{10, 20, 30}));

    clickHeader(left + 8);
    EXPECT_EQ(m_checks->checkedIds(), QList<qint64>());
}

TEST_F(test_ui_TableRowChecks, HeaderTextSorts)
{
    const QHeaderView* header = m_table->horizontalHeader();
    const int textX = header->sectionViewportPosition(0) + header->sectionSize(0) / 2;
    clickHeader(textX);

    EXPECT_EQ(m_sorts, 1);
    EXPECT_TRUE(m_checks->checkedIds().isEmpty());
}

TEST_F(test_ui_TableRowChecks, ClearDropsEveryTick)
{
    clickViewport(cellIndicator(m_table.get(), 0));
    clickViewport(cellIndicator(m_table.get(), 2));
    ASSERT_EQ(m_checks->checkedIds().size(), 2);

    m_checks->clear();

    EXPECT_TRUE(m_checks->checkedIds().isEmpty());
    EXPECT_TRUE(m_checks->checkedRows().isEmpty());
}

TEST_F(test_ui_TableRowChecks, HeaderBoxSitsOnTheRightUnderRtl)
{
    m_table->setLayoutDirection(Qt::RightToLeft);
    m_table->horizontalHeader()->setLayoutDirection(Qt::RightToLeft);
    QTest::qWait(0);

    const QHeaderView* header = m_table->horizontalHeader();
    const int sectionLeft = header->sectionViewportPosition(0);
    const int sectionRight = sectionLeft + header->sectionSize(0);
    clickHeader(sectionRight - 8);

    EXPECT_EQ(m_sorts, 0);
    EXPECT_EQ(m_checks->checkedIds(), (QList<qint64>{10, 20, 30}));

    m_checks->clear();
    clickHeader(sectionLeft + header->sectionSize(0) / 2);
    EXPECT_EQ(m_sorts, 1);
    EXPECT_TRUE(m_checks->checkedIds().isEmpty());
}

TEST(test_ui_CheckHeaderLayout, ReplacingTheHeaderKeepsTheColumnWidths)
{
    auto table = std::make_unique<QTableWidget>(1, 3);
    QHeaderView* header = table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(true);
    header->setCascadingSectionResizes(false);
    header->setMinimumSectionSize(60);
    header->resizeSection(0, 180);
    header->resizeSection(1, 90);

    TableRowChecks checks(table.get());

    QHeaderView* next = table->horizontalHeader();
    EXPECT_NE(next, header);
    EXPECT_TRUE(next->stretchLastSection());
    EXPECT_FALSE(next->cascadingSectionResizes());
    EXPECT_EQ(next->minimumSectionSize(), 60);
    EXPECT_EQ(next->sectionResizeMode(0), QHeaderView::Interactive);
    EXPECT_EQ(next->sectionSize(0), 180);
    EXPECT_EQ(next->sectionSize(1), 90);
}

// The application stylesheet's header rule leaves the painter clipped to the
// label. A checkbox drawn after that is clipped away, so the leading edge
// stays the header fill and a click there is a sort. This reads the pixels.
TEST(test_ui_CheckHeaderLayout, HeaderCheckboxIsPaintedAndItsClickDoesNotSort)
{
    const QString previousSheet = qApp->styleSheet();
    const QPalette previousPalette = qApp->palette();
    qApp->setPalette(VLMS::applicationPalette(VLMS::ThemeMode::Light));
    qApp->setStyleSheet(VLMS::applicationStylesheet(VLMS::ThemeMode::Light));

    auto restore = qScopeGuard([&] {
        qApp->setPalette(previousPalette);
        qApp->setStyleSheet(previousSheet);
    });

    for (const Qt::LayoutDirection direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        SCOPED_TRACE(direction == Qt::RightToLeft ? "rtl" : "ltr");

        auto table = std::make_unique<QTableWidget>(2, 2);
        table->setLayoutDirection(direction);
        table->setHorizontalHeaderLabels({QStringLiteral("Title"), QStringLiteral("Other")});
        table->resize(480, 200);
        auto* title = new QTableWidgetItem(QStringLiteral("Alpha"));
        title->setData(Qt::UserRole, 10);
        table->setItem(0, 0, title);
        table->setItem(0, 1, new QTableWidgetItem(QStringLiteral("other")));
        auto* beta = new QTableWidgetItem(QStringLiteral("Beta"));
        beta->setData(Qt::UserRole, 20);
        table->setItem(1, 0, beta);
        table->setItem(1, 1, new QTableWidgetItem(QStringLiteral("other")));

        TableRowChecks checks(table.get());
        checks.clear();
        table->horizontalHeader()->setLayoutDirection(direction);
        table->horizontalHeader()->resizeSection(0, 220);
        VLMS::TableHeaderSort sort(table.get());
        int sorts = 0;
        QObject::connect(&sort, &VLMS::TableHeaderSort::sortChanged, [&sorts](int, bool) {
            ++sorts;
        });

        table->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(table.get()));
        table->horizontalHeader()->viewport()->repaint();

        QHeaderView* header = table->horizontalHeader();
        constexpr int kMargin = 4;
        const int size = header->style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, header);
        const int sectionLeft = header->sectionViewportPosition(0);
        const int sectionRight = sectionLeft + header->sectionSize(0);
        const QRect box = direction == Qt::RightToLeft
                              ? QRect(sectionRight - kMargin - size + 1,
                                      (header->height() - size) / 2, size, size)
                              : QRect(sectionLeft + kMargin, (header->height() - size) / 2, size,
                                      size);

        const QImage image = header->viewport()->grab().toImage();
        const int dpr = qMax(1, qRound(image.devicePixelRatio()));
        const QRect device = QRect(box.topLeft() * dpr, box.size() * dpr);
        const QPoint fillAt = direction == Qt::RightToLeft
                                  ? QPoint(sectionLeft + 8, 2)
                                  : QPoint(sectionRight - 8, 2);
        const QColor fill = image.pixelColor(fillAt * dpr);
        int differs = 0;
        for (int y = device.top(); y <= device.bottom(); ++y) {
            for (int x = device.left(); x <= device.right(); ++x) {
                if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
                    continue;
                }
                const QColor pixel = image.pixelColor(x, y);
                const int distance = qAbs(pixel.red() - fill.red()) + qAbs(pixel.green() - fill.green())
                                     + qAbs(pixel.blue() - fill.blue());
                if (distance > 24) {
                    ++differs;
                }
            }
        }
        EXPECT_GT(differs, 0);

        QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier, box.center());
        EXPECT_EQ(sorts, 0);
        EXPECT_EQ(sort.column(), -1);
        EXPECT_EQ(checks.checkedIds(), (QList<qint64>{10, 20}));

        checks.clear();
        const int textX = sectionLeft + header->sectionSize(0) / 2;
        QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(textX, header->height() / 2));
        EXPECT_EQ(sorts, 1);
        EXPECT_EQ(sort.column(), 0);
        EXPECT_TRUE(checks.checkedIds().isEmpty());
    }
}

// Catalogue opens on every live book. Each setItem emits itemChanged, and a
// recount of every row already on screen turns that fill into a long stall
// before MainWindow::show. Ten times as many rows must stay near-linear.
TEST(test_ui_TableRowChecksFill, FillingRowsDoesNotRescanTheWholeTable)
{
    auto fill = [](int rows) {
        auto table = std::make_unique<QTableWidget>(rows, 2);
        TableRowChecks checks(table.get());
        QElapsedTimer timer;
        timer.start();
        for (int row = 0; row < rows; ++row) {
            auto* title = new QTableWidgetItem(QString::number(row));
            title->setData(Qt::UserRole, row);
            table->setItem(row, 0, title);
            table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("x")));
        }
        return timer.elapsed();
    };

    const qint64 small = fill(200);
    const qint64 large = fill(2000);
    EXPECT_LT(large, small * 25 + 200);

    auto table = std::make_unique<QTableWidget>(1, 1);
    TableRowChecks checks(table.get());
    auto* title = new QTableWidgetItem(QStringLiteral("one"));
    title->setData(Qt::UserRole, 7);
    table->setItem(0, 0, title);
    table->item(0, 0)->setCheckState(Qt::Checked);
    EXPECT_EQ(checks.checkedIds(), QList<qint64>({7}));
}

TEST_F(test_ui_TableRowChecks, RemovingATickedRowUpdatesTheHeaderCount)
{
    m_table->item(0, 0)->setCheckState(Qt::Checked);
    m_table->item(1, 0)->setCheckState(Qt::Checked);
    QSignalSpy spy(m_checks.get(), &TableRowChecks::checkedCountChanged);

    m_table->removeRow(0);

    ASSERT_FALSE(spy.isEmpty());
    EXPECT_EQ(spy.last().at(0).toInt(), 1);
    EXPECT_EQ(m_checks->checkedIds(), QList<qint64>({20}));
}

// The checks object outlives the table's ~QWidget, so viewport events and
// modelReset arrive while the dynamic type is already QWidget. UBSan rejects
// the QTableWidget calls from that window, and the sanitizer run aborts.
TEST(test_ui_TableRowChecksLifetime, DestroyingTheTableWhileChecksAreItsChildStaysDefined)
{
    auto table = std::make_unique<QTableWidget>(2, 2);
    table->setHorizontalHeaderLabels({QStringLiteral("Title"), QStringLiteral("Other")});
    addRow(table.get(), 0, 10, QStringLiteral("Alpha"));
    addRow(table.get(), 1, 20, QStringLiteral("Beta"));
    new TableRowChecks(table.get());
    table->item(0, 0)->setCheckState(Qt::Checked);
    table->show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(table.get()));
    table.reset();
}
