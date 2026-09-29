#include "ui/workflows/MemberWorkflow.h"

#include "QtBridge.h"
#include "ui/workflows/BulkRunner.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/DialogFactory.h"
#include "ui/workflows/ErrorPresenter.h"
#include "ui/shell/Navigator.h"

#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QWidget>

using VLMS::T;
using VLMS::qs;
using VLMS::ss;

namespace VLMS {
namespace {

Repositories::MemberWrite writeFor(const MemberForm& form)
{
    Repositories::MemberWrite write;
    write.member = form.member;
    if (form.photoChanged) {
        write.photoSourcePath = ss(form.photoSourcePath);
    }
    if (form.idImageChanged) {
        write.idImageSourcePath = ss(form.idImageSourcePath);
    }
    write.clearPhoto = form.photoRemoved;
    write.clearIdImage = form.idImageRemoved;
    return write;
}

}  // namespace

MemberWorkflow::MemberWorkflow(Repositories::MemberRepository& members,
                               DialogFactory& dialogs,
                               ErrorPresenter& errors,
                               DataEvents& events,
                               Navigator& navigator,
                               BulkRunner& bulk,
                               LoanWorkflow& loans,
                               LoanListSpec& loanHistorySpec)
    : m_members(members)
    , m_dialogs(dialogs)
    , m_errors(errors)
    , m_events(events)
    , m_navigator(navigator)
    , m_bulk(bulk)
    , m_loanWorkflow(loans)
    , m_loanHistorySpec(loanHistorySpec)
{
}

Core::Status MemberWorkflow::add(QWidget* parent)
{
    const auto form = m_dialogs.editMember(parent, std::nullopt);
    if (!form) {
        return Core::Status::ok();
    }
    const auto created = m_members.saveNewMember(writeFor(*form));
    if (!created) {
        m_errors.show(parent, created.error());
        return Core::asStatus(created);
    }
    m_events.emitChanged(Entity::Members, {created.value()});
    return Core::Status::ok();
}

Core::Status MemberWorkflow::edit(QWidget* parent, qint64 memberId)
{
    if (memberId <= 0) {
        m_dialogs.inform(parent, T("members.editMember"), T("members.selectMemberFirst"));
        return Core::Status::ok();
    }
    const auto member = m_members.getMember(memberId);
    if (!member) {
        m_errors.show(parent, member.error());
        return Core::asStatus(member);
    }
    const auto form = m_dialogs.editMember(parent, member.value());
    if (!form) {
        return Core::Status::ok();
    }
    const auto saved = m_members.saveExistingMember(memberId, writeFor(*form));
    if (!saved) {
        m_errors.show(parent, saved.error());
        return saved;
    }
    m_events.emitChanged(Entity::Members, {memberId});
    return Core::Status::ok();
}

Core::Status MemberWorkflow::archive(QWidget* parent,
                                     const QList<qint64>& ids,
                                     const QList<QString>& labels)
{
    if (ids.isEmpty()) {
        m_dialogs.inform(parent, T("members.deleteMember"), T("members.selectMemberFirst"));
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
        const qint64 memberId = rows.first().id;
        const QString title = T("members.deleteMember");
        const auto block = m_members.removalBlock(memberId);
        if (!block) {
            m_errors.show(parent, block.error());
            return Core::asStatus(block);
        }
        if (block.value() == Repositories::MemberRepository::MemberRemovalBlock::OpenLoans) {
            if (m_dialogs.warnWithAction(parent, title, T("members.deleteBlockedByLoans"),
                                         T("members.goToLoans"))) {
                const auto member = m_members.getMember(memberId);
                m_navigator.showMemberLoans(member ? qs(member->membershipNumber) : QString());
            }
            return Core::Status::ok();
        }
        if (!m_dialogs.askYesNo(parent, title, T("members.deleteConfirm"))) {
            return Core::Status::ok();
        }
        const auto archived = m_members.archiveMember(memberId);
        if (!archived) {
            m_errors.show(parent, archived.error());
            return archived;
        }
        m_events.emitChanged(Entity::Members, {memberId});
        return Core::Status::ok();
    }

    BulkActionTexts texts;
    texts.title = T("members.deleteMember");
    texts.verb = T("bulk.verb.archive");
    texts.passive = T("bulk.passive.archive");
    texts.noun = T("bulk.noun.members");
    m_bulk.run(
        parent, texts, rows,
        [this](const BulkRow& bulk) { return m_members.canArchiveMember(bulk.id); },
        [this](const BulkRow& bulk) { return m_members.archiveMember(bulk.id); },
        Entity::Members);
    return Core::Status::ok();
}

Core::Status MemberWorkflow::showLoans(QWidget* parent, qint64 memberId)
{
    if (memberId <= 0) {
        return Core::Status::ok();
    }
    const auto member = m_members.getMember(memberId);
    if (!member) {
        m_errors.show(parent, member.error());
        return Core::asStatus(member);
    }
    LoanHistoryRequest request;
    request.kind = LoanHistoryKind::Member;
    request.id = memberId;
    request.title = qs(member->fullName.empty() ? member->firstName + " " + member->lastName
                                                : member->fullName);
    request.spec = &m_loanHistorySpec;
    request.loans = &m_loanWorkflow;
    request.events = &m_events;
    m_dialogs.showLoanHistory(parent, request);
    return Core::Status::ok();
}

}  // namespace VLMS
