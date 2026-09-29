#pragma once

#include "ui/workflows/BulkFlow.h"
#include "ui/workflows/DataEvents.h"

#include <VLMS/Core/Result.h>

#include <QList>
#include <functional>

class QWidget;

namespace VLMS {

class DialogFactory;

/// runBulkFlow for workflows: confirmation, progress and the failure summary
/// go through DialogFactory, and one `changed` event carries every id that
/// succeeded.
class BulkRunner {
public:
    BulkRunner(DialogFactory& dialogs, DataEvents& events);

    int run(QWidget* parent,
            const BulkActionTexts& texts,
            const QList<BulkRow>& rows,
            const std::function<Core::Status(const BulkRow&)>& check,
            const std::function<Core::Status(const BulkRow&)>& act,
            EntitySet emitWhat);

private:
    DialogFactory& m_dialogs;
    DataEvents& m_events;
};

}  // namespace VLMS
