#include "Capture.h"
#include "Shots.h"

#include "Application.h"
#include "QtBridge.h"
#include "ui/MainWindow.h"
#include "ui/ListPageFrame.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/MemberRepository.h>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTableWidget>
#include <QTest>

#include <algorithm>

using VLMS::T;
using VLMS::qs;

namespace ManualCapture {

namespace {

[[noreturn]] void fail(const char* message)
{
    throw std::runtime_error(message);
}

Application& app()
{
    auto* application = qobject_cast<Application*>(qApp);
    if (application == nullptr) {
        fail("no application");
    }
    return *application;
}

VLMS::ListPageFrame* frameOf(Capture& capture)
{
    auto* frame = capture.page()->findChild<VLMS::ListPageFrame*>();
    if (frame == nullptr) {
        fail("list frame is missing");
    }
    return frame;
}

QRect widgetRect(QWidget* widget, QWidget* base)
{
    return QRect(widget->mapTo(base, QPoint(0, 0)), widget->size());
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

QRect headerSection(const QTableWidget* table, int column, QWidget* window)
{
    const QHeaderView* header = table->horizontalHeader();
    const QRect section(header->sectionViewportPosition(column), 0,
                        header->sectionSize(column), header->height());
    return QRect(header->viewport()->mapTo(window, section.topLeft()), section.size());
}

QRect headerByKey(const QTableWidget* table, const char* key, QWidget* window)
{
    const int column = columnByHeader(table, T(key));
    if (column < 0) {
        fail("column is missing");
    }
    return headerSection(table, column, window);
}

void setSearch(Capture& capture, const QString& text)
{
    auto* edit = frameOf(capture)->searchEdit();
    // Arabic input would rewrite ASCII digits, and the search would miss the row.
    edit->setAttribute(Qt::WA_InputMethodEnabled, false);
    edit->setText(text);
    capture.settle(500);
}

void holdRow(QTableWidget* table, int row)
{
    // selectRow() in right-to-left asks the header for the column at the
    // viewport's far edge. That index is -1 until the sections fill the width,
    // so the call selects nothing. Set the current index on column 0 instead.
    const QModelIndex index = table->model()->index(row, 0);
    table->selectionModel()->setCurrentIndex(
        index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void selectFirst(Capture& capture)
{
    QTableWidget* table = capture.table();
    if (table->rowCount() < 1) {
        fail("the table is empty");
    }
    holdRow(table, 0);
    if (table->currentRow() != 0 || table->selectedItems().isEmpty()) {
        fail("could not select a row");
    }
}

bool selectId(QTableWidget* table, qint64 id)
{
    int found = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* item = table->item(row, 0);
        if (item != nullptr && item->data(Qt::UserRole).toLongLong() == id) {
            found = row;
            break;
        }
    }
    if (found < 0) {
        return false;
    }
    holdRow(table, found);
    return table->currentRow() == found && !table->selectedItems().isEmpty();
}

void pickList(Capture& capture, const char* objectName, const QString& text)
{
    auto* list = capture.named<QListWidget>(QLatin1String(objectName));
    if (list == nullptr) {
        fail("filter list is missing");
    }
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->text() == text) {
            list->setCurrentItem(list->item(row));
            capture.settle(500);
            return;
        }
    }
    fail("filter entry is missing");
}

void answer(Capture& capture, QWidget* modal, const char* key)
{
    capture.click(capture.button(key, modal));
}

QPixmap topOfTable(Capture& capture, QTableWidget* table, int rows)
{
    const QRect tableInWindow = widgetRect(table, &capture.window());
    const QRect crop = tableInWindow.adjusted(-12, -12, 12, 12).intersected(capture.window().rect());
    const int rowHeight = table->rowCount() > 0 ? table->rowHeight(0) : 28;
    const int keep = (tableInWindow.top() - crop.top()) + table->horizontalHeader()->height()
        + rows * rowHeight + 12;
    return capture.grab().copy(crop).copy(0, 0, crop.width(), std::min(keep, crop.height()));
}

/// The live library has no archived books. Archive one shelf-only title inside
/// this process so the Archive book and copy lists are not empty. Copies keep
/// their local numbers, which is what Reuse local number needs.
void ensureArchivedBook()
{
    static bool done = false;
    if (done) {
        return;
    }
    BookQuery archived;
    archived.archive = ArchiveScope::Archived;
    archived.limit = 1;
    const auto existing = app().catalog().listBooks(archived);
    if (!existing) {
        fail("could not list archived books");
    }
    if (!existing.value().empty()) {
        done = true;
        return;
    }

    // Copies ascending lists the copy-less titles first. Available descending
    // starts among titles that still have a copy on the shelf.
    BookQuery live;
    live.sortColumn = BookSort::kAvailable;
    live.sortAscending = false;
    live.limit = 40;
    const auto books = app().catalog().listBooks(live);
    if (!books) {
        fail("could not list books to archive");
    }
    for (const BookRecord& book : books.value()) {
        if (book.localIds.empty() || !book.localIdsOnLoan.empty()) {
            continue;
        }
        const auto open = app().catalog().bookHasOpenLoans(book.id);
        if (!open || open.value()) {
            continue;
        }
        if (!app().catalog().archiveBook(book.id)) {
            continue;
        }
        std::fprintf(stdout, "sandbox setup: archived 1 live book so Archive shots have a book and its copies\n");
        done = true;
        return;
    }
    fail("could not archive a book in the sandbox");
}

BookRecord multiCopyOnLoan()
{
    BookQuery query;
    query.sortColumn = BookSort::kCopies;
    query.sortAscending = false;
    query.limit = 80;
    const auto books = app().catalog().listBooks(query);
    if (!books) {
        fail("could not list books");
    }
    for (const BookRecord& book : books.value()) {
        if (book.localIds.size() >= 3 && !book.localIdsOnLoan.empty()) {
            return book;
        }
    }
    fail("no multi-copy title has a copy on loan");
}

QString mixedLoanNumber()
{
    const BookRecord seed = multiCopyOnLoan();
    for (const std::string& number : seed.localIdsOnLoan) {
        BookQuery query;
        query.search = number;
        query.limit = 10;
        const auto hits = app().catalog().listBooks(query);
        if (!hits) {
            continue;
        }
        bool onShelf = false;
        bool onLoan = false;
        for (const BookRecord& book : hits.value()) {
            const bool loaned = std::find(book.localIdsOnLoan.begin(), book.localIdsOnLoan.end(), number)
                != book.localIdsOnLoan.end();
            onLoan = onLoan || loaned;
            onShelf = onShelf || !loaned;
        }
        if (onShelf && onLoan) {
            return qs(number);
        }
    }
    return qs(seed.localIdsOnLoan.front());
}

LoanRecord firstUnreturnedLoan()
{
    for (const char* filter : {LoanFilter::kOpen, LoanFilter::kOverdue}) {
        LoanQuery query;
        query.filters = {filter};
        query.limit = 1;
        const auto loans = app().circulation().listLoans(query);
        if (loans && !loans.value().empty()) {
            return loans.value().front();
        }
    }
    fail("no loan is still out");
}

void showBook(Capture& capture, qint64 bookId, const QString& search)
{
    capture.goTo("nav.catalog");
    setSearch(capture, search);
    if (!selectId(capture.table(), bookId)) {
        fail("the book is not in the catalogue search");
    }
}

void shotCatOverview(Capture& capture)
{
    capture.goTo("nav.catalog");
    frameOf(capture)->searchEdit()->clear();
    capture.settle(400);
    selectFirst(capture);

    QTableWidget* table = capture.table();
    QWidget* window = &capture.window();
    auto* frame = frameOf(capture);
    const QList<QRect> marks = {
        widgetRect(capture.named<QListWidget>(QStringLiteral("categoryFilter")), window),
        widgetRect(capture.named<QListWidget>(QStringLiteral("languageFilter")), window),
        widgetRect(capture.named<QListWidget>(QStringLiteral("coverFilter")), window),
        widgetRect(frame->searchEdit(), window),
        headerByKey(table, "catalog.col.localNumber", window),
        headerByKey(table, "catalog.col.copies", window).united(headerByKey(table, "catalog.col.available", window)),
        widgetRect(frame->previewPanel(), window),
        widgetRect(capture.button("catalog.loans"), window),
        widgetRect(capture.button("catalog.addBook"), window),
        widgetRect(capture.button("catalog.edit"), window),
        widgetRect(capture.button("catalog.delete"), window),
    };
    capture.save(QStringLiteral("cat-overview"), capture.calloutsAt(capture.grab(), marks));
}

void shotCatSearchNumber(Capture& capture)
{
    const BookRecord book = multiCopyOnLoan();
    showBook(capture, book.id, qs(book.localIdsOnLoan.front()));
    capture.save(QStringLiteral("cat-search-number"), topOfTable(capture, capture.table(), 6));
}

void shotCatNumberDropdown(Capture& capture)
{
    const BookRecord book = multiCopyOnLoan();
    showBook(capture, book.id, qs(book.localIdsOnLoan.front()));
    QTableWidget* table = capture.table();
    const int column = columnByHeader(table, T("catalog.col.localNumber"));
    const int row = table->currentRow();
    if (column < 0 || row < 0) {
        fail("local-number cell is missing");
    }
    const QRect cell = table->visualRect(table->model()->index(row, column));
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, cell.center());
    capture.settle(200);
    auto* combo = table->findChild<QComboBox*>();
    if (combo == nullptr) {
        fail("the local-number drop-down did not open");
    }
    combo->showPopup();
    capture.settle(300);
    capture.save(QStringLiteral("cat-number-dropdown"), capture.grabRegion({table}));
    combo->hidePopup();
}

void shotCatCopyColours(Capture& capture)
{
    capture.goTo("nav.catalog");
    setSearch(capture, mixedLoanNumber());
    selectFirst(capture);
    capture.save(QStringLiteral("cat-copy-colours"), topOfTable(capture, capture.table(), 6));
}

void shotCatLoans(Capture& capture)
{
    const BookRecord book = multiCopyOnLoan();
    showBook(capture, book.id, qs(book.localIds.front()));
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.loans")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("cat-loans-dialog"), capture.grab(modal));
            answer(capture, modal, "common.close");
        });
}

void shotMemOverview(Capture& capture)
{
    capture.goTo("nav.members");
    frameOf(capture)->searchEdit()->clear();
    capture.settle(400);
    selectFirst(capture);

    QTableWidget* table = capture.table();
    QWidget* window = &capture.window();
    auto* frame = frameOf(capture);
    const QList<QRect> marks = {
        widgetRect(capture.named<QListWidget>(QStringLiteral("statusFilter")), window),
        widgetRect(capture.named<QListWidget>(QStringLiteral("sexFilter")), window),
        widgetRect(capture.named<QListWidget>(QStringLiteral("yearFilter")), window),
        widgetRect(capture.named<QListWidget>(QStringLiteral("ageGroupFilter")), window),
        widgetRect(capture.named<QListWidget>(QStringLiteral("cityFilter")), window),
        widgetRect(frame->searchEdit(), window),
        headerByKey(table, "members.col.loans", window),
        widgetRect(frame->previewPanel(), window),
        widgetRect(capture.button("members.loans"), window),
        widgetRect(capture.button("members.addMember"), window),
        widgetRect(capture.button("members.edit"), window),
        widgetRect(capture.button("members.delete"), window),
    };
    capture.save(QStringLiteral("mem-overview"), capture.calloutsAt(capture.grab(), marks));
}

void showMember(Capture& capture, const LoanRecord& loan)
{
    capture.goTo("nav.members");
    setSearch(capture, qs(loan.membershipNumber));
    if (!selectId(capture.table(), loan.memberId)) {
        selectFirst(capture);
    }
}

void shotMemDetails(Capture& capture)
{
    showMember(capture, firstUnreturnedLoan());
    auto* panel = frameOf(capture)->previewPanel();
    // The caption promises status and the last active day, which sit below the fold at 900px.
    // Put the name at the top: in Arabic the panel runs past the window's bottom edge, so
    // ensureWidgetVisible() thinks the row is already on screen.
    auto* scroll = capture.named<QScrollArea>(QStringLiteral("memberDetailsScroll"));
    const QString name = T("members.col.name") + QStringLiteral(":");
    QLabel* nameLabel = nullptr;
    for (QLabel* label : scroll->widget()->findChildren<QLabel*>()) {
        if (label->text() == name) {
            nameLabel = label;
        }
    }
    if (nameLabel == nullptr) {
        fail("the name is not in the member details");
    }
    scroll->verticalScrollBar()->setValue(nameLabel->mapTo(scroll->widget(), QPoint(0, 0)).y() - 4);
    capture.settle(200);
    capture.save(QStringLiteral("mem-details"), capture.grabRegion({panel}));
    scroll->verticalScrollBar()->setValue(0);
}

void shotMemDeleteBlocked(Capture& capture)
{
    showMember(capture, firstUnreturnedLoan());
    capture.openModal(
        [&]() { capture.click(capture.button("members.delete")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("mem-delete-blocked"), capture.grab(modal));
            answer(capture, modal, "common.ok");
        });
}

void shotMemLoans(Capture& capture)
{
    showMember(capture, firstUnreturnedLoan());
    capture.openModal(
        [&]() { capture.click(capture.button("members.loans")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("mem-loans-dialog"), capture.grab(modal));
            answer(capture, modal, "common.close");
        });
}

void shotCircOverview(Capture& capture)
{
    capture.goTo("nav.circulation");
    pickList(capture, "loanFilter", T("circulation.filter.all"));
    frameOf(capture)->searchEdit()->clear();
    capture.settle(400);
    selectFirst(capture);

    QWidget* window = &capture.window();
    auto* frame = frameOf(capture);
    const QList<QRect> marks = {
        widgetRect(capture.named<QListWidget>(QStringLiteral("loanFilter")), window),
        widgetRect(capture.named<QWidget>(QStringLiteral("memberFilters")), window),
        widgetRect(frame->searchEdit(), window),
        widgetRect(frame->table(), window),
        widgetRect(frame->imageLabel(), window),
        widgetRect(frame->secondImageLabel(), window),
        widgetRect(capture.button("circulation.return"), window),
        widgetRect(capture.button("circulation.checkout"), window),
        widgetRect(capture.button("circulation.extend"), window),
        widgetRect(capture.button("circulation.delete"), window),
    };
    capture.save(QStringLiteral("circ-overview"), capture.calloutsAt(capture.grab(), marks));
}

void shotCircOverdue(Capture& capture)
{
    capture.goTo("nav.circulation");
    frameOf(capture)->searchEdit()->clear();
    pickList(capture, "loanFilter", T("circulation.filter.overdue"));
    if (capture.table()->rowCount() > 0) {
        selectFirst(capture);
    }
    capture.save(QStringLiteral("circ-overdue"), capture.grab());
}

void shotCircSearchName(Capture& capture)
{
    const LoanRecord loan = firstUnreturnedLoan();
    capture.goTo("nav.circulation");
    pickList(capture, "loanFilter", T("circulation.filter.all"));
    setSearch(capture, qs(loan.memberName));
    if (capture.table()->rowCount() > 0) {
        selectFirst(capture);
    }
    capture.save(QStringLiteral("circ-search-name"), capture.grab());
}

void showArchive(Capture& capture, const char* typeKey)
{
    ensureArchivedBook();
    capture.goTo("nav.archive");
    pickList(capture, "archiveType", T(typeKey));
    if (capture.table()->rowCount() > 0) {
        selectFirst(capture);
    }
}

void shotArcBooks(Capture& capture)
{
    showArchive(capture, "archive.type.books");
    QWidget* window = &capture.window();
    auto* frame = frameOf(capture);
    const QList<QRect> marks = {
        widgetRect(capture.named<QListWidget>(QStringLiteral("archiveType")), window),
        widgetRect(capture.named<QWidget>(QStringLiteral("bookFilters")), window),
        widgetRect(frame->searchEdit(), window),
        headerByKey(capture.table(), "archive.col.copies", window),
        widgetRect(frame->imageLabel(), window),
        widgetRect(capture.button("archive.restore"), window),
        widgetRect(capture.button("archive.purge"), window),
    };
    capture.save(QStringLiteral("arc-books"), capture.calloutsAt(capture.grab(), marks));
}

void shotArcCopies(Capture& capture)
{
    showArchive(capture, "archive.type.copies");
    QWidget* window = &capture.window();
    const QList<QRect> marks = {
        widgetRect(capture.named<QWidget>(QStringLiteral("bookFilters")), window),
        widgetRect(frameOf(capture)->imageLabel(), window),
        headerByKey(capture.table(), "archive.col.loans", window),
        widgetRect(capture.button("archive.reuse"), window),
    };
    capture.save(QStringLiteral("arc-copies"), capture.calloutsAt(capture.grab(), marks));
}

void shotArcLoans(Capture& capture)
{
    showArchive(capture, "archive.type.loans");
    QWidget* window = &capture.window();
    auto* frame = frameOf(capture);
    const QList<QRect> marks = {
        widgetRect(capture.named<QWidget>(QStringLiteral("memberFilters")), window),
        widgetRect(frame->imageLabel(), window),
        widgetRect(frame->secondImageLabel(), window),
    };
    capture.save(QStringLiteral("arc-loans"), capture.calloutsAt(capture.grab(), marks));
}

void shotArcMembers(Capture& capture)
{
    showArchive(capture, "archive.type.members");
    QWidget* window = &capture.window();
    const QList<QRect> marks = {
        widgetRect(capture.named<QWidget>(QStringLiteral("memberFilters")), window),
        widgetRect(frameOf(capture)->imageLabel(), window),
        headerByKey(capture.table(), "archive.col.loans", window),
        widgetRect(capture.button("archive.loans"), window),
    };
    capture.save(QStringLiteral("arc-members"), capture.calloutsAt(capture.grab(), marks));
}

void shotArcRestore(Capture& capture)
{
    showArchive(capture, "archive.type.books");
    capture.openModal(
        [&]() { capture.click(capture.button("archive.restore")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("arc-restore-confirm"), capture.grab(modal));
            answer(capture, modal, "common.no");
        });
}

void shotArcPurge(Capture& capture)
{
    showArchive(capture, "archive.type.loans");
    capture.openModal(
        [&]() { capture.click(capture.button("archive.purge")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("arc-purge-confirm"), capture.grab(modal));
            answer(capture, modal, "common.no");
        });
}

void shotMetOverview(Capture& capture)
{
    capture.goTo("nav.metrics");
    capture.settle(400);
    QList<QRect> marks;
    const auto headings = capture.page()->findChildren<QLabel*>(QStringLiteral("sectionTitle"));
    for (QLabel* heading : headings) {
        const QRect rect = widgetRect(heading, &capture.window());
        if (heading->isVisible() && capture.window().rect().intersects(rect)) {
            marks.append(rect);
        }
    }
    marks.append(widgetRect(capture.button("metrics.refresh"), &capture.window()));
    capture.save(QStringLiteral("met-overview"), capture.calloutsAt(capture.grab(), marks));
}

void shotMetFull(Capture& capture)
{
    capture.goTo("nav.metrics");
    capture.settle(400);
    auto* scroll = capture.named<QScrollArea>(QStringLiteral("metricsScroll"));
    if (scroll == nullptr || scroll->widget() == nullptr) {
        fail("metrics viewer is missing");
    }
    scroll->widget()->adjustSize();
    capture.save(QStringLiteral("met-full"), scroll->widget()->grab());
}

}  // namespace

void registerScreenShots(ShotRegistry& registry)
{
    registry.emplace(QStringLiteral("cat-overview"), shotCatOverview);
    registry.emplace(QStringLiteral("cat-search-number"), shotCatSearchNumber);
    registry.emplace(QStringLiteral("cat-number-dropdown"), shotCatNumberDropdown);
    registry.emplace(QStringLiteral("cat-copy-colours"), shotCatCopyColours);
    registry.emplace(QStringLiteral("cat-loans-dialog"), shotCatLoans);
    registry.emplace(QStringLiteral("mem-overview"), shotMemOverview);
    registry.emplace(QStringLiteral("mem-details"), shotMemDetails);
    registry.emplace(QStringLiteral("mem-delete-blocked"), shotMemDeleteBlocked);
    registry.emplace(QStringLiteral("mem-loans-dialog"), shotMemLoans);
    registry.emplace(QStringLiteral("circ-overview"), shotCircOverview);
    registry.emplace(QStringLiteral("circ-overdue"), shotCircOverdue);
    registry.emplace(QStringLiteral("circ-search-name"), shotCircSearchName);
    registry.emplace(QStringLiteral("arc-books"), shotArcBooks);
    registry.emplace(QStringLiteral("arc-copies"), shotArcCopies);
    registry.emplace(QStringLiteral("arc-loans"), shotArcLoans);
    registry.emplace(QStringLiteral("arc-members"), shotArcMembers);
    registry.emplace(QStringLiteral("arc-restore-confirm"), shotArcRestore);
    registry.emplace(QStringLiteral("arc-purge-confirm"), shotArcPurge);
    registry.emplace(QStringLiteral("met-overview"), shotMetOverview);
    registry.emplace(QStringLiteral("met-full"), shotMetFull);
}

}  // namespace ManualCapture
