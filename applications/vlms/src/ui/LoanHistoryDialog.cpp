#include "ui/LoanHistoryDialog.h"

#include "QtBridge.h"
#include "ui/UiHelpers.h"
#include "ui/presentation/LoanListSpec.h"
#include "ui/presentation/PagedQueryModel.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/LoanWorkflow.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

using VLMS::T;
using VLMS::ss;

namespace VLMS {

LoanHistoryDialog::LoanHistoryDialog(LoanListSpec& loansSpec,
                                     LoanWorkflow& loans,
                                     DataEvents& events,
                                     const Kind kind,
                                     const qint64 pinnedId,
                                     const QString& title,
                                     const Repositories::ArchiveScope scope,
                                     QWidget* parent)
    : QDialog(parent)
    , m_loansSpec(loansSpec)
    , m_loans(loans)
    , m_events(events)
    , m_kind(kind)
    , m_pinnedId(pinnedId)
    , m_titleName(title)
    , m_scope(scope)
{
    resize(mutableKind() ? 900 : 960, 420);
    buildUi();
    retranslateUi();
    applyPinnedQuery();
    updateEmptyState();
    updateActionEnabled();
}

const QueryState& LoanHistoryDialog::queryState() const
{
    return m_model->query();
}

PagedQueryModel* LoanHistoryDialog::model() const
{
    return m_model;
}

QTableView* LoanHistoryDialog::table() const
{
    return m_table;
}

bool LoanHistoryDialog::mutableKind() const
{
    return m_kind == Kind::Book || m_kind == Kind::Member;
}

QString LoanHistoryDialog::titleKey() const
{
    switch (m_kind) {
    case Kind::Book:
        return QStringLiteral("catalog.loanHistoryTitle");
    case Kind::Member:
        return QStringLiteral("members.loanHistoryTitle");
    case Kind::ArchivedMember:
    case Kind::ArchivedBook:
    case Kind::ArchivedCopy:
        return QStringLiteral("archive.loanHistoryTitle");
    }
    return QStringLiteral("archive.loanHistoryTitle");
}

QString LoanHistoryDialog::emptyKey() const
{
    switch (m_kind) {
    case Kind::Book:
        return QStringLiteral("catalog.loanHistoryEmpty");
    case Kind::Member:
        return QStringLiteral("members.loanHistoryEmpty");
    case Kind::ArchivedMember:
    case Kind::ArchivedBook:
    case Kind::ArchivedCopy:
        return QStringLiteral("archive.loanHistoryEmpty");
    }
    return QStringLiteral("archive.loanHistoryEmpty");
}

void LoanHistoryDialog::buildUi()
{
    auto* layout = new QVBoxLayout(this);

    m_model = new PagedQueryModel(&m_loansSpec, m_events, this);
    m_table = new QTableView(this);
    m_table->setObjectName(QStringLiteral("loanHistoryTable"));
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(m_table);

    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setObjectName(QStringLiteral("loanHistoryEmpty"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setVisible(false);
    layout->addWidget(m_emptyLabel, 1);

    auto* footer = new QHBoxLayout();
    if (mutableKind()) {
        m_checkoutButton = makePrimaryButton({});
        m_extendButton = makeSecondaryButton({});
        m_returnButton = makeSecondaryButton({});
        footer->addWidget(m_checkoutButton);
        footer->addWidget(m_extendButton);
        footer->addWidget(m_returnButton);
        footer->addStretch(1);
        connect(m_checkoutButton, &QPushButton::clicked, this, &LoanHistoryDialog::checkout);
        connect(m_extendButton, &QPushButton::clicked, this, &LoanHistoryDialog::extendLoan);
        connect(m_returnButton, &QPushButton::clicked, this, &LoanHistoryDialog::returnLoan);
        connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                &LoanHistoryDialog::updateActionEnabled);
    }

    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    localizeButtonBox(closeBox);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    footer->addWidget(closeBox, 0);
    layout->addLayout(footer);

    connect(m_model, &PagedQueryModel::loaded, this, [this](int) {
        updateEmptyState();
        updateActionEnabled();
    });
}

void LoanHistoryDialog::retranslateUi()
{
    QString title = T(ss(titleKey()));
    title.replace(QStringLiteral("{name}"), m_titleName);
    setWindowTitle(title);

    m_emptyLabel->setText(T(ss(emptyKey())));
    retranslateStandardButtons(findChild<QDialogButtonBox*>());

    if (m_checkoutButton != nullptr) {
        m_checkoutButton->setText(T("circulation.checkout"));
        m_extendButton->setText(T("circulation.extend"));
        m_returnButton->setText(T("circulation.return"));
    }
}

void LoanHistoryDialog::applyPinnedQuery()
{
    QueryState state;
    state.scope = m_scope;
    state.pageSize = 0;
    state.page = 1;

    LoanScope pin;
    switch (m_kind) {
    case Kind::Book:
    case Kind::ArchivedBook:
        pin.bookId = m_pinnedId;
        break;
    case Kind::Member:
    case Kind::ArchivedMember:
        pin.memberId = m_pinnedId;
        break;
    case Kind::ArchivedCopy:
        pin.copyId = m_pinnedId;
        break;
    }
    state.pinned = pin;
    m_model->setQuery(state);
}

void LoanHistoryDialog::updateEmptyState()
{
    const bool empty = m_model == nullptr || m_model->rowCount() == 0;
    m_table->setVisible(!empty);
    m_emptyLabel->setVisible(empty);
}

qint64 LoanHistoryDialog::selectedLoanId() const
{
    if (m_table == nullptr || m_table->selectionModel() == nullptr) {
        return 0;
    }
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return 0;
    }
    return m_loansSpec.idAt(rows.first().row());
}

bool LoanHistoryDialog::selectedLoanIsOpen() const
{
    if (m_table == nullptr || m_table->selectionModel() == nullptr || m_model == nullptr) {
        return false;
    }
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return false;
    }
    const int row = rows.first().row();
    // status is the last LoanListSpec column
    const QVariant status = m_model->index(row, m_model->columnCount() - 1).data(Qt::DisplayRole);
    return status.toString() != T("circulation.status.returned");
}

void LoanHistoryDialog::updateActionEnabled()
{
    if (!mutableKind()) {
        return;
    }
    const bool open = selectedLoanIsOpen();
    m_extendButton->setEnabled(open);
    m_returnButton->setEnabled(open);
}

void LoanHistoryDialog::checkout()
{
    LoanCheckoutPrefill prefill;
    if (m_kind == Kind::Book) {
        prefill.bookId = m_pinnedId;
    } else if (m_kind == Kind::Member) {
        prefill.memberId = m_pinnedId;
    }
    m_loans.checkout(this, prefill);
    m_model->reload();
    updateEmptyState();
    updateActionEnabled();
}

void LoanHistoryDialog::extendLoan()
{
    if (!m_loans.extend(this, selectedLoanId())) {
        return;
    }
    m_model->reload();
    updateEmptyState();
    updateActionEnabled();
}

void LoanHistoryDialog::returnLoan()
{
    if (!m_loans.returnLoan(this, selectedLoanId())) {
        return;
    }
    m_model->reload();
    updateEmptyState();
    updateActionEnabled();
}

}  // namespace VLMS
