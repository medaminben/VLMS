#include "Capture.h"
#include "Shots.h"

#include "ui/shell/Application.h"
#include "QtBridge.h"
#include "ui/shell/MainWindow.h"
#include "ui/views/ClickableLabel.h"
#include "ui/shell/LanguageSelector.h"
#include "ui/views/ListPagePager.h"

#include <QAbstractButton>
#include <QAbstractItemModel>
#include <QApplication>
#include <QDialog>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableView>
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

QRect checkIndicatorInWindow(const QTableView* table, int row, QWidget* window)
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

QPixmap topOfTable(Capture& capture, QTableView* table, int rows)
{
    const QRect tableInWindow = windowRect(table, &capture.window());
    QRect crop = tableInWindow.adjusted(-12, -12, 12, 12).intersected(capture.window().rect());
    QPixmap image = capture.grab(&capture.window()).copy(crop);
    const int rowHeight = table->model()->rowCount() > 0 ? table->rowHeight(0) : 28;
    const int keep = (tableInWindow.top() - crop.top()) + table->horizontalHeader()->height()
        + rows * rowHeight + 12;
    return image.copy(0, 0, image.width(), std::min(keep, image.height()));
}

QRect intoCrop(const QRect& inWindow, const QRect& crop)
{
    return inWindow.translated(-crop.topLeft());
}

QRect tableCrop(QTableView* table, QWidget* window)
{
    return windowRect(table, window).adjusted(-12, -12, 12, 12).intersected(window->rect());
}

int columnByHeader(const QTableView* table, const QString& label)
{
    const QAbstractItemModel* model = table->model();
    for (int column = 0; column < model->columnCount(); ++column) {
        if (model->headerData(column, Qt::Horizontal).toString() == label) {
            return column;
        }
    }
    return -1;
}

void shotWindow(Capture& capture)
{
    QTableView* books = capture.table();
    if (books->model() != nullptr && books->model()->rowCount() > 0) {
        const QModelIndex index = books->model()->index(0, 0);
        books->selectionModel()->setCurrentIndex(
            index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        capture.settle();
    }

    auto* search = capture.named<QLineEdit>(QStringLiteral("listSearch"));
    auto* filterColumn = capture.named<QWidget>(QStringLiteral("filterColumn"));
    auto* listTable = capture.named<QTableView>(QStringLiteral("listTable"));
    auto* pager = capture.page()->findChild<VLMS::ListPagePager*>();
    auto* details = capture.named<QWidget>(QStringLiteral("listDetailsScroll"));
    auto* buttonPad = capture.named<QWidget>(QStringLiteral("buttonPad"));
    if (search == nullptr || filterColumn == nullptr || listTable == nullptr
        || pager == nullptr || details == nullptr || buttonPad == nullptr) {
        throw std::runtime_error("catalogue chrome is missing");
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
        filterColumn,
        static_cast<QWidget*>(search),
        static_cast<QWidget*>(listTable),
        static_cast<QWidget*>(pager),
        details,
        buttonPad,
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
    QTableView* books = capture.table();
    auto* model = books->model();
    if (model == nullptr || model->rowCount() < 3) {
        throw std::runtime_error("catalogue has fewer than three rows");
    }
    for (int row = 0; row < 3; ++row) {
        if (!model->setData(model->index(row, 0), Qt::Checked, Qt::CheckStateRole)) {
            throw std::runtime_error("could not tick a catalogue row");
        }
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
    QTableView* books = capture.table();
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
