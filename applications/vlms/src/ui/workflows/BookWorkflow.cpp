#include "ui/workflows/BookWorkflow.h"

#include "QtBridge.h"
#include "ui/views/LoanHistoryDialog.h"
#include "ui/views/BookEditorDialog.h"
#include "ui/presentation/LoanListSpec.h"
#include "ui/workflows/BulkRunner.h"
#include "ui/workflows/LoanWorkflow.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/DialogFactory.h"
#include "ui/workflows/ErrorPresenter.h"
#include "ui/shell/Navigator.h"
#include "ui/views/UiHelpers.h"

#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>

#include <QWidget>

using VLMS::T;
using VLMS::qs;
using VLMS::ss;

namespace VLMS {

BookWorkflow::BookWorkflow(Repositories::CatalogRepository& catalog,
                           Repositories::CirculationRepository& circulation,
                           CatalogLookups& lookups,
                           OcrService& ocr,
                           DialogFactory& dialogs,
                           ErrorPresenter& errors,
                           DataEvents& events,
                           Navigator& navigator,
                           BulkRunner& bulk,
                           LoanWorkflow& loans,
                           LoanListSpec& loanHistorySpec)
    : m_catalog(catalog)
    , m_circulation(circulation)
    , m_lookups(lookups)
    , m_ocr(ocr)
    , m_dialogs(dialogs)
    , m_errors(errors)
    , m_events(events)
    , m_navigator(navigator)
    , m_bulk(bulk)
    , m_loanWorkflow(loans)
    , m_loanHistorySpec(loanHistorySpec)
{
    (void)m_navigator;
}

Core::Status BookWorkflow::add(QWidget* parent)
{
    const auto form = m_dialogs.addBook(parent);
    if (!form) {
        return Core::Status::ok();
    }

    Repositories::BookWrite write;
    write.book = form->book;
    write.copies = form->copies;
    if (form->coverChanged) {
        write.coverSourcePath = ss(form->coverSourcePath);
    }
    const auto created = m_catalog.saveNewBook(write);
    if (!created) {
        m_errors.show(parent, created.error());
        return Core::asStatus(created);
    }
    m_events.emitChanged(Entity::Books | Entity::Copies, {created.value()});
    return Core::Status::ok();
}

Core::Status BookWorkflow::edit(QWidget* parent, qint64 bookId)
{
    if (bookId <= 0) {
        showInformation(parent, T("catalog.editBook"), T("catalog.selectBookFirst"));
        return Core::Status::ok();
    }

    const auto book = m_catalog.getBook(bookId);
    if (!book) {
        m_errors.show(parent, book.error());
        return Core::asStatus(book);
    }

    // Phase 2: DialogFactory::editBook still rebuilds from BookInput only; open
    // the existing editor with the full record so copies and cover load.
    BookEditorDialog dialog(m_lookups, m_ocr, book.value(), parent);
    dialog.setDialogFactory(&m_dialogs);
    if (dialog.exec() != QDialog::Accepted) {
        return Core::Status::ok();
    }

    Repositories::BookWrite write;
    write.book = dialog.bookInput();
    write.copies = dialog.copyInputs();
    if (dialog.coverChanged()) {
        write.coverSourcePath = ss(dialog.coverSourcePath());
    }
    const auto saved = m_catalog.saveExistingBook(bookId, write);
    if (!saved) {
        m_errors.show(parent, saved.error());
        return saved;
    }
    m_events.emitChanged(Entity::Books | Entity::Copies, {bookId});
    return Core::Status::ok();
}

Core::Status BookWorkflow::archive(QWidget* parent,
                                   const QList<qint64>& ids,
                                   const QList<QString>& labels)
{
    if (ids.isEmpty()) {
        showInformation(parent, T("catalog.deleteBook"), T("catalog.selectBookFirst"));
        return Core::Status::ok();
    }

    QList<BulkRow> rows;
    for (int i = 0; i < ids.size(); ++i) {
        BulkRow row;
        row.id = ids.at(i);
        row.row = i;
        row.label = i < labels.size() ? labels.at(i) : QString::number(ids.at(i));
        rows.append(row);
    }

    if (rows.size() == 1) {
        const auto open = m_catalog.bookHasOpenLoans(rows.first().id);
        if (!open) {
            m_errors.show(parent, open.error());
            return Core::asStatus(open);
        }
        if (open.value()) {
            showInformation(parent, T("catalog.deleteBook"), T("error.book.hasActiveLoans"));
            return Core::Status::ok();
        }
        if (!askYesNo(parent, T("catalog.deleteBook"), T("catalog.deleteConfirm"))) {
            return Core::Status::ok();
        }
        const auto archived = m_catalog.archiveBook(rows.first().id);
        if (!archived) {
            m_errors.show(parent, archived.error());
            return archived;
        }
        m_events.emitChanged(Entity::Books, {rows.first().id});
        return Core::Status::ok();
    }

    BulkActionTexts texts;
    texts.title = T("catalog.deleteBook");
    texts.verb = T("bulk.verb.archive");
    texts.passive = T("bulk.passive.archive");
    texts.noun = T("bulk.noun.books");
    m_bulk.run(
        parent, texts, rows,
        [this](const BulkRow& bulk) { return m_catalog.canArchiveBook(bulk.id); },
        [this](const BulkRow& bulk) { return m_catalog.archiveBook(bulk.id); },
        Entity::Books);
    return Core::Status::ok();
}

Core::Status BookWorkflow::showLoans(QWidget* parent, qint64 bookId)
{
    if (bookId <= 0) {
        return Core::Status::ok();
    }
    const auto book = m_catalog.getBook(bookId);
    if (!book) {
        m_errors.show(parent, book.error());
        return Core::asStatus(book);
    }
    LoanHistoryDialog dialog(m_loanHistorySpec, m_loanWorkflow, m_events,
                             LoanHistoryDialog::Kind::Book, bookId, qs(book->title),
                             Repositories::ArchiveScope::Any, parent);
    dialog.exec();
    return Core::Status::ok();
}

Core::Status BookWorkflow::manageCategories(QWidget* parent)
{
    (void)m_dialogs.manageCategories(parent);
    m_events.emitChanged(Entity::Categories, {});
    return Core::Status::ok();
}

}  // namespace VLMS
