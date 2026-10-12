#include "ui/workflows/BookWorkflow.h"

#include "QtBridge.h"
#include "ui/workflows/BulkRunner.h"
#include "ui/workflows/CategoryEditor.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/DialogFactory.h"
#include "ui/workflows/ErrorPresenter.h"

#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>

#include <QWidget>

using VLMS::T;
using VLMS::qs;
using VLMS::ss;

namespace VLMS {
namespace {

/// The category manager's writes, run by the workflow. Remembers whether
/// anything changed so the caller emits one event, or none.
class CatalogCategoryEditor final : public CategoryEditor {
public:
    CatalogCategoryEditor(Repositories::CatalogRepository& catalog, ErrorPresenter& errors)
        : m_catalog(catalog)
        , m_errors(errors)
    {
    }

    [[nodiscard]] std::vector<Repositories::CategoryRecord> categories() const override
    {
        const auto all = m_catalog.listAllCategories();
        return all ? all.value() : std::vector<Repositories::CategoryRecord>{};
    }

    Core::Status create(QWidget* parent, const QString& code, const QString& label) override
    {
        return track(parent, Core::asStatus(m_catalog.createCategory(ss(code), ss(label))));
    }

    Core::Status update(QWidget* parent, qint64 id, const QString& code, const QString& label) override
    {
        return track(parent, m_catalog.updateCategory(id, ss(code), ss(label)));
    }

    Core::Status remove(QWidget* parent, qint64 id) override
    {
        return track(parent, m_catalog.deleteCategory(id));
    }

    [[nodiscard]] bool changed() const { return m_changed; }

private:
    Core::Status track(QWidget* parent, const Core::Status& status)
    {
        if (!status) {
            m_errors.show(parent, status.error());
        } else {
            m_changed = true;
        }
        return status;
    }

    Repositories::CatalogRepository& m_catalog;
    ErrorPresenter& m_errors;
    bool m_changed = false;
};

}  // namespace

BookWorkflow::BookWorkflow(Repositories::CatalogRepository& catalog,
                           DialogFactory& dialogs,
                           ErrorPresenter& errors,
                           DataEvents& events,
                           BulkRunner& bulk,
                           LoanWorkflow& loans,
                           LoanListSpec& loanHistorySpec)
    : m_catalog(catalog)
    , m_dialogs(dialogs)
    , m_errors(errors)
    , m_events(events)
    , m_bulk(bulk)
    , m_loanWorkflow(loans)
    , m_loanHistorySpec(loanHistorySpec)
{
}

BookEditRequest BookWorkflow::editRequest(std::optional<Repositories::BookRecord> book)
{
    BookEditRequest request;
    request.book = std::move(book);
    request.manageCategories = [this](QWidget* parent) { manageCategories(parent); };
    return request;
}

Repositories::BookWrite BookWorkflow::writeFor(const BookForm& form)
{
    Repositories::BookWrite write;
    write.book = form.book;
    write.copies = form.copies;
    if (form.coverChanged) {
        write.coverSourcePath = ss(form.coverSourcePath);
    }
    return write;
}

Core::Status BookWorkflow::add(QWidget* parent)
{
    const auto form = m_dialogs.editBook(parent, editRequest());
    if (!form) {
        return Core::Status::ok();
    }
    const auto created = m_catalog.saveNewBook(writeFor(*form));
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
        m_dialogs.inform(parent, T("catalog.editBook"), T("catalog.selectBookFirst"));
        return Core::Status::ok();
    }
    const auto book = m_catalog.getBook(bookId);
    if (!book) {
        m_errors.show(parent, book.error());
        return Core::asStatus(book);
    }
    const auto form = m_dialogs.editBook(parent, editRequest(book.value()));
    if (!form) {
        return Core::Status::ok();
    }
    const auto saved = m_catalog.saveExistingBook(bookId, writeFor(*form));
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
        m_dialogs.inform(parent, T("catalog.deleteBook"), T("catalog.selectBookFirst"));
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
            m_dialogs.inform(parent, T("catalog.deleteBook"), T("error.book.hasActiveLoans"));
            return Core::Status::ok();
        }
        if (!m_dialogs.askYesNo(parent, T("catalog.deleteBook"), T("catalog.deleteConfirm"))) {
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
    LoanHistoryRequest request;
    request.kind = LoanHistoryKind::Book;
    request.id = bookId;
    request.title = qs(book->title);
    request.spec = &m_loanHistorySpec;
    request.loans = &m_loanWorkflow;
    request.events = &m_events;
    m_dialogs.showLoanHistory(parent, request);
    return Core::Status::ok();
}

Core::Status BookWorkflow::manageCategories(QWidget* parent)
{
    CatalogCategoryEditor editor(m_catalog, m_errors);
    m_dialogs.manageCategories(parent, editor);
    if (editor.changed()) {
        m_events.emitChanged(Entity::Categories, {});
    }
    return Core::Status::ok();
}

}  // namespace VLMS
