#pragma once

#include <VLMS/Core/Result.h>
#include <VLMS/Repositories/CatalogTypes.h>

#include <QList>

#include <cstdint>

class QWidget;

namespace VLMS {

class BulkRunner;
class CatalogLookups;
class DataEvents;
class LoanListSpec;
class LoanWorkflow;
class DialogFactory;
class ErrorPresenter;
class Navigator;
class OcrService;

namespace Repositories {
class CatalogRepository;
class CirculationRepository;
}  // namespace Repositories

class BookWorkflow {
public:
    BookWorkflow(Repositories::CatalogRepository& catalog,
                 Repositories::CirculationRepository& circulation,
                 CatalogLookups& lookups,
                 OcrService& ocr,
                 DialogFactory& dialogs,
                 ErrorPresenter& errors,
                 DataEvents& events,
                 Navigator& navigator,
                 BulkRunner& bulk,
                 LoanWorkflow& loans,
                 LoanListSpec& loanHistorySpec);

    Core::Status add(QWidget* parent);
    Core::Status edit(QWidget* parent, qint64 bookId);
    Core::Status archive(QWidget* parent, const QList<qint64>& ids, const QList<QString>& labels);
    Core::Status showLoans(QWidget* parent, qint64 bookId);
    Core::Status manageCategories(QWidget* parent);

private:
    Repositories::CatalogRepository& m_catalog;
    Repositories::CirculationRepository& m_circulation;
    CatalogLookups& m_lookups;
    OcrService& m_ocr;
    DialogFactory& m_dialogs;
    ErrorPresenter& m_errors;
    DataEvents& m_events;
    Navigator& m_navigator;
    BulkRunner& m_bulk;
    LoanWorkflow& m_loanWorkflow;
    LoanListSpec& m_loanHistorySpec;
};

}  // namespace VLMS
