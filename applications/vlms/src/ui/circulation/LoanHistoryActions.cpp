#include "ui/circulation/LoanHistoryActions.h"

#include <VLMS/Core/Strings.h>
#include "ui/UiHelpers.h"
#include "ui/workflows/LoanWorkflow.h"
#include "QtBridge.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QTableWidget>

using VLMS::T;

LoanHistoryActions::LoanHistoryActions(VLMS::Repositories::CirculationRepository& repository,
                                       VLMS::LoanWorkflow& loans,
                                       QTableWidget* table,
                                       QWidget* parent)
    : QWidget(parent),
      m_repository(repository),
      m_loans(loans),
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
    if (!m_loans.extend(this, selectedLoanId())) {
        return;
    }
    emit loansChanged();
}

void LoanHistoryActions::returnLoan() {
    if (!m_loans.returnLoan(this, selectedLoanId())) {
        return;
    }
    emit loansChanged();
}
