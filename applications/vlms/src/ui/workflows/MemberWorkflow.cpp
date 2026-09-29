#include "ui/workflows/MemberWorkflow.h"

#include "QtBridge.h"
#include "ui/members/MemberEditorDialog.h"
#include "ui/members/MemberLoansDialog.h"
#include "ui/workflows/BulkRunner.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/DialogFactory.h"
#include "ui/workflows/ErrorPresenter.h"
#include "ui/workflows/Navigator.h"
#include "ui/UiHelpers.h"

#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QWidget>

using VLMS::T;
using VLMS::qs;
using VLMS::ss;

namespace VLMS {

MemberWorkflow::MemberWorkflow(Repositories::MemberRepository& members,
                               Repositories::CirculationRepository& circulation,
                               DialogFactory& dialogs,
                               ErrorPresenter& errors,
                               DataEvents& events,
                               Navigator& navigator,
                               BulkRunner& bulk)
    : m_members(members)
    , m_circulation(circulation)
    , m_dialogs(dialogs)
    , m_errors(errors)
    , m_events(events)
    , m_navigator(navigator)
    , m_bulk(bulk)
{
    (void)m_dialogs;
}

Core::Status MemberWorkflow::add(QWidget* parent)
{
    MemberEditorDialog dialog(m_members, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return Core::Status::ok();
    }

    Repositories::MemberWrite write;
    write.member = dialog.memberInput();
    if (dialog.photoChanged()) {
        write.photoSourcePath = ss(dialog.photoSourcePath());
    }
    if (dialog.idImageChanged()) {
        write.idImageSourcePath = ss(dialog.idImageSourcePath());
    }
    write.clearPhoto = dialog.photoRemoved();
    write.clearIdImage = dialog.idImageRemoved();
    const auto created = m_members.saveNewMember(write);
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
        showInformation(parent, T("members.editMember"), T("members.selectMemberFirst"));
        return Core::Status::ok();
    }

    const auto member = m_members.getMember(memberId);
    if (!member) {
        m_errors.show(parent, member.error());
        return Core::asStatus(member);
    }

    MemberEditorDialog dialog(m_members, member.value(), parent);
    if (dialog.exec() != QDialog::Accepted) {
        return Core::Status::ok();
    }

    Repositories::MemberWrite write;
    write.member = dialog.memberInput();
    if (dialog.photoChanged()) {
        write.photoSourcePath = ss(dialog.photoSourcePath());
    }
    if (dialog.idImageChanged()) {
        write.idImageSourcePath = ss(dialog.idImageSourcePath());
    }
    write.clearPhoto = dialog.photoRemoved();
    write.clearIdImage = dialog.idImageRemoved();
    const auto saved = m_members.saveExistingMember(memberId, write);
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
        showInformation(parent, T("members.deleteMember"), T("members.selectMemberFirst"));
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
            if (showWarningWithAction(parent, title, T("members.deleteBlockedByLoans"),
                                      T("members.goToLoans"))) {
                const auto member = m_members.getMember(memberId);
                m_navigator.showMemberLoans(member ? qs(member->membershipNumber) : QString());
            }
            return Core::Status::ok();
        }
        if (!askYesNo(parent, title, T("members.deleteConfirm"))) {
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
    const QString fullName = qs(member->fullName.empty()
                                    ? member->firstName + " " + member->lastName
                                    : member->fullName);
    MemberLoansDialog dialog(m_circulation, memberId, fullName, parent);
    dialog.exec();
    return Core::Status::ok();
}

}  // namespace VLMS
