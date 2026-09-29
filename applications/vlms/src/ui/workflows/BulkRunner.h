#pragma once

#include "ui/views/BulkAction.h"
#include "ui/workflows/DataEvents.h"

#include <VLMS/Core/Result.h>

#include <QList>
#include <functional>

class QWidget;

namespace VLMS {

class DialogFactory;
class ErrorPresenter;

/// Keeps runBulkAction's check-then-act logic and emits DataEvents for
/// successful ids. Confirmation still uses runBulkAction's Yes/No for now.
class BulkRunner {
public:
    BulkRunner(DialogFactory& dialogs, ErrorPresenter& errors, DataEvents& events);

    int run(QWidget* parent,
            const BulkActionTexts& texts,
            const QList<BulkRow>& rows,
            const std::function<Core::Status(const BulkRow&)>& check,
            const std::function<Core::Status(const BulkRow&)>& act,
            EntitySet emitWhat);

private:
    DialogFactory& m_dialogs;
    ErrorPresenter& m_errors;
    DataEvents& m_events;
};

}  // namespace VLMS
