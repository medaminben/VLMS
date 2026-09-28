#include "Capture.h"
#include "Shots.h"

#include "Application.h"
#include "QtBridge.h"
#include "ui/MainWindow.h"
#include "ui/ListPageFrame.h"
#include "ui/catalog/LocalNumberDelegate.h"
#include "ui/members/BirthDateEdit.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cstdio>

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

void setSearch(Capture& capture, const QString& text)
{
    auto* edit = frameOf(capture)->searchEdit();
    edit->setAttribute(Qt::WA_InputMethodEnabled, false);
    edit->setText(text);
    capture.settle(500);
}

void holdRow(QTableWidget* table, int row)
{
    const QModelIndex index = table->model()->index(row, 0);
    table->selectionModel()->setCurrentIndex(
        index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

bool selectId(QTableWidget* table, qint64 id)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* item = table->item(row, 0);
        if (item != nullptr && item->data(Qt::UserRole).toLongLong() == id) {
            holdRow(table, row);
            return table->currentRow() == row && !table->selectedItems().isEmpty();
        }
    }
    return false;
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

void closeModal(QWidget* modal)
{
    if (auto* dialog = qobject_cast<QDialog*>(modal)) {
        dialog->reject();
    } else {
        modal->close();
    }
}

void answer(Capture& capture, QWidget* modal, const char* key)
{
    capture.click(capture.button(key, modal));
}

void hidePopups()
{
    const auto tops = QApplication::topLevelWidgets();
    for (QWidget* widget : tops) {
        // Qt::Popup includes the Window bit, so a mask test would hide every
        // dialog. Only a real popup (the author completer) should close.
        if (widget != nullptr && widget->isVisible() && widget->windowType() == Qt::Popup) {
            widget->hide();
        }
    }
}

QWidget* fieldFor(QWidget* dialog, const char* key)
{
    const QString wanted = T(key);
    for (QFormLayout* form : dialog->findChildren<QFormLayout*>()) {
        for (int row = 0; row < form->rowCount(); ++row) {
            auto* labelItem = form->itemAt(row, QFormLayout::LabelRole);
            auto* label = labelItem == nullptr ? nullptr : qobject_cast<QLabel*>(labelItem->widget());
            if (label == nullptr || label->text() != wanted) {
                continue;
            }
            auto* fieldItem = form->itemAt(row, QFormLayout::FieldRole);
            if (fieldItem != nullptr && fieldItem->widget() != nullptr) {
                return fieldItem->widget();
            }
        }
    }
    for (QLabel* label : dialog->findChildren<QLabel*>()) {
        if (label->text() != wanted) {
            continue;
        }
        for (QGridLayout* grid : dialog->findChildren<QGridLayout*>()) {
            const int index = grid->indexOf(label);
            if (index < 0) {
                continue;
            }
            int row = 0;
            int column = 0;
            int rowSpan = 0;
            int columnSpan = 0;
            grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
            if (QLayoutItem* field = grid->itemAtPosition(row, column + columnSpan)) {
                if (field->widget() != nullptr) {
                    return field->widget();
                }
            }
        }
    }
    throw std::runtime_error(std::string("form field is missing: ") + key);
}

void fillLine(Capture& capture, QWidget* dialog, const char* key, const QString& text)
{
    auto* line = qobject_cast<QLineEdit*>(fieldFor(dialog, key));
    if (line == nullptr) {
        fail("form field is not a line edit");
    }
    // QTest::keyClicks asserts on a non-ASCII character in this Qt build, so
    // Arabic example text is written with setText. ASCII still goes through typeInto.
    const bool ascii = std::all_of(text.cbegin(), text.cend(), [](QChar character) {
        return character.unicode() < 128;
    });
    if (ascii) {
        capture.typeInto(line, text);
    }
    if (line->text() != text) {
        line->setText(text);
    }
}

void showBook(Capture& capture, qint64 bookId, const QString& search)
{
    capture.goTo("nav.catalog");
    setSearch(capture, search);
    if (!selectId(capture.table(), bookId)) {
        fail("the book is not in the catalogue search");
    }
}

VLMS::Repositories::BookRecord firstBook(bool fewestFirst, const auto& accept)
{
    VLMS::Repositories::BookQuery query;
    query.sortColumn = VLMS::Repositories::BookSort::kCopies;
    query.sortAscending = fewestFirst;
    query.limit = 200;
    const auto books = app().catalog().listBooks(query);
    if (!books) {
        fail("could not list books");
    }
    for (const VLMS::Repositories::BookRecord& book : books.value()) {
        if (accept(book)) {
            return book;
        }
    }
    fail("no book matches the shot");
}

VLMS::Repositories::BookRecord multiCopyOnLoan()
{
    return firstBook(false, [](const VLMS::Repositories::BookRecord& book) {
        return book.localIds.size() >= 3 && !book.localIdsOnLoan.empty();
    });
}

VLMS::Repositories::BookRecord shelfCopies(int minimum)
{
    VLMS::Repositories::BookQuery query;
    query.sortColumn = VLMS::Repositories::BookSort::kAvailable;
    query.sortAscending = false;
    query.limit = 80;
    const auto books = app().catalog().listBooks(query);
    if (!books) {
        fail("could not list books");
    }
    for (const VLMS::Repositories::BookRecord& book : books.value()) {
        if (static_cast<int>(book.localIds.size()) >= minimum && book.localIdsOnLoan.empty()) {
            return book;
        }
    }
    fail("no shelf title matches the shot");
}

VLMS::Repositories::BookRecord neverBorrowed()
{
    for (int offset = 0; offset < 240; offset += 40) {
        VLMS::Repositories::BookQuery query;
        query.sortColumn = VLMS::Repositories::BookSort::kTitle;
        query.sortAscending = true;
        query.limit = 40;
        query.offset = offset;
        const auto books = app().catalog().listBooks(query);
        if (!books || books.value().empty()) {
            break;
        }
        for (const VLMS::Repositories::BookRecord& book : books.value()) {
            if (book.localIds.empty()) {
                continue;
            }
            VLMS::Repositories::LoanQuery loans;
            loans.bookId = book.id;
            loans.archive = VLMS::Repositories::ArchiveScope::Any;
            loans.limit = 1;
            const auto count = app().circulation().countLoans(loans);
            if (count && count.value() == 0) {
                return book;
            }
        }
    }
    fail("no never-borrowed title");
}

VLMS::Repositories::MemberRecord nonActiveMember()
{
    VLMS::Repositories::MemberQuery query;
    query.statuses = {VLMS::Repositories::MemberStatus::kNonActive};
    query.limit = 1;
    const auto members = app().members().listMembers(query);
    if (!members || members.value().empty()) {
        fail("no non-active member");
    }
    return members.value().front();
}

void ensureArchivedBook()
{
    static bool done = false;
    if (done) {
        return;
    }
    VLMS::Repositories::BookQuery archived;
    archived.archive = VLMS::Repositories::ArchiveScope::Archived;
    archived.limit = 1;
    const auto existing = app().catalog().listBooks(archived);
    if (!existing) {
        fail("could not list archived books");
    }
    if (!existing.value().empty()) {
        done = true;
        return;
    }
    const VLMS::Repositories::BookRecord book = shelfCopies(1);
    if (!app().catalog().archiveBook(book.id)) {
        fail("could not archive a book in the sandbox");
    }
    std::fprintf(stdout, "sandbox setup: archived 1 live book so reuse shots have a copy\n");
    done = true;
}

void showTab(QWidget* dialog, const char* key)
{
    auto* tabs = dialog->findChild<QTabWidget*>();
    if (tabs == nullptr) {
        fail("tabs are missing");
    }
    const QString label = T(key);
    for (int index = 0; index < tabs->count(); ++index) {
        if (tabs->tabText(index) == label) {
            tabs->setCurrentIndex(index);
            return;
        }
    }
    fail("tab is missing");
}

const char* unreturnedFilterKey()
{
    VLMS::Repositories::LoanQuery query;
    query.filters = {VLMS::Repositories::LoanFilter::kOpen};
    query.limit = 1;
    const auto open = app().circulation().listLoans(query);
    if (open && !open.value().empty()) {
        return "circulation.filter.open";
    }
    return "circulation.filter.overdue";
}

void showUnreturnedLoan(Capture& capture)
{
    capture.goTo("nav.circulation");
    auto* search = frameOf(capture)->searchEdit();
    search->setAttribute(Qt::WA_InputMethodEnabled, false);
    search->clear();
    pickList(capture, "loanFilter", T(unreturnedFilterKey()));
    if (capture.table()->rowCount() < 1) {
        fail("no unreturned loan is on screen");
    }
    holdRow(capture.table(), 0);
}

void selectUnreturnedRow(QTableWidget* table)
{
    const QString open = T("circulation.status.open");
    const QString overdue = T("circulation.status.overdue");
    for (int row = 0; row < table->rowCount(); ++row) {
        for (int column = 0; column < table->columnCount(); ++column) {
            const QTableWidgetItem* item = table->item(row, column);
            if (item != nullptr && (item->text() == open || item->text() == overdue)) {
                holdRow(table, row);
                return;
            }
        }
    }
    fail("the history has no loan still out");
}

void shotAddBookOpen(Capture& capture)
{
    capture.goTo("nav.catalog");
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.addBook")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("add-book-open"), capture.grab(modal));
            closeModal(modal);
        });
}

void fillExampleBook(Capture& capture, QWidget* dialog)
{
    fillLine(capture, dialog, "book.field.title", QStringLiteral("الأيام"));
    fillLine(capture, dialog, "book.field.author", QStringLiteral("طه حسين"));
    auto* language = dialog->findChild<QComboBox*>(QStringLiteral("bookLanguageCombo"));
    if (language == nullptr) {
        fail("language combo is missing");
    }
    const int arabic = language->findData(QStringLiteral("ar"));
    if (arabic >= 0) {
        language->setCurrentIndex(arabic);
    }
    if (auto* category = qobject_cast<QWidget*>(fieldFor(dialog, "book.field.category"))) {
        if (auto* combo = category->findChild<QComboBox*>()) {
            if (combo->count() > 1) {
                combo->setCurrentIndex(1);
            }
        }
    }
    hidePopups();
}

void shotAddBookFilled(Capture& capture)
{
    capture.goTo("nav.catalog");
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.addBook")); },
        [&](QWidget* modal) {
            fillExampleBook(capture, modal);
            const QList<QWidget*> marks = {
                capture.button("catalog.categories", modal),
                capture.button("ocr.readFromImage", modal),
            };
            capture.save(QStringLiteral("add-book-filled"),
                         capture.callouts(capture.grab(modal), modal, marks));
            closeModal(modal);
        });
}

void shotAddBookCopies(Capture& capture)
{
    capture.goTo("nav.catalog");
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.addBook")); },
        [&](QWidget* modal) {
            showTab(modal, "book.tab.copies");
            capture.click(capture.button("book.copy.add", modal));
            capture.save(QStringLiteral("add-book-copies"), capture.grab(modal));
            closeModal(modal);
        });
}

void shotAddBookFreeNumber(Capture& capture)
{
    capture.goTo("nav.catalog");
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.addBook")); },
        [&](QWidget* modal) {
            showTab(modal, "book.tab.copies");
            capture.click(capture.button("book.copy.add", modal));
            auto* table = modal->findChild<QTableWidget*>(QStringLiteral("copiesTable"));
            if (table == nullptr || table->rowCount() < 1) {
                fail("the new copy row is missing");
            }
            const int row = table->rowCount() - 1;
            holdRow(table, row);
            table->edit(table->model()->index(row, 0));
            capture.settle(200);
            auto* combo = table->findChild<QComboBox*>();
            if (combo == nullptr) {
                fail("the free-number editor did not open");
            }
            combo->showPopup();
            capture.settle(300);
            capture.save(QStringLiteral("add-book-free-number"), capture.grab(modal));
            combo->hidePopup();
            closeModal(modal);
        });
}

void shotAddBookCategories(Capture& capture)
{
    capture.goTo("nav.catalog");
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.addBook")); },
        [&](QWidget* book) {
            capture.openModal(
                [&]() { capture.click(capture.button("catalog.categories", book)); },
                [&](QWidget* categories) {
                    capture.save(QStringLiteral("add-book-categories"), capture.grab(categories));
                    closeModal(categories);
                });
            closeModal(book);
        });
}

void shotRegOpen(Capture& capture)
{
    capture.goTo("nav.members");
    capture.openModal(
        [&]() { capture.click(capture.button("members.addMember")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("reg-open"), capture.grab(modal));
            closeModal(modal);
        });
}

void shotRegFilled(Capture& capture)
{
    capture.goTo("nav.members");
    capture.openModal(
        [&]() { capture.click(capture.button("members.addMember")); },
        [&](QWidget* modal) {
            fillLine(capture, modal, "member.field.firstName", QStringLiteral("أحمد"));
            fillLine(capture, modal, "member.field.lastName", QStringLiteral("بن علي الورداني"));
            auto* birth = qobject_cast<BirthDateEdit*>(fieldFor(modal, "member.field.dateOfBirth"));
            if (birth == nullptr) {
                fail("date of birth is not the three birth-date boxes");
            }
            birth->setIsoDate(QStringLiteral("1990-04-12"));
            fillLine(capture, modal, "member.field.city", QStringLiteral("قصور الساف"));
            if (auto* sex = qobject_cast<QComboBox*>(fieldFor(modal, "member.field.sex"))) {
                const int female = sex->findData(QStringLiteral("female"));
                if (female >= 0) {
                    sex->setCurrentIndex(female);
                }
            }
            const QList<QWidget*> marks = {
                modal->findChild<QWidget*>(QStringLiteral("memberNumberValue")),
                modal->findChild<QWidget*>(QStringLiteral("memberStatusCombo")),
                modal->findChild<QWidget*>(QStringLiteral("memberActiveUntilValue")),
            };
            capture.save(QStringLiteral("reg-filled"), capture.callouts(capture.grab(modal), modal, marks));
            closeModal(modal);
        });
}

void shotRegRenew(Capture& capture)
{
    const VLMS::Repositories::MemberRecord member = nonActiveMember();
    capture.goTo("nav.members");
    setSearch(capture, qs(member.membershipNumber));
    if (!selectId(capture.table(), member.id)) {
        fail("the member is not in the search");
    }
    capture.openModal(
        [&]() { capture.click(capture.button("members.edit")); },
        [&](QWidget* modal) {
            auto* status = modal->findChild<QComboBox*>(QStringLiteral("memberStatusCombo"));
            if (status == nullptr) {
                fail("status combo is missing");
            }
            status->showPopup();
            capture.settle(300);
            capture.save(QStringLiteral("reg-renew"), capture.grab(modal));
            status->hidePopup();
            closeModal(modal);
        });
}

void shotRegRenewed(Capture& capture)
{
    const VLMS::Repositories::MemberRecord member = nonActiveMember();
    capture.goTo("nav.members");
    setSearch(capture, qs(member.membershipNumber));
    if (!selectId(capture.table(), member.id)) {
        fail("the member is not in the search");
    }
    capture.openModal(
        [&]() { capture.click(capture.button("members.edit")); },
        [&](QWidget* modal) {
            auto* status = modal->findChild<QComboBox*>(QStringLiteral("memberStatusCombo"));
            if (status == nullptr) {
                fail("status combo is missing");
            }
            const int active = status->findData(QStringLiteral("active"));
            if (active < 0) {
                fail("active status is missing");
            }
            status->setCurrentIndex(active);
            capture.settle(200);
            capture.save(QStringLiteral("reg-renewed"), capture.grab(modal));
            closeModal(modal);
        });
}

void shotLendDialog(Capture& capture)
{
    capture.goTo("nav.circulation");
    capture.openModal(
        [&]() { capture.click(capture.button("circulation.checkout")); },
        [&](QWidget* modal) {
            const auto combos = modal->findChildren<QComboBox*>();
            for (QComboBox* combo : combos) {
                combo->setCurrentIndex(-1);
            }
            capture.save(QStringLiteral("lend-dialog"), capture.grab(modal));
            closeModal(modal);
        });
}

void shotLendFilled(Capture& capture)
{
    capture.goTo("nav.circulation");
    capture.openModal(
        [&]() { capture.click(capture.button("circulation.checkout")); },
        [&](QWidget* modal) {
            const auto combos = modal->findChildren<QComboBox*>();
            if (combos.size() < 2) {
                fail("checkout combos are missing");
            }
            combos.at(0)->setCurrentIndex(0);
            combos.at(1)->setCurrentIndex(0);
            if (combos.at(0)->currentData().toLongLong() <= 0
                || combos.at(1)->currentData().toLongLong() <= 0) {
                fail("no borrowable member or available copy");
            }
            capture.save(QStringLiteral("lend-filled"), capture.grab(modal));
            closeModal(modal);
        });
}

void shotLendFromBook(Capture& capture)
{
    const VLMS::Repositories::BookRecord book = multiCopyOnLoan();
    showBook(capture, book.id, qs(book.localIds.front()));
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.loans")); },
        [&](QWidget* history) {
            capture.openModal(
                [&]() { capture.click(capture.button("circulation.checkout", history)); },
                [&](QWidget* checkout) {
                    capture.save(QStringLiteral("lend-from-book"), capture.grab(checkout));
                    closeModal(checkout);
                });
            closeModal(history);
        });
}

void shotLendFromMember(Capture& capture)
{
    VLMS::Repositories::LoanQuery query;
    query.filters = {VLMS::Repositories::LoanFilter::kOpen};
    query.limit = 1;
    const auto loans = app().circulation().listLoans(query);
    if (!loans || loans.value().empty()) {
        query.filters = {VLMS::Repositories::LoanFilter::kOverdue};
    }
    const auto chosen = app().circulation().listLoans(query);
    if (!chosen || chosen.value().empty()) {
        fail("no loan is still out");
    }
    const VLMS::Repositories::LoanRecord loan = chosen.value().front();
    capture.goTo("nav.members");
    setSearch(capture, qs(loan.membershipNumber));
    if (!selectId(capture.table(), loan.memberId)) {
        fail("the member is not in the search");
    }
    capture.openModal(
        [&]() { capture.click(capture.button("members.loans")); },
        [&](QWidget* history) {
            capture.openModal(
                [&]() { capture.click(capture.button("circulation.checkout", history)); },
                [&](QWidget* checkout) {
                    capture.save(QStringLiteral("lend-from-member"), capture.grab(checkout));
                    closeModal(checkout);
                });
            closeModal(history);
        });
}

void shotLendEmptyHistory(Capture& capture)
{
    const VLMS::Repositories::BookRecord book = neverBorrowed();
    showBook(capture, book.id, qs(book.localIds.front()));
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.loans")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("lend-empty-history"), capture.grab(modal));
            answer(capture, modal, "common.close");
        });
}

void shotRetDialog(Capture& capture)
{
    showUnreturnedLoan(capture);
    capture.openModal(
        [&]() { capture.click(capture.button("circulation.return")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("ret-dialog"), capture.grab(modal));
            closeModal(modal);
        });
}

void shotExtDialog(Capture& capture)
{
    showUnreturnedLoan(capture);
    capture.openModal(
        [&]() { capture.click(capture.button("circulation.extend")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("ext-dialog"), capture.grab(modal));
            closeModal(modal);
        });
}

void shotRetFromHistory(Capture& capture)
{
    const VLMS::Repositories::BookRecord book = multiCopyOnLoan();
    showBook(capture, book.id, qs(book.localIdsOnLoan.front()));
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.loans")); },
        [&](QWidget* modal) {
            selectUnreturnedRow(modal->findChild<QTableWidget*>());
            capture.settle(200);
            capture.save(QStringLiteral("ret-from-history"), capture.grab(modal));
            closeModal(modal);
        });
}

void shotRmBookConfirm(Capture& capture)
{
    const VLMS::Repositories::BookRecord book = shelfCopies(1);
    showBook(capture, book.id, qs(book.localIds.front()));
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.delete")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("rm-book-confirm"), capture.grab(modal));
            answer(capture, modal, "common.no");
        });
}

void shotRmBookRefused(Capture& capture)
{
    const VLMS::Repositories::BookRecord book = multiCopyOnLoan();
    showBook(capture, book.id, qs(book.localIdsOnLoan.front()));
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.delete")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("rm-book-refused"), capture.grab(modal));
            answer(capture, modal, "common.ok");
        });
}

void shotRmCopyRow(Capture& capture)
{
    const VLMS::Repositories::BookRecord book = shelfCopies(2);
    showBook(capture, book.id, qs(book.localIds.front()));
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.edit")); },
        [&](QWidget* modal) {
            showTab(modal, "book.tab.copies");
            auto* table = modal->findChild<QTableWidget*>(QStringLiteral("copiesTable"));
            if (table == nullptr || table->rowCount() < 1) {
                fail("the copies table is empty");
            }
            holdRow(table, 0);
            capture.settle(150);
            const QList<QWidget*> marks = {capture.button("book.copy.remove", modal)};
            capture.save(QStringLiteral("rm-copy-row"),
                         capture.callouts(capture.grab(modal), modal, marks));
            closeModal(modal);
        });
}

void shotRmLoanConfirm(Capture& capture)
{
    capture.goTo("nav.circulation");
    auto* search = frameOf(capture)->searchEdit();
    search->setAttribute(Qt::WA_InputMethodEnabled, false);
    search->clear();
    pickList(capture, "loanFilter", T("circulation.filter.returned"));
    if (capture.table()->rowCount() < 1) {
        fail("no returned loan is on screen");
    }
    holdRow(capture.table(), 0);
    capture.openModal(
        [&]() { capture.click(capture.button("circulation.delete")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("rm-loan-confirm"), capture.grab(modal));
            answer(capture, modal, "common.no");
        });
}

void shotRmBulkConfirm(Capture& capture)
{
    capture.goTo("nav.catalog");
    frameOf(capture)->searchEdit()->clear();
    capture.settle(400);
    QTableWidget* table = capture.table();
    const int localColumn = [&]() {
        for (int column = 0; column < table->columnCount(); ++column) {
            const QTableWidgetItem* header = table->horizontalHeaderItem(column);
            if (header != nullptr && header->text() == T("catalog.col.localNumber")) {
                return column;
            }
        }
        return -1;
    }();
    int ticked = 0;
    for (int row = 0; row < table->rowCount() && ticked < 3; ++row) {
        const QTableWidgetItem* local = localColumn < 0 ? nullptr : table->item(row, localColumn);
        if (local != nullptr
            && !local->data(LocalNumberDelegate::kOnLoanRole).toStringList().isEmpty()) {
            continue;
        }
        QTableWidgetItem* title = table->item(row, 0);
        if (title == nullptr) {
            continue;
        }
        title->setCheckState(Qt::Checked);
        ++ticked;
    }
    if (ticked < 3) {
        fail("could not tick three shelf titles");
    }
    capture.settle(200);
    capture.openModal(
        [&]() { capture.click(capture.button("catalog.delete")); },
        [&](QWidget* modal) {
            capture.save(QStringLiteral("rm-bulk-confirm"), capture.grab(modal));
            answer(capture, modal, "common.no");
        });
}

void shotReuseChooser(Capture& capture)
{
    ensureArchivedBook();
    capture.goTo("nav.archive");
    pickList(capture, "archiveType", T("archive.type.copies"));
    if (capture.table()->rowCount() < 1) {
        fail("no archived copy");
    }
    holdRow(capture.table(), 0);
    capture.openModal(
        [&]() { capture.click(capture.button("archive.reuse")); },
        [&](QWidget* chooser) {
            capture.save(QStringLiteral("reuse-chooser"), capture.grab(chooser));
            closeModal(chooser);
        });
}

void shotReuseEditor(Capture& capture)
{
    ensureArchivedBook();
    capture.goTo("nav.archive");
    pickList(capture, "archiveType", T("archive.type.copies"));
    if (capture.table()->rowCount() < 1) {
        fail("no archived copy");
    }
    holdRow(capture.table(), 0);
    capture.openModal(
        [&]() { capture.click(capture.button("archive.reuse")); },
        [&](QWidget* chooser) {
            auto* books = chooser->findChild<QListWidget*>(QStringLiteral("reuseBooks"));
            if (books == nullptr || books->count() < 1) {
                fail("the book chooser is empty");
            }
            books->setCurrentRow(0);
            // Accepting the chooser opens the editor only after this callback
            // returns, so a timer grabs the editor once it is on screen.
            auto* timer = new QTimer(static_cast<QObject*>(&capture.window()));
            timer->setSingleShot(true);
            QObject::connect(timer, &QTimer::timeout, &capture.window(), [timer, &capture]() {
                for (QWidget* widget : QApplication::topLevelWidgets()) {
                    if (widget == nullptr || !widget->isVisible()
                        || !widget->inherits("BookEditorDialog")) {
                        continue;
                    }
                    if (auto* tabs = widget->findChild<QTabWidget*>()) {
                        const QString copies = T("book.tab.copies");
                        for (int index = 0; index < tabs->count(); ++index) {
                            if (tabs->tabText(index) == copies) {
                                tabs->setCurrentIndex(index);
                            }
                        }
                    }
                    capture.settle(200);
                    capture.save(QStringLiteral("reuse-editor"), capture.grab(widget));
                    if (auto* dialog = qobject_cast<QDialog*>(widget)) {
                        dialog->reject();
                    }
                    timer->deleteLater();
                    return;
                }
                const int attempts = timer->property("attempts").toInt();
                if (attempts > 30) {
                    for (QWidget* widget : QApplication::topLevelWidgets()) {
                        if (auto* dialog = qobject_cast<QDialog*>(widget);
                            dialog != nullptr && dialog->isVisible()) {
                            dialog->reject();
                        }
                    }
                    timer->deleteLater();
                    return;
                }
                timer->setProperty("attempts", attempts + 1);
                timer->start(50);
            });
            timer->start(100);
            if (auto* dialog = qobject_cast<QDialog*>(chooser)) {
                dialog->accept();
            }
        });
}

}  // namespace

void registerTaskShots(ShotRegistry& registry)
{
    registry.emplace(QStringLiteral("add-book-open"), shotAddBookOpen);
    registry.emplace(QStringLiteral("add-book-filled"), shotAddBookFilled);
    registry.emplace(QStringLiteral("add-book-copies"), shotAddBookCopies);
    registry.emplace(QStringLiteral("add-book-free-number"), shotAddBookFreeNumber);
    registry.emplace(QStringLiteral("add-book-categories"), shotAddBookCategories);
    registry.emplace(QStringLiteral("reg-open"), shotRegOpen);
    registry.emplace(QStringLiteral("reg-filled"), shotRegFilled);
    registry.emplace(QStringLiteral("reg-renew"), shotRegRenew);
    registry.emplace(QStringLiteral("reg-renewed"), shotRegRenewed);
    registry.emplace(QStringLiteral("lend-dialog"), shotLendDialog);
    registry.emplace(QStringLiteral("lend-filled"), shotLendFilled);
    registry.emplace(QStringLiteral("lend-from-book"), shotLendFromBook);
    registry.emplace(QStringLiteral("lend-from-member"), shotLendFromMember);
    registry.emplace(QStringLiteral("lend-empty-history"), shotLendEmptyHistory);
    registry.emplace(QStringLiteral("ret-dialog"), shotRetDialog);
    registry.emplace(QStringLiteral("ext-dialog"), shotExtDialog);
    registry.emplace(QStringLiteral("ret-from-history"), shotRetFromHistory);
    registry.emplace(QStringLiteral("rm-book-confirm"), shotRmBookConfirm);
    registry.emplace(QStringLiteral("rm-book-refused"), shotRmBookRefused);
    registry.emplace(QStringLiteral("rm-copy-row"), shotRmCopyRow);
    registry.emplace(QStringLiteral("rm-loan-confirm"), shotRmLoanConfirm);
    registry.emplace(QStringLiteral("rm-bulk-confirm"), shotRmBulkConfirm);
    registry.emplace(QStringLiteral("reuse-chooser"), shotReuseChooser);
    registry.emplace(QStringLiteral("reuse-editor"), shotReuseEditor);
}

}  // namespace ManualCapture
