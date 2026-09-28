#include "ui/archive/ArchiveLoansDialog.h"

#include "QtBridge.h"
#include "ui/TableHeaderSort.h"
#include "ui/UiHelpers.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVariant>
#include <QVBoxLayout>

using VLMS::T;
using VLMS::qs;

namespace {

constexpr int kHistoryLimit = 100000;

QString statusLabel(const VLMS::Repositories::LoanRecord& loan)
{
    if (!loan.returnedAt.empty()) {
        return T("circulation.status.returned");
    }
    return T(loan.isOverdue ? "circulation.status.overdue" : "circulation.status.open");
}

QString dashIfEmpty(const std::string& text)
{
    return text.empty() ? T("common.emDash") : qs(text);
}

}  // namespace

ArchiveLoansDialog::ArchiveLoansDialog(VLMS::Repositories::CirculationRepository& repository,
                                       const VLMS::Repositories::LoanQuery& query,
                                       const QString& name,
                                       QWidget* parent)
    : QDialog(parent),
      m_repository(repository),
      m_query(query)
{
    // History: an archived loan still happened, and a live one still counts.
    m_query.archive = VLMS::Repositories::ArchiveScope::Any;
    m_query.limit = kHistoryLimit;
    m_query.offset = 0;
    resize(960, 420);
    buildUi(name);
    refresh();
}

void ArchiveLoansDialog::buildUi(const QString& name)
{
    QString title = T("archive.loanHistoryTitle");
    title.replace(QStringLiteral("{name}"), name);
    setWindowTitle(title);

    auto* layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("archiveLoanHistory"));
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({
        T("circulation.col.member"),
        T("circulation.col.number"),
        T("archive.col.copy"),
        T("circulation.col.borrowed"),
        T("circulation.col.due"),
        T("loan.field.returnedAt"),
        T("circulation.col.status"),
        T("archive.col.archivedAt"),
    });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    for (const int column : {1, 3, 4, 5, 6}) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    m_table->verticalHeader()->setVisible(false);
    VLMS::enableWidgetTableSort(m_table);
    layout->addWidget(m_table);

    m_emptyLabel = new QLabel(T("archive.loanHistoryEmpty"), this);
    m_emptyLabel->setObjectName(QStringLiteral("loanHistoryEmpty"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setVisible(false);
    layout->addWidget(m_emptyLabel, 1);

    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    VLMS::localizeButtonBox(closeBox);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(closeBox);
}

void ArchiveLoansDialog::refresh()
{
    const auto loans = m_repository.listLoans(m_query);
    if (!loans) {
        VLMS::showRepoError(this, loans.error());
        return;
    }
    const auto& rows = loans.value();
    m_table->setRowCount(static_cast<int>(rows.size()));
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const VLMS::Repositories::LoanRecord& loan = rows.at(static_cast<std::size_t>(row));
        auto* member = new QTableWidgetItem(qs(loan.memberName));
        member->setData(Qt::UserRole, QVariant::fromValue(loan.id));
        m_table->setItem(row, 0, member);
        m_table->setItem(row, 1, new QTableWidgetItem(qs(loan.membershipNumber)));
        m_table->setItem(row, 2, new QTableWidgetItem(qs(loan.copyCode) + QStringLiteral(" / ")
                                                      + qs(loan.bookTitle)));
        m_table->setItem(row, 3, new QTableWidgetItem(qs(loan.borrowedAt)));
        m_table->setItem(row, 4, new QTableWidgetItem(qs(loan.dueAt)));
        m_table->setItem(row, 5, new QTableWidgetItem(dashIfEmpty(loan.returnedAt)));
        m_table->setItem(row, 6, new QTableWidgetItem(statusLabel(loan)));
        m_table->setItem(row, 7, new QTableWidgetItem(dashIfEmpty(loan.archivedAt)));
    }

    const bool empty = rows.empty();
    m_table->setVisible(!empty);
    m_emptyLabel->setVisible(empty);
}
