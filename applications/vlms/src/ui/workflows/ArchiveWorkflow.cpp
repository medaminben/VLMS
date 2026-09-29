#include "ui/workflows/ArchiveWorkflow.h"

#include "ui/BulkAction.h"
#include "ui/UiHelpers.h"
#include "ui/archive/ArchiveLoansDialog.h"
#include "ui/archive/ReuseNumberFlow.h"
#include "ui/workflows/BulkRunner.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/DialogFactory.h"
#include "ui/workflows/ErrorPresenter.h"

#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QWidget>

using VLMS::T;

namespace VLMS {
namespace {

EntitySet entityFor(const ArchiveType type)
{
    switch (type) {
    case ArchiveType::Members:
        return Entity::Members;
    case ArchiveType::Books:
        return Entity::Books;
    case ArchiveType::Copies:
        return Entity::Copies;
    case ArchiveType::Loans:
        return Entity::Loans;
    }
    return EntitySet{};
}

const char* bulkNounKey(const ArchiveType type)
{
    switch (type) {
    case ArchiveType::Members:
        return "bulk.noun.members";
    case ArchiveType::Books:
        return "bulk.noun.books";
    case ArchiveType::Copies:
        return "bulk.noun.copies";
    case ArchiveType::Loans:
        return "bulk.noun.loans";
    }
    return "bulk.noun.books";
}

const char* purgeConfirmKey(const ArchiveType type)
{
    switch (type) {
    case ArchiveType::Members:
        return "archive.purgeConfirmMember";
    case ArchiveType::Books:
        return "archive.purgeConfirmBook";
    case ArchiveType::Copies:
        return "archive.purgeConfirmCopy";
    case ArchiveType::Loans:
        return "archive.purgeConfirmLoan";
    }
    return "archive.purgeConfirmMember";
}

QList<BulkRow> toBulkRows(const QList<qint64>& ids, const QList<QString>& labels)
{
    QList<BulkRow> rows;
    for (int i = 0; i < ids.size(); ++i) {
        BulkRow row;
        row.id = ids.at(i);
        row.row = i;
        row.label = i < labels.size() ? labels.at(i) : QString::number(ids.at(i));
        rows.append(row);
    }
    return rows;
}

Core::Status restoreOne(Repositories::MemberRepository& members,
                        Repositories::CatalogRepository& catalog,
                        Repositories::CirculationRepository& circulation,
                        const ArchiveType type,
                        const qint64 id)
{
    switch (type) {
    case ArchiveType::Members:
        return members.restoreMember(id);
    case ArchiveType::Books:
        return catalog.restoreBook(id);
    case ArchiveType::Copies:
        return catalog.restoreCopy(id);
    case ArchiveType::Loans:
        return circulation.restoreLoan(id);
    }
    return Core::Status::ok();
}

Core::Status canPurgeOne(Repositories::MemberRepository& members,
                         Repositories::CatalogRepository& catalog,
                         Repositories::CirculationRepository& circulation,
                         const ArchiveType type,
                         const qint64 id)
{
    switch (type) {
    case ArchiveType::Members:
        return members.canPurgeMember(id);
    case ArchiveType::Books:
        return catalog.canPurgeBook(id);
    case ArchiveType::Copies:
        return catalog.canPurgeCopy(id);
    case ArchiveType::Loans:
        return circulation.canPurgeLoan(id);
    }
    return Core::Status::ok();
}

Core::Status purgeOne(Repositories::MemberRepository& members,
                      Repositories::CatalogRepository& catalog,
                      Repositories::CirculationRepository& circulation,
                      const ArchiveType type,
                      const qint64 id)
{
    switch (type) {
    case ArchiveType::Members:
        return members.purgeMember(id);
    case ArchiveType::Books:
        return catalog.purgeBook(id);
    case ArchiveType::Copies:
        return catalog.purgeCopy(id);
    case ArchiveType::Loans:
        return circulation.purgeLoan(id);
    }
    return Core::Status::ok();
}

}  // namespace

ArchiveWorkflow::ArchiveWorkflow(Repositories::MemberRepository& members,
                                 Repositories::CatalogRepository& catalog,
                                 Repositories::CirculationRepository& circulation,
                                 DialogFactory& dialogs,
                                 ErrorPresenter& errors,
                                 DataEvents& events,
                                 Navigator& navigator,
                                 BulkRunner& bulk)
    : m_members(members)
    , m_catalog(catalog)
    , m_circulation(circulation)
    , m_dialogs(dialogs)
    , m_errors(errors)
    , m_events(events)
    , m_navigator(navigator)
    , m_bulk(bulk)
{
    (void)m_dialogs;
    (void)m_navigator;
}

Core::Status ArchiveWorkflow::restore(QWidget* parent,
                                      ArchiveType type,
                                      const QList<qint64>& ids,
                                      const QList<QString>& labels)
{
    if (ids.isEmpty()) {
        return Core::Status::ok();
    }

    const QList<BulkRow> rows = toBulkRows(ids, labels);
    if (rows.size() == 1) {
        if (!askYesNo(parent, T("archive.restore"), T("archive.restoreConfirm"))) {
            return Core::Status::ok();
        }
        const qint64 id = rows.first().id;
        const auto restored = restoreOne(m_members, m_catalog, m_circulation, type, id);
        if (!restored) {
            m_errors.show(parent, restored.error());
            return restored;
        }
        m_events.emitChanged(entityFor(type), {id});
        return Core::Status::ok();
    }

    BulkActionTexts texts;
    texts.title = T("archive.restore");
    texts.verb = T("bulk.verb.restore");
    texts.passive = T("bulk.passive.restore");
    texts.noun = T(bulkNounKey(type));
    m_bulk.run(
        parent, texts, rows,
        [](const BulkRow&) { return Core::Status::ok(); },
        [this, type](const BulkRow& bulk) {
            return restoreOne(m_members, m_catalog, m_circulation, type, bulk.id);
        },
        entityFor(type));
    return Core::Status::ok();
}

Core::Status ArchiveWorkflow::purge(QWidget* parent,
                                    ArchiveType type,
                                    const QList<qint64>& ids,
                                    const QList<QString>& labels)
{
    if (ids.isEmpty()) {
        return Core::Status::ok();
    }

    const QList<BulkRow> rows = toBulkRows(ids, labels);
    if (rows.size() == 1) {
        if (!askYesNo(parent, T("archive.purge"), T(purgeConfirmKey(type)))) {
            return Core::Status::ok();
        }
        const qint64 id = rows.first().id;
        const auto removed = purgeOne(m_members, m_catalog, m_circulation, type, id);
        if (!removed) {
            m_errors.show(parent, removed.error());
            return removed;
        }
        // ListPage reloads from DataEvents; the old archive page used to refresh itself.
        m_events.emitChanged(entityFor(type), {id});
        return Core::Status::ok();
    }

    BulkActionTexts texts;
    texts.title = T("archive.purge");
    texts.verb = T("bulk.verb.purge");
    texts.passive = T("bulk.passive.purge");
    texts.noun = T(bulkNounKey(type));
    m_bulk.run(
        parent, texts, rows,
        [this, type](const BulkRow& bulk) {
            return canPurgeOne(m_members, m_catalog, m_circulation, type, bulk.id);
        },
        [this, type](const BulkRow& bulk) {
            return purgeOne(m_members, m_catalog, m_circulation, type, bulk.id);
        },
        entityFor(type));
    return Core::Status::ok();
}

Core::Status ArchiveWorkflow::reuseNumber(QWidget* parent, qint64 copyId)
{
    if (copyId <= 0) {
        return Core::Status::ok();
    }

    Repositories::BookCopyRecord found;
    bool have = false;
    Repositories::CopyQuery query;
    query.archive = Repositories::ArchiveScope::Archived;
    query.limit = 200;
    for (int offset = 0;; offset += query.limit) {
        query.offset = offset;
        const auto rows = m_catalog.listCopyRows(query);
        if (!rows) {
            m_errors.show(parent, rows.error());
            return Core::asStatus(rows);
        }
        for (const Repositories::BookCopyRecord& copy : rows.value()) {
            if (copy.id == copyId) {
                found = copy;
                have = true;
                break;
            }
        }
        if (have || static_cast<int>(rows->size()) < query.limit) {
            break;
        }
    }
    if (!have || found.localId.empty()) {
        return Core::Status::ok();
    }

    if (!runReuseNumberFlow(parent, m_catalog, found)) {
        return Core::Status::ok();
    }
    m_events.emitChanged(Entity::Copies | Entity::Books, {copyId});
    return Core::Status::ok();
}

Core::Status ArchiveWorkflow::showLoans(QWidget* parent,
                                        ArchiveType type,
                                        qint64 id,
                                        const QString& displayName)
{
    if (id <= 0 || type == ArchiveType::Loans) {
        return Core::Status::ok();
    }

    Repositories::LoanQuery query;
    switch (type) {
    case ArchiveType::Members:
        query.memberId = id;
        break;
    case ArchiveType::Books:
        query.bookId = id;
        break;
    case ArchiveType::Copies:
        query.copyId = id;
        break;
    case ArchiveType::Loans:
        return Core::Status::ok();
    }
    ArchiveLoansDialog dialog(m_circulation, query, displayName, parent);
    dialog.exec();
    return Core::Status::ok();
}

}  // namespace VLMS
