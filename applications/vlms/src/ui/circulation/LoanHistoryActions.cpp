#include "ui/circulation/LoanHistoryActions.h"

#include <VLMS/Core/Strings.h>
#include "ui/UiHelpers.h"
#include "ui/circulation/LoanExtendDialog.h"
#include "ui/circulation/LoanReturnDialog.h"
#include "QtBridge.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QTableWidget>

using VLMS::T;
using VLMS::ss;

LoanHistoryActions::LoanHistoryActions(CirculationRepository& repository,
                                       QTableWidget* table,
                                       QWidget* parent)
    : QWidget(parent),
      m_repository(repository),
      m_table(table) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_loanButton = VLMS::makePrimaryButton({});
    m_extendButton = VLMS::makeSecondaryButton({});
    m_returnButton = VLMS::makeSecondaryButton({});
    layout->addWidget(m_loanButton);
    layout->addWidget(m_extendButton);
    layout->addWidget(m_returnButton);
    layout->addStretch(1);

    connect(m_loanButton, &QPushButton::clicked, this, &LoanHistoryActions::loanRequested);
    connect(m_extendButton, &QPushButton::clicked, this, &LoanHistoryActions::extendLoan);
    connect(m_returnButton, &QPushButton::clicked, this, &LoanHistoryActions::returnLoan);

    if (m_table != nullptr) {
        connect(m_table, &QTableWidget::itemSelectionChanged,
                this, &LoanHistoryActions::updateEnabled);
    }
    retranslateUi();
    updateEnabled();
}

void LoanHistoryActions::retranslateUi() {
    m_loanButton->setText(T("circulation.checkout"));
    m_extendButton->setText(T("circulation.extend"));
    m_returnButton->setText(T("circulation.return"));
}

qint64 LoanHistoryActions::selectedLoanId() const {
    if (m_table == nullptr) {
        return 0;
    }
    const int row = m_table->currentRow();
    if (row < 0 || m_table->item(row, 0) == nullptr) {
        return 0;
    }
    return m_table->item(row, 0)->data(Qt::UserRole).toLongLong();
}

void LoanHistoryActions::updateEnabled() {
    // Extend and Return only mean something for a loan still out. The Status
    // column already says why they are grey, so no warning box is needed.
    const qint64 loanId = selectedLoanId();
    bool stillOut = false;
    if (loanId > 0) {
        if (const auto loan = m_repository.getLoan(loanId); loan.has_value()) {
            stillOut = loan->returnedAt.empty();
        }
    }
    m_extendButton->setEnabled(stillOut);
    m_returnButton->setEnabled(stillOut);
}

void LoanHistoryActions::extendLoan() {
    const qint64 loanId = selectedLoanId();
    if (loanId <= 0) {
        return;
    }
    const auto loan = m_repository.getLoan(loanId);
    if (!loan) {
        VLMS::showRepoError(this, loan.error());
        return;
    }

    LoanExtendDialog dialog(loan.value(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (const auto extended = m_repository.extendLoan(loanId, ss(dialog.dueAt())); !extended) {
        VLMS::showRepoError(this, extended.error());
        return;
    }
    emit loansChanged();
}

void LoanHistoryActions::returnLoan() {
    const qint64 loanId = selectedLoanId();
    if (loanId <= 0) {
        return;
    }
    const auto loan = m_repository.getLoan(loanId);
    if (!loan) {
        VLMS::showRepoError(this, loan.error());
        return;
    }

    LoanReturnDialog dialog(loan.value(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (const auto returned =
            m_repository.returnLoan(loanId, ss(dialog.returnedAt()), ss(dialog.notes()));
        !returned) {
        VLMS::showRepoError(this, returned.error());
        return;
    }
    emit loansChanged();
}
