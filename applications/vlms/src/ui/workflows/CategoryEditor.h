#pragma once

#include <VLMS/Core/Result.h>
#include <VLMS/Repositories/CatalogTypes.h>

#include <QString>

#include <vector>

class QWidget;

namespace VLMS {

/// What the category manager may do. BookWorkflow implements it, so every
/// write goes through the workflow; the dialog only lists and asks. Each
/// write shows its own error and returns the status.
class CategoryEditor {
public:
    virtual ~CategoryEditor() = default;
    [[nodiscard]] virtual std::vector<Repositories::CategoryRecord> categories() const = 0;
    virtual Core::Status create(QWidget* parent, const QString& code, const QString& label) = 0;
    virtual Core::Status update(QWidget* parent, qint64 id, const QString& code, const QString& label) = 0;
    virtual Core::Status remove(QWidget* parent, qint64 id) = 0;
};

}  // namespace VLMS
