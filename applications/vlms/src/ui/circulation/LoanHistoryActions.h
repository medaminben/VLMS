#pragma once

#include <VLMS/Repositories/CirculationRepository.h>

#include <QWidget>

class QPushButton;
class QTableWidget;

/// The Loan / Extend / Return row both loan-history dialogs carry. Extend and
/// Return follow the table's selected row and are disabled unless that row is
/// a loan still out; Loan means something different on each page, so it leaves
/// here as a signal rather than as a method.
class LoanHistoryActions final : public QWidget {
    Q_OBJECT

public:
    LoanHistoryActions(VLMS::Repositories::CirculationRepository& repository,
                       QTableWidget* table,
                       QWidget* parent = nullptr);

    void retranslateUi();
    void updateEnabled();

signals:
    void loanRequested();
    void loansChanged();

private:
    void extendLoan();
    void returnLoan();
    [[nodiscard]] qint64 selectedLoanId() const;

    VLMS::Repositories::CirculationRepository& m_repository;
    QTableWidget* m_table = nullptr;
    QPushButton* m_loanButton = nullptr;
    QPushButton* m_extendButton = nullptr;
    QPushButton* m_returnButton = nullptr;
};
