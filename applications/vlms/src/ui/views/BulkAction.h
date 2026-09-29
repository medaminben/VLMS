#pragma once

#include "ui/workflows/BulkFlow.h"

#include <VLMS/Core/Result.h>

#include <QList>

#include <functional>

class QWidget;

/// runBulkFlow over Qt message boxes and a progress dialog, for widgets that
/// act on rows in their own memory (the book editor's copies table).
/// Returns how many calls to `act` succeeded.
int runBulkAction(QWidget* parent,
                  const BulkActionTexts& texts,
                  const QList<BulkRow>& rows,
                  const std::function<VLMS::Core::Status(const BulkRow&)>& check,
                  const std::function<VLMS::Core::Status(const BulkRow&)>& act);

namespace VLMS {

/// Qt message boxes and progress dialog for a bulk action.
class QtBulkUi final : public BulkUi {
public:
    explicit QtBulkUi(QWidget* parent) : m_parent(parent) {}

    bool confirm(const QString& title, const QString& text) override;
    void inform(const QString& title, const QString& text) override;
    std::unique_ptr<BulkProgress> startProgress(const QString& title, int total) override;

private:
    QWidget* m_parent = nullptr;
};

}  // namespace VLMS
