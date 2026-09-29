#include "ui/workflows/LoanWorkflow.h"

#include "QtBridge.h"
#include "ui/BulkAction.h"
#include "ui/circulation/LoanCheckoutDialog.h"
#include "ui/circulation/LoanExtendDialog.h"
#include "ui/circulation/LoanReturnDialog.h"
#include "ui/presentation/CirculationLookups.h"
#include "ui/workflows/BulkRunner.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/DialogFactory.h"
#include "ui/workflows/ErrorPresenter.h"
#include "ui/workflows/Navigator.h"
#include "ui/UiHelpers.h"

#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CirculationRepository.h>

#include <QWidget>

using VLMS::T;
using VLMS::ss;

namespace VLMS {

LoanWorkflow::LoanWorkflow(Repositories::CirculationRepository& circulation,
                           CirculationLookups& lookups,
                           DialogFactory& dialogs,
                           ErrorPresenter& errors,
                           DataEvents& events,
                           Navigator& navigator,
                           BulkRunner& bulk)
    : m_circulation(circulation)
    , m_lookups(lookups)
    , m_dialogs(dialogs)
    , m_errors(errors)
    , m_events(events)
    , m_navigator(navigator)
    , m_bulk(bulk)
{
    (void)m_dialogs;
    (void)m_navigator;
}

Core::Status LoanWorkflow::checkout(QWidget* parent, const LoanCheckoutPrefill& prefill)
{
    LoanScope scope;
    scope.bookId = prefill.bookId;
    scope.memberId = prefill.memberId;
    LoanCheckoutDialog dialog(m_lookups, scope, parent);
    if (!dialog.hasLendableCopy()) {
        return Core::Status::ok();
    }
    if (dialog.exec() != QDialog::Accepted) {
        return Core::Status::ok();
    }
    const auto created = m_circulation.createLoan(dialog.loanInput());
    if (!created) {
        m_errors.show(parent, created.error());
        return Core::asStatus(created);
    }
    m_events.emitChanged(Entity::Loans | Entity::Copies, {created.value()});
    return Core::Status::ok();
}

Core::Status LoanWorkflow::returnLoan(QWidget* parent, qint64 loanId)
{
    if (loanId <= 0) {
        showInformation(parent, T("circulation.return"), T("circulation.selectLoanFirst"));
        return Core::Status::ok();
    }
    const auto loan = m_circulation.getLoan(loanId);
    if (!loan) {
        m_errors.show(parent, loan.error());
        return Core::asStatus(loan);
    }
    if (!loan->returnedAt.empty()) {
        showInformation(parent, T("circulation.return"), T("circulation.alreadyReturned"));
        return Core::Status::ok();
    }
    LoanReturnDialog dialog(loan.value(), parent);
    if (dialog.exec() != QDialog::Accepted) {
        return Core::Status::ok();
    }
    const auto returned =
        m_circulation.returnLoan(loanId, ss(dialog.returnedAt()), ss(dialog.notes()));
    if (!returned) {
        m_errors.show(parent, returned.error());
        return returned;
    }
    m_events.emitChanged(Entity::Loans | Entity::Copies, {loanId});
    return Core::Status::ok();
}

Core::Status LoanWorkflow::extend(QWidget* parent, qint64 loanId)
{
    if (loanId <= 0) {
        showInformation(parent, T("circulation.extend"), T("circulation.selectLoanFirst"));
        return Core::Status::ok();
    }
    const auto loan = m_circulation.getLoan(loanId);
    if (!loan) {
        m_errors.show(parent, loan.error());
        return Core::asStatus(loan);
    }
    if (!loan->returnedAt.empty()) {
        showInformation(parent, T("circulation.extend"), T("circulation.alreadyReturned"));
        return Core::Status::ok();
    }
    LoanExtendDialog dialog(loan.value(), parent);
    if (dialog.exec() != QDialog::Accepted) {
        return Core::Status::ok();
    }
    const auto extended = m_circulation.extendLoan(loanId, ss(dialog.dueAt()));
    if (!extended) {
        m_errors.show(parent, extended.error());
        return extended;
    }
    m_events.emitChanged(Entity::Loans, {loanId});
    return Core::Status::ok();
}

Core::Status LoanWorkflow::archive(QWidget* parent,
                                   const QList<qint64>& ids,
                                   const QList<QString>& labels)
{
    if (ids.isEmpty()) {
        showInformation(parent, T("circulation.delete"), T("circulation.selectLoanFirst"));
        return Core::Status::ok();
    }

    QList<BulkRow> rows;
    for (int i = 0; i < ids.size(); ++i) {
        BulkRow row;
        row.id = ids.at(i);
        row.row = i;
        row.label = i < labels.size() ? labels.at(i) : QString::number(ids.at(i));
        rows.append(row);
    }

    if (rows.size() == 1) {
        if (!askYesNo(parent, T("circulation.delete"), T("circulation.archiveLoan"))) {
            return Core::Status::ok();
        }
        const auto archived = m_circulation.archiveLoan(rows.first().id);
        if (!archived) {
            m_errors.show(parent, archived.error());
            return archived;
        }
        m_events.emitChanged(Entity::Loans, {rows.first().id});
        return Core::Status::ok();
    }

    BulkActionTexts texts;
    texts.title = T("circulation.delete");
    texts.verb = T("bulk.verb.archive");
    texts.passive = T("bulk.passive.archive");
    texts.noun = T("bulk.noun.loans");
    m_bulk.run(
        parent, texts, rows,
        [this](const BulkRow& bulk) { return m_circulation.canArchiveLoan(bulk.id); },
        [this](const BulkRow& bulk) { return m_circulation.archiveLoan(bulk.id); },
        Entity::Loans);
    return Core::Status::ok();
}

}  // namespace VLMS
