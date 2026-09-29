#pragma once

#include "ui/workflows/Navigator.h"

#include <VLMS/Core/Result.h>

#include <QList>
#include <QString>

#include <cstdint>

class QWidget;

namespace VLMS {

class BulkRunner;
class CatalogLookups;
class DataEvents;
class DialogFactory;
class ErrorPresenter;
class LoanListSpec;
class LoanWorkflow;
class OcrService;

namespace Repositories {
class CatalogRepository;
class CirculationRepository;
class MemberRepository;
}  // namespace Repositories

class ArchiveWorkflow {
public:
    ArchiveWorkflow(Repositories::MemberRepository& members,
                    Repositories::CatalogRepository& catalog,
                    Repositories::CirculationRepository& circulation,
                    CatalogLookups& catalogLookups,
                    OcrService& ocr,
                    DialogFactory& dialogs,
                    ErrorPresenter& errors,
                    DataEvents& events,
                    Navigator& navigator,
                    BulkRunner& bulk,
                    LoanListSpec& loanHistorySpec,
                    LoanWorkflow& loans);

    Core::Status restore(QWidget* parent, ArchiveType type,
                         const QList<qint64>& ids, const QList<QString>& labels);
    Core::Status purge(QWidget* parent, ArchiveType type,
                       const QList<qint64>& ids, const QList<QString>& labels);
    Core::Status reuseNumber(QWidget* parent, qint64 copyId);
    Core::Status showLoans(QWidget* parent, ArchiveType type, qint64 id,
                           const QString& displayName);

private:
    Repositories::MemberRepository& m_members;
    Repositories::CatalogRepository& m_catalog;
    Repositories::CirculationRepository& m_circulation;
    CatalogLookups& m_catalogLookups;
    OcrService& m_ocr;
    DialogFactory& m_dialogs;
    ErrorPresenter& m_errors;
    DataEvents& m_events;
    Navigator& m_navigator;
    BulkRunner& m_bulk;
    LoanListSpec& m_loanHistorySpec;
    LoanWorkflow& m_loanWorkflow;
};

}  // namespace VLMS
