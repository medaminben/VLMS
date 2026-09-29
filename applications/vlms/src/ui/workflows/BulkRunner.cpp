#include "ui/workflows/BulkRunner.h"

#include "ui/workflows/DataEvents.h"
#include "ui/workflows/DialogFactory.h"
#include "ui/workflows/ErrorPresenter.h"

namespace VLMS {

BulkRunner::BulkRunner(DialogFactory& dialogs, ErrorPresenter& errors, DataEvents& events)
    : m_dialogs(dialogs)
    , m_errors(errors)
    , m_events(events)
{
    (void)m_dialogs;
    (void)m_errors;
}

int BulkRunner::run(QWidget* parent,
                    const BulkActionTexts& texts,
                    const QList<BulkRow>& rows,
                    const std::function<Core::Status(const BulkRow&)>& check,
                    const std::function<Core::Status(const BulkRow&)>& act,
                    EntitySet emitWhat)
{
    // Phase 2: reuse today's runBulkAction (own Yes/No confirm). Phase 5 moves
    // the confirm prompt onto DialogFactory::confirmBulk.
    QList<qint64> succeeded;
    const int done = runBulkAction(
        parent, texts, rows, check,
        [&](const BulkRow& row) -> Core::Status {
            const Core::Status status = act(row);
            if (status) {
                succeeded.append(row.id);
            }
            return status;
        });
    if (done > 0 && emitWhat != EntitySet{}) {
        m_events.emitChanged(emitWhat, succeeded);
    }
    return done;
}

}  // namespace VLMS
