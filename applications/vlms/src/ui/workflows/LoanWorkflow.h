#pragma once

#include <VLMS/Core/Result.h>

#include <QList>
#include <QString>

#include <cstdint>

class QWidget;

namespace VLMS {

class BulkRunner;
class CirculationLookups;
class DataEvents;
class DialogFactory;
class ErrorPresenter;
class Navigator;

namespace Repositories {
class CirculationRepository;
}  // namespace Repositories

struct LoanCheckoutPrefill {
    qint64 bookId = 0;
    qint64 memberId = 0;
};

class LoanWorkflow {
public:
    LoanWorkflow(Repositories::CirculationRepository& circulation,
                 CirculationLookups& lookups,
                 DialogFactory& dialogs,
                 ErrorPresenter& errors,
                 DataEvents& events,
                 Navigator& navigator,
                 BulkRunner& bulk);

    Core::Status checkout(QWidget* parent, const LoanCheckoutPrefill& prefill = {});
    Core::Status returnLoan(QWidget* parent, qint64 loanId);
    Core::Status extend(QWidget* parent, qint64 loanId);
    Core::Status archive(QWidget* parent, const QList<qint64>& ids, const QList<QString>& labels);

private:
    Repositories::CirculationRepository& m_circulation;
    CirculationLookups& m_lookups;
    DialogFactory& m_dialogs;
    ErrorPresenter& m_errors;
    DataEvents& m_events;
    Navigator& m_navigator;
    BulkRunner& m_bulk;
};

}  // namespace VLMS
