#pragma once

#include <VLMS/Core/Result.h>
#include <VLMS/Repositories/CatalogTypes.h>

#include <QList>

#include <cstdint>

class QWidget;

namespace VLMS {

class BulkRunner;
class DataEvents;
class DialogFactory;
class ErrorPresenter;
class Navigator;

namespace Repositories {
class CatalogRepository;
class CirculationRepository;
}  // namespace Repositories

class BookWorkflow {
public:
    BookWorkflow(Repositories::CatalogRepository& catalog,
                 Repositories::CirculationRepository& circulation,
                 DialogFactory& dialogs,
                 ErrorPresenter& errors,
                 DataEvents& events,
                 Navigator& navigator,
                 BulkRunner& bulk);

    Core::Status add(QWidget* parent);
    Core::Status edit(QWidget* parent, qint64 bookId);
    Core::Status archive(QWidget* parent, const QList<qint64>& ids, const QList<QString>& labels);
    Core::Status showLoans(QWidget* parent, qint64 bookId);
    Core::Status manageCategories(QWidget* parent);

private:
    Repositories::CatalogRepository& m_catalog;
    Repositories::CirculationRepository& m_circulation;
    DialogFactory& m_dialogs;
    ErrorPresenter& m_errors;
    DataEvents& m_events;
    Navigator& m_navigator;
    BulkRunner& m_bulk;
};

}  // namespace VLMS
