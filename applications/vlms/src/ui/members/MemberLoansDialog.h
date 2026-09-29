#pragma once

#include <VLMS/Repositories/CirculationRepository.h>

#include <QDialog>

class QLabel;
class QTableWidget;
class LoanHistoryActions;
namespace VLMS { class LoanWorkflow; }

class MemberLoansDialog final : public QDialog {
    Q_OBJECT

public:
    MemberLoansDialog(VLMS::Repositories::CirculationRepository& repository,
                      qint64 memberId,
                      const QString& memberName,
                      VLMS::LoanWorkflow& loans,
                      QWidget* parent = nullptr);

private:
    void buildUi();
    void retranslateUi();
    void refresh();
    void lendABook();
    QString loanStatusLabel(const VLMS::Repositories::LoanRecord& loan) const;

    VLMS::Repositories::CirculationRepository& m_repository;
    qint64 m_memberId = 0;
    QString m_memberName;
    VLMS::LoanWorkflow& m_loans;
    QTableWidget* m_table = nullptr;
    QLabel* m_emptyLabel = nullptr;
    LoanHistoryActions* m_actions = nullptr;
};
