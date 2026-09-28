#include "ui/members/MemberLoansDialog.h"

#include <VLMS/Core/Strings.h>
#include "ui/TableHeaderSort.h"
#include "ui/UiHelpers.h"
#include "ui/circulation/LoanCheckoutDialog.h"
#include "ui/circulation/LoanHistoryActions.h"
#include "QtBridge.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVariant>
#include <QVBoxLayout>

using VLMS::T;
using VLMS::cd;
using VLMS::qd;
using VLMS::qs;
using VLMS::qsl;
using VLMS::ss;
using VLMS::svl;

namespace {

using VLMS::Strings;

}  // namespace

MemberLoansDialog::MemberLoansDialog(VLMS::Repositories::CirculationRepository& repository,
                                     const qint64 memberId,
                                     const QString& memberName,
                                     QWidget* parent)
    : QDialog(parent),
      m_repository(repository),
      m_memberId(memberId),
      m_memberName(memberName) {
    resize(820, 420);
    buildUi();
    retranslateUi();
    refresh();
}

void MemberLoansDialog::buildUi() {
    auto* layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    // Every column but the stretched first and last is as wide as its widest
    // entry, header included: at Qt's default 100px «تاريخ الإرجاع» was clipped.
    for (int column = 1; column < m_table->columnCount() - 1; ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    m_table->verticalHeader()->setVisible(false);
    VLMS::enableWidgetTableSort(m_table);
    layout->addWidget(m_table);

    // A member who has borrowed nothing is a member about to: the dialog says
    // so rather than showing an empty grid, and Loan is live beside it.
    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setObjectName(QStringLiteral("loanHistoryEmpty"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setVisible(false);
    layout->addWidget(m_emptyLabel, 1);

    m_actions = new LoanHistoryActions(m_repository, m_table, this);
    connect(m_actions, &LoanHistoryActions::loanRequested, this, &MemberLoansDialog::lendABook);
    connect(m_actions, &LoanHistoryActions::loansChanged, this, &MemberLoansDialog::refresh);

    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    VLMS::localizeButtonBox(closeBox);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* footer = new QHBoxLayout();
    footer->addWidget(m_actions, 1);
    footer->addWidget(closeBox, 0);
    layout->addLayout(footer);
}

void MemberLoansDialog::retranslateUi() {
    QString title = T("members.loanHistoryTitle");
    title.replace(QStringLiteral("{name}"), m_memberName);
    setWindowTitle(title);

    VLMS::retranslateStandardButtons(findChild<QDialogButtonBox*>());

    m_table->setHorizontalHeaderLabels({
        T("circulation.col.title"),
        T("loan.field.copy"),
        T("circulation.col.borrowed"),
        T("circulation.col.due"),
        T("loan.field.returnedAt"),
        T("circulation.col.status"),
    });

    m_emptyLabel->setText(T("members.loanHistoryEmpty"));
    if (m_actions != nullptr) {
        m_actions->retranslateUi();
    }
}

QString MemberLoansDialog::loanStatusLabel(const VLMS::Repositories::LoanRecord& loan) const {
    if (!loan.returnedAt.empty()) {
        return T("circulation.status.returned");
    }
    if (loan.isOverdue) {
        return T("circulation.status.overdue");
    }
    return T("circulation.status.open");
}

void MemberLoansDialog::refresh() {
    VLMS::Repositories::LoanQuery query;
    query.memberId = m_memberId;
    query.archive = VLMS::Repositories::ArchiveScope::Any;  // history: an archived loan still happened
    query.limit = 1000;
    query.offset = 0;

    const auto loansResult = m_repository.listLoans(query);
    if (!loansResult) {
        VLMS::showRepoError(this, loansResult.error());
        return;
    }
    const auto& loans = loansResult.value();
    m_table->setRowCount(loans.size());

    for (int row = 0; row < static_cast<int>(loans.size()); ++row) {
        const VLMS::Repositories::LoanRecord& loan = loans.at(row);
        auto* titleItem = new QTableWidgetItem(qs(loan.bookTitle));
        titleItem->setData(Qt::UserRole, QVariant::fromValue(loan.id));
        m_table->setItem(row, 0, titleItem);
        m_table->setItem(row, 1, new QTableWidgetItem(qs(loan.copyCode)));
        m_table->setItem(row, 2, new QTableWidgetItem(qs(loan.borrowedAt)));
        m_table->setItem(row, 3, new QTableWidgetItem(qs(loan.dueAt)));
        m_table->setItem(
            row,
            4,
            new QTableWidgetItem(loan.returnedAt.empty()
                                     ? T("common.emDash")
                                     : qs(loan.returnedAt)));
        m_table->setItem(row, 5, new QTableWidgetItem(loanStatusLabel(loan)));
    }

    const bool empty = loans.empty();
    m_table->setVisible(!empty);
    m_emptyLabel->setVisible(empty);
    m_actions->updateEnabled();
}

void MemberLoansDialog::lendABook() {
    LoanScope scope;
    scope.memberId = m_memberId;
    scope.label = m_memberName;

    LoanCheckoutDialog dialog(m_repository, scope, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    if (const auto created = m_repository.createLoan(dialog.loanInput()); !created) {
        VLMS::showRepoError(this, created.error());
        return;
    }
    refresh();
}
