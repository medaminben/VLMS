#include "ui/workflows/BulkRunner.h"

#include "ui/workflows/DataEvents.h"
#include "ui/workflows/DialogFactory.h"

namespace VLMS {
namespace {

class FactoryBulkUi final : public BulkUi {
public:
    FactoryBulkUi(DialogFactory& dialogs, QWidget* parent) : m_dialogs(dialogs), m_parent(parent) {}

    bool confirm(const QString& title, const QString& text) override
    {
        return m_dialogs.confirmBulk(m_parent, BulkPlan{title, text});
    }
    void inform(const QString& title, const QString& text) override
    {
        m_dialogs.inform(m_parent, title, text);
    }
    std::unique_ptr<BulkProgress> startProgress(const QString& title, int total) override
    {
        return m_dialogs.startProgress(m_parent, title, total);
    }

private:
    DialogFactory& m_dialogs;
    QWidget* m_parent = nullptr;
};

}  // namespace

BulkRunner::BulkRunner(DialogFactory& dialogs, DataEvents& events)
    : m_dialogs(dialogs)
    , m_events(events)
{
}

int BulkRunner::run(QWidget* parent,
                    const BulkActionTexts& texts,
                    const QList<BulkRow>& rows,
                    const std::function<Core::Status(const BulkRow&)>& check,
                    const std::function<Core::Status(const BulkRow&)>& act,
                    EntitySet emitWhat)
{
    FactoryBulkUi ui(m_dialogs, parent);
    const BulkOutcome outcome = runBulkFlow(ui, texts, rows, check, act);
    if (outcome.done > 0 && emitWhat != EntitySet{}) {
        m_events.emitChanged(emitWhat, outcome.succeeded);
    }
    return outcome.done;
}

}  // namespace VLMS
