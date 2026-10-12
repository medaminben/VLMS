#include "ui/views/LoanHistoryDialog.h"

#include "QtBridge.h"
#include "ui/views/ActionBar.h"
#include "ui/views/UiHelpers.h"
#include "ui/presentation/LoanListSpec.h"
#include "ui/presentation/PagedQueryModel.h"
#include "ui/workflows/DataEvents.h"
#include "ui/workflows/LoanWorkflow.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
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
    applyLabels();
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
        // The same action bar as every list page: buttons, context menu and
        // shortcuts from one list, enabled by each action's own rule.
        auto* actions = new QHBoxLayout();
        actions->setSpacing(8);
        footer->addLayout(actions);
        footer->addStretch(1);
        m_actionBar = new ActionBar(this, m_table, actions, this);
        m_actionBar->setContextProvider([this] { return currentContext(); });
        m_actionBar->setRunner([this](const PageAction& action, const ActionContext& ctx) {
            if (action.run) {
                (void)action.run(ctx);
            }
            m_model->reload();
            updateEmptyState();
        });
        m_actionBar->setActions(historyActions());
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

void LoanHistoryDialog::changeEvent(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        applyLabels();
    }
    QDialog::changeEvent(event);
}

void LoanHistoryDialog::applyLabels()
{
    QString title = T(ss(titleKey()));
    title.replace(QStringLiteral("{name}"), m_titleName);
    setWindowTitle(title);

    m_emptyLabel->setText(T(ss(emptyKey())));
    retranslateStandardButtons(findChild<QDialogButtonBox*>());

    if (m_actionBar != nullptr) {
        m_actionBar->retranslate();
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

void LoanHistoryDialog::updateActionEnabled()
{
    if (m_actionBar != nullptr) {
        m_actionBar->refreshEnabled();
    }
}

QList<PageAction> LoanHistoryDialog::historyActions()
{
    // The history offers what it always offered: checkout, extend, return.
    // Checkout starts from the book or member whose history this is.
    QList<PageAction> kept;
    for (PageAction action : m_loansSpec.actions()) {
        if (action.id == QStringLiteral("checkout")) {
            action.run = [this](const ActionContext& ctx) {
                LoanCheckoutPrefill prefill;
                if (m_kind == Kind::Book) {
                    prefill.bookId = m_pinnedId;
                } else if (m_kind == Kind::Member) {
                    prefill.memberId = m_pinnedId;
                }
                return m_loans.checkout(ctx.parent, prefill);
            };
            kept.append(action);
        } else if (action.id == QStringLiteral("extend")
                   || action.id == QStringLiteral("return")) {
            kept.append(action);
        }
    }
    return kept;
}

ActionContext LoanHistoryDialog::currentContext()
{
    ActionContext ctx;
    ctx.parent = this;
    if (const qint64 id = selectedLoanId(); id > 0) {
        ctx.ids.append(id);
    }
    return ctx;
}

}  // namespace VLMS
