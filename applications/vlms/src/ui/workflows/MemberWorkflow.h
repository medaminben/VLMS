#pragma once

#include <VLMS/Core/Result.h>

#include <QList>
#include <QString>

#include <cstdint>

class QWidget;

namespace VLMS {

class BulkRunner;
class DataEvents;
class LoanListSpec;
class LoanWorkflow;
class DialogFactory;
class ErrorPresenter;
class Navigator;

namespace Repositories {
class MemberRepository;
}  // namespace Repositories

class MemberWorkflow {
public:
    MemberWorkflow(Repositories::MemberRepository& members,
                   DialogFactory& dialogs,
                   ErrorPresenter& errors,
                   DataEvents& events,
                   Navigator& navigator,
                   BulkRunner& bulk,
                   LoanWorkflow& loans,
                   LoanListSpec& loanHistorySpec);

    Core::Status add(QWidget* parent);
    Core::Status edit(QWidget* parent, qint64 memberId);
    Core::Status archive(QWidget* parent, const QList<qint64>& ids, const QList<QString>& labels);
    Core::Status showLoans(QWidget* parent, qint64 memberId);

private:
    Repositories::MemberRepository& m_members;
    DialogFactory& m_dialogs;
    ErrorPresenter& m_errors;
    DataEvents& m_events;
    Navigator& m_navigator;
    BulkRunner& m_bulk;
    LoanWorkflow& m_loanWorkflow;
    LoanListSpec& m_loanHistorySpec;
};

}  // namespace VLMS
