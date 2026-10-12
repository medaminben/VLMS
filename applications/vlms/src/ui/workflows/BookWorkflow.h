#pragma once

#include "ui/workflows/Forms.h"

#include <VLMS/Core/Result.h>
#include <VLMS/Repositories/CatalogTypes.h>

#include <QList>

#include <cstdint>

class QWidget;

namespace VLMS {

class BulkRunner;
class DataEvents;
class LoanListSpec;
class LoanWorkflow;
class DialogFactory;
class ErrorPresenter;

namespace Repositories {
class CatalogRepository;
}  // namespace Repositories

class BookWorkflow {
public:
    BookWorkflow(Repositories::CatalogRepository& catalog,
                 DialogFactory& dialogs,
                 ErrorPresenter& errors,
                 DataEvents& events,
                 BulkRunner& bulk,
                 LoanWorkflow& loans,
                 LoanListSpec& loanHistorySpec);

    Core::Status add(QWidget* parent);
    Core::Status edit(QWidget* parent, qint64 bookId);
    Core::Status archive(QWidget* parent, const QList<qint64>& ids, const QList<QString>& labels);
    Core::Status showLoans(QWidget* parent, qint64 bookId);
    /// Opens the category manager. Emits Categories once if anything changed.
    Core::Status manageCategories(QWidget* parent);

    /// What the book editor opens on, with Categories wired to this workflow.
    [[nodiscard]] BookEditRequest editRequest(std::optional<Repositories::BookRecord> book = {});
    [[nodiscard]] static Repositories::BookWrite writeFor(const BookForm& form);

private:
    Repositories::CatalogRepository& m_catalog;
    DialogFactory& m_dialogs;
    ErrorPresenter& m_errors;
    DataEvents& m_events;
    BulkRunner& m_bulk;
    LoanWorkflow& m_loanWorkflow;
    LoanListSpec& m_loanHistorySpec;
};

}  // namespace VLMS
