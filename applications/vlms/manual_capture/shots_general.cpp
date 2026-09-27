#include "Capture.h"
#include "Shots.h"

#include "Application.h"
#include "QtBridge.h"
#include "ui/MainWindow.h"
#include "ui/ClickableLabel.h"
#include "ui/LanguageSelector.h"
#include "ui/ListPageFrame.h"
#include "ui/TablePager.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTest>
#include <QWidget>

#include <algorithm>

using VLMS::T;

namespace ManualCapture {

namespace {

QRect windowRect(QWidget* widget, QWidget* window)
{
    return QRect(widget->mapTo(window, QPoint(0, 0)), widget->size());
}

QRect checkIndicatorInWindow(const QTableWidget* table, int row, QWidget* window)
{
    QStyleOptionViewItem option;
    option.initFrom(table);
    option.rect = table->visualRect(table->model()->index(row, 0));
    option.features = QStyleOptionViewItem::HasCheckIndicator;
    option.checkState = Qt::Checked;
    const QRect box = table->style()->subElementRect(
        QStyle::SE_ItemViewItemCheckIndicator, &option, table);
    const QPoint origin = table->viewport()->mapTo(window, box.topLeft());
    return QRect(origin, box.size());
}

QRect headerIndicatorInWindow(const QHeaderView* header, QWidget* window)
{
    constexpr int kMargin = 4;
    const int size = header->style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, header);
    const QRect section(header->sectionViewportPosition(0), 0, header->sectionSize(0), header->height());
    const int y = section.top() + (section.height() - size) / 2;
    QRect box;
    if (header->layoutDirection() == Qt::RightToLeft) {
        box = QRect(section.right() - kMargin - size + 1, y, size, size);
    } else {
        box = QRect(section.left() + kMargin, y, size, size);
    }
    const QPoint origin = header->viewport()->mapTo(window, box.topLeft());
    return QRect(origin, box.size());
}

QPixmap topOfTable(Capture& capture, QTableWidget* table, int rows)
{
    const QRect tableInWindow = windowRect(table, &capture.window());
    QRect crop = tableInWindow.adjusted(-12, -12, 12, 12).intersected(capture.window().rect());
    QPixmap image = capture.grab(&capture.window()).copy(crop);
    const int rowHeight = table->rowCount() > 0 ? table->rowHeight(0) : 28;
    const int keep = (tableInWindow.top() - crop.top()) + table->horizontalHeader()->height()
        + rows * rowHeight + 12;
    return image.copy(0, 0, image.width(), std::min(keep, image.height()));
}

QRect intoCrop(const QRect& inWindow, const QRect& crop)
{
    return inWindow.translated(-crop.topLeft());
}

QRect tableCrop(QTableWidget* table, QWidget* window)
{
    return windowRect(table, window).adjusted(-12, -12, 12, 12).intersected(window->rect());
}

int columnByHeader(const QTableWidget* table, const QString& label)
{
    for (int column = 0; column < table->columnCount(); ++column) {
        const QTableWidgetItem* item = table->horizontalHeaderItem(column);
        if (item != nullptr && item->text() == label) {
            return column;
        }
    }
    return -1;
}

void shotWindow(Capture& capture)
{
    QTableWidget* books = capture.table();
    if (books->rowCount() > 0) {
        books->selectRow(0);
        capture.settle();
    }

    auto* frame = capture.page()->findChild<VLMS::ListPageFrame*>();
    if (frame == nullptr) {
        throw std::runtime_error("catalogue frame is missing");
    }
    QPushButton* catalogNav = nullptr;
    const auto navs = capture.window().findChildren<QPushButton*>(QStringLiteral("navLink"));
    for (QPushButton* nav : navs) {
        if (nav->text() == T("nav.catalog")) {
            catalogNav = nav;
            break;
        }
    }
    auto* languages = capture.window().findChild<VLMS::LanguageSelector*>();
    auto* theme = capture.named<QPushButton>(QStringLiteral("themeToggle"), &capture.window());
    auto* footer = capture.named<QWidget>(QStringLiteral("appFooter"), &capture.window());
    if (catalogNav == nullptr || languages == nullptr || theme == nullptr || footer == nullptr) {
        throw std::runtime_error("a getting-started callout target is missing");
    }

    const QList<QWidget*> targets = {
        static_cast<QWidget*>(catalogNav),
        static_cast<QWidget*>(languages),
        static_cast<QWidget*>(theme),
        frame->filterColumn(),
        static_cast<QWidget*>(frame->searchEdit()),
        static_cast<QWidget*>(frame->table()),
        static_cast<QWidget*>(frame->pager()),
        frame->detailsPanel(),
        frame->buttonPad(),
        footer,
    };
    QPixmap image = capture.grab();
    image = capture.callouts(image, &capture.window(), targets);
    capture.save(QStringLiteral("gs-window"), image);
}

void shotDark(Capture& capture)
{
    auto* app = qobject_cast<Application*>(qApp);
    if (app == nullptr) {
        throw std::runtime_error("no application");
    }
    app->setDarkTheme(true);
    capture.settle(400);
    capture.save(QStringLiteral("gs-dark"), capture.grab());
}

void shotTicks(Capture& capture)
{
    QTableWidget* books = capture.table();
    if (books->rowCount() < 3) {
        throw std::runtime_error("catalogue has fewer than three rows");
    }
    for (int row = 0; row < 3; ++row) {
        QTableWidgetItem* item = books->item(row, 0);
        if (item == nullptr) {
            throw std::runtime_error("catalogue row has no title cell");
        }
        item->setCheckState(Qt::Checked);
    }
    capture.settle();

    const QRect crop = tableCrop(books, &capture.window());
    QPixmap image = topOfTable(capture, books, 8);
    const QList<QRect> marks = {
        intoCrop(checkIndicatorInWindow(books, 0, &capture.window()), crop),
        intoCrop(headerIndicatorInWindow(books->horizontalHeader(), &capture.window()), crop),
    };
    capture.save(QStringLiteral("gs-ticks"), capture.calloutsAt(image, marks));
}

void shotSort(Capture& capture)
{
    QTableWidget* books = capture.table();
    const int titleColumn = columnByHeader(books, T("catalog.col.title"));
    if (titleColumn < 0) {
        throw std::runtime_error("title column is missing");
    }
    QHeaderView* header = books->horizontalHeader();
    // The first cell's tick sits at the inline start of this section. The
    // middle of the section is the title, in either direction.
    const QPoint at(header->sectionViewportPosition(titleColumn) + header->sectionSize(titleColumn) / 2,
                    header->height() / 2);
    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, at);
    capture.settle(400);
    capture.save(QStringLiteral("gs-sort"), topOfTable(capture, books, 8));
}

void shotLicence(Capture& capture)
{
    auto* footer = capture.named<QWidget>(QStringLiteral("appFooter"), &capture.window());
    auto* label = footer == nullptr ? nullptr : footer->findChild<VLMS::ClickableLabel*>();
    if (label == nullptr) {
        throw std::runtime_error("footer label is missing");
    }
    capture.openModal(
        [&]() { capture.click(label); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("gs-licence"), capture.grab(modal));
            if (auto* dialog = qobject_cast<QDialog*>(modal)) {
                dialog->reject();
            } else {
                modal->close();
            }
        });
}

}  // namespace

void registerGeneralShots(ShotRegistry& registry)
{
    registry.emplace(QStringLiteral("gs-window"), shotWindow);
    registry.emplace(QStringLiteral("gs-dark"), shotDark);
    registry.emplace(QStringLiteral("gs-ticks"), shotTicks);
    registry.emplace(QStringLiteral("gs-sort"), shotSort);
    registry.emplace(QStringLiteral("gs-licence"), shotLicence);
}

}  // namespace ManualCapture
