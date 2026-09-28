#include "ui/circulation/CirculationPage.h"

#include <VLMS/Core/Strings.h>
#include "ui/BulkAction.h"
#include "ui/ListPageFrame.h"
#include "ui/TableRowChecks.h"
#include "ui/TablePager.h"
#include "ui/PreviewImages.h"
#include "ui/members/MemberFacetFilters.h"
#include "ui/UiHelpers.h"
#include "QtBridge.h"
#include "ui/circulation/LoanExtendDialog.h"
#include "ui/circulation/LoanCheckoutDialog.h"
#include "ui/circulation/LoanReturnDialog.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QEvent>
#include <QShowEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPair>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
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

constexpr int kPreviewPanelMinWidth = 200;
constexpr int kTitleColumnWidth = 300;
constexpr int kMemberColumnWidth = 200;

bool isAllFilter(const QString& code) {
    return code.isEmpty() || code == QLatin1String(VLMS::Repositories::LoanFilter::kAll);
}

QStringList loanDetailLabelKeys() {
    return {
        QStringLiteral("loan.field.notes"),
        QStringLiteral("book.field.title"),
        QStringLiteral("book.field.author"),
        QStringLiteral("loan.field.member"),
        QStringLiteral("member.field.number"),
        QStringLiteral("loan.field.copy"),
        QStringLiteral("loan.field.borrowedAt"),
        QStringLiteral("loan.field.dueAt"),
        QStringLiteral("loan.field.returnedAt"),
        QStringLiteral("circulation.col.status"),
    };
}

}  // namespace

CirculationPage::CirculationPage(VLMS::Repositories::CirculationRepository& repository,
                                 VLMS::Repositories::CatalogRepository& catalogRepository,
                                 VLMS::Repositories::MemberRepository& memberRepository,
                                 QWidget* parent)
    : QWidget(parent),
      m_repository(repository),
      m_catalogRepository(catalogRepository),
      m_memberRepository(memberRepository) {
    buildUi();
    retranslateUi();
    refreshFilter();
    refreshLoans();
}

void CirculationPage::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* frame = new VLMS::ListPageFrame(this);
    VLMS::ListPageFrame::ListConfig config;
    config.subtitle = T("page.circulation.body");
    config.tableColumnCount = 6;
    config.tableColumnWidths = {kMemberColumnWidth, 130, kTitleColumnWidth, 110, 110, 110};
    config.imageObjectName = QStringLiteral("bookCover");
    config.imageBounds = VLMS::bookCoverPreviewBounds();
    config.secondImageObjectName = QStringLiteral("loanMemberPhoto");
    config.secondImageBounds = VLMS::bookCoverPreviewBounds();
    config.detailsScrollObjectName = QStringLiteral("loanDetailsScroll");
    config.previewPanelMinWidth = kPreviewPanelMinWidth;
    frame->buildList(config);
    rootLayout->addWidget(frame);

    m_filterList = new QListWidget(frame->filterColumn());
    m_filterList->setObjectName(QStringLiteral("loanFilter"));
    m_filterList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_filterList, &QListWidget::itemSelectionChanged, this, &CirculationPage::onFilterChanged);
    frame->addFilter(m_filterList, 0);
    // The borrower's filters, as on the Members page. Loans of members since
    // archived are still listed here, so the years and cities include them.
    m_memberFilters = new VLMS::MemberFacetFilters(
        m_memberRepository, VLMS::Repositories::ArchiveScope::Any, frame->filterColumn());
    // The year here is the year of the loan: a borrower's registration year
    // says nothing about the loans on this list.
    m_memberFilters->useLoanYears(
        [this]() { return m_repository.listLoanYears(VLMS::Repositories::ArchiveScope::Live); });
    connect(m_memberFilters, &VLMS::MemberFacetFilters::changed,
            this, &CirculationPage::resetPagerAndRefresh);
    frame->addFilter(m_memberFilters, 1);

    m_searchEdit = frame->searchEdit();
    connect(m_searchEdit, &QLineEdit::textChanged, this, &CirculationPage::onSearchChanged);

    // Return first, beside the search: a returning book is found by searching
    // for it, and the desk returns books far more often than it extends them.
    m_returnButton = VLMS::makeSecondaryButton({});
    m_checkoutButton = VLMS::makePrimaryButton({});
    m_extendButton = VLMS::makeSecondaryButton({});
    connect(m_checkoutButton, &QPushButton::clicked, this, &CirculationPage::checkoutLoan);
    connect(m_extendButton, &QPushButton::clicked, this, &CirculationPage::extendLoan);
    connect(m_returnButton, &QPushButton::clicked, this, &CirculationPage::returnLoan);
    m_deleteButton = VLMS::makeSecondaryButton({});
    connect(m_deleteButton, &QPushButton::clicked, this, &CirculationPage::archiveLoan);
    frame->addButton(m_returnButton);
    frame->addButton(m_checkoutButton);
    frame->addButton(m_extendButton);
    frame->addButton(m_deleteButton);

    m_loansTable = frame->table();
    m_checks = new TableRowChecks(m_loansTable, this);
    connect(m_checks, &TableRowChecks::checkedCountChanged, this,
            &CirculationPage::onSelectionChanged);
    connect(m_loansTable, &QTableWidget::itemSelectionChanged, this, &CirculationPage::onSelectionChanged);
    m_pager = frame->pager();
    connect(m_pager, &VLMS::TablePager::pageChanged, this, &CirculationPage::refreshLoans);

    m_sort = new VLMS::TableHeaderSort(m_loansTable, this);
    m_sort->setColumnKeys({
        QString::fromLatin1(VLMS::Repositories::LoanSort::kMember),
        QString::fromLatin1(VLMS::Repositories::LoanSort::kNumber),
        QString::fromLatin1(VLMS::Repositories::LoanSort::kTitle),
        QString::fromLatin1(VLMS::Repositories::LoanSort::kBorrowed),
        QString::fromLatin1(VLMS::Repositories::LoanSort::kDue),
        QString::fromLatin1(VLMS::Repositories::LoanSort::kStatus),
    });
    connect(m_sort, &VLMS::TableHeaderSort::sortChanged,
            this, &CirculationPage::onSortChanged);

    m_coverPreview = frame->imageLabel();
    m_photoPreview = frame->secondImageLabel();
    m_previewPanel = frame->previewPanel();
    m_detailsPanel = frame->detailsPanel();
    renderPreviewImages();

    auto* detailsLayout = qobject_cast<QVBoxLayout*>(m_detailsPanel->layout());
    const QStringList detailKeys = loanDetailLabelKeys();
    m_detailRows.reserve(detailKeys.size());
    for (const QString& key : detailKeys) {
        LoanDetailRow detail;
        detail.labelKey = key;

        auto* fieldWidget = new QWidget(m_detailsPanel);
        auto* fieldLayout = new QVBoxLayout(fieldWidget);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(2);

        detail.label = new QLabel(fieldWidget);
        detail.label->setObjectName(QStringLiteral("bookDetailLabel"));
        detail.label->setAlignment(Qt::AlignLeading);

        const bool scrollable = key == QStringLiteral("loan.field.notes");
        detail.value = VLMS::makeDetailValueWidget(fieldWidget, scrollable);

        fieldLayout->addWidget(detail.label);
        fieldLayout->addWidget(detail.value);

        detailsLayout->addWidget(fieldWidget);
        m_detailRows.append(detail);
    }
    detailsLayout->addStretch(1);

    m_previewPanel->installEventFilter(this);
}

void CirculationPage::retranslateUi() {
    if (auto* title = findChild<QLabel*>(QStringLiteral("pageTitle"))) {
        title->setText(T("page.circulation.title"));
    }
    if (auto* subtitle = findChild<QLabel*>(QStringLiteral("pageSubtitle"))) {
        subtitle->setText(T("page.circulation.body"));
    }

    m_searchEdit->setPlaceholderText(T("circulation.searchPlaceholder"));
    m_checkoutButton->setText(T("circulation.checkout"));
    m_extendButton->setText(T("circulation.extend"));
    m_returnButton->setText(T("circulation.return"));
    m_deleteButton->setText(T("circulation.delete"));
    m_pager->retranslateUi();

    m_loansTable->setHorizontalHeaderLabels({
        T("circulation.col.member"),
        T("circulation.col.number"),
        T("circulation.col.title"),
        T("circulation.col.borrowed"),
        T("circulation.col.due"),
        T("circulation.col.status"),
    });

    refreshFilter();
    m_memberFilters->refresh();

    const qint64 selectedId = selectedLoanId();
    refreshLoans();
    selectLoanId(selectedId);

    refreshSelectedLoanPreview();
}

QString CirculationPage::loanStatusLabel(const VLMS::Repositories::LoanRecord& loan) const {
    if (!loan.returnedAt.empty()) {
        return T("circulation.status.returned");
    }
    if (loan.isOverdue) {
        return T("circulation.status.overdue");
    }
    return T("circulation.status.open");
}

QStringList CirculationPage::selectedFilters() const {
    QStringList filters;
    const auto selected = m_filterList->selectedItems();
    for (const QListWidgetItem* item : selected) {
        const QString code = item->data(Qt::UserRole).toString();
        if (!code.isEmpty() && !isAllFilter(code)) {
            filters.append(code);
        }
    }
    return filters;
}

void CirculationPage::sizeFilterList() {
    if (m_filterList == nullptr || m_filterList->count() == 0) {
        return;
    }
    const int rowHeight = std::max(m_filterList->sizeHintForRow(0), 20);
    m_filterList->setFixedHeight(rowHeight * m_filterList->count() + 4);
    m_filterList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void CirculationPage::refreshFilter() {
    const QStringList previouslySelected = selectedFilters();
    bool hadAllSelected = false;
    for (int row = 0; row < m_filterList->count(); ++row) {
        QListWidgetItem* item = m_filterList->item(row);
        if (item->isSelected() && isAllFilter(item->data(Qt::UserRole).toString())) {
            hadAllSelected = true;
            break;
        }
    }

    const QSignalBlocker blocker(m_filterList);
    m_updatingFilter = true;

    m_filterList->clear();

    auto* allItem = new QListWidgetItem(T("circulation.filter.all"));
    allItem->setData(Qt::UserRole, QString());
    m_filterList->addItem(allItem);

    for (const std::string& code : VLMS::Repositories::CirculationRepository::filterCodes()) {
        if (code == VLMS::Repositories::LoanFilter::kAll) {
            continue;
        }
        auto* item = new QListWidgetItem(T("circulation.filter." + code));
        item->setData(Qt::UserRole, qs(code));
        m_filterList->addItem(item);
    }

    QListWidgetItem* toSelect = nullptr;
    if (hadAllSelected) {
        toSelect = allItem;
    } else if (!previouslySelected.isEmpty()) {
        for (int row = 1; row < m_filterList->count(); ++row) {
            QListWidgetItem* item = m_filterList->item(row);
            if (previouslySelected.contains(item->data(Qt::UserRole).toString())) {
                toSelect = item;
                break;
            }
        }
    }
    if (toSelect == nullptr) {
        // Default to All: on imported data every unreturned loan can be past
        // due, and an Open default would open the page on an empty list.
        toSelect = allItem;
    }
    if (toSelect != nullptr) {
        toSelect->setSelected(true);
    }

    sizeFilterList();
    m_updatingFilter = false;
}

void CirculationPage::resetPagerAndRefresh() {
    m_pager->resetToFirstPage();
    refreshLoans();
}

VLMS::Repositories::LoanQuery CirculationPage::currentLoanQuery() const {
    VLMS::Repositories::LoanQuery query;
    query.search = ss(m_searchEdit->text());
    query.filters = svl(selectedFilters());
    query.member = m_memberFilters->facets();
    query.loanYears = svl(m_memberFilters->loanYears());
    query.limit = m_pager->pageSize();
    query.offset = m_pager->offset();
    if (m_sort != nullptr && m_sort->isActive()) {
        query.sortColumn = ss(m_sort->columnKey());
        query.sortAscending = m_sort->ascending();
    }
    return query;
}

void CirculationPage::refreshLoans() {
    VLMS::Repositories::LoanQuery query = currentLoanQuery();
    const auto totalCount = m_repository.countLoans(query);
    if (!totalCount) {
        VLMS::showRepoError(this, totalCount.error());
        return;
    }
    m_pager->setTotalCount(totalCount.value());

    query.limit = m_pager->pageSize();
    query.offset = m_pager->offset();
    const auto loansResult = m_repository.listLoans(query);
    if (!loansResult) {
        VLMS::showRepoError(this, loansResult.error());
        return;
    }
    const auto& loans = loansResult.value();
    m_loansTable->setRowCount(loans.size());

    for (int row = 0; row < static_cast<int>(loans.size()); ++row) {
        const VLMS::Repositories::LoanRecord& loan = loans.at(row);

        auto* memberItem = new QTableWidgetItem(qs(loan.memberName));
        memberItem->setData(Qt::UserRole, QVariant::fromValue(loan.id));
        m_loansTable->setItem(row, 0, memberItem);
        m_loansTable->setItem(row, 1, new QTableWidgetItem(qs(loan.membershipNumber)));
        m_loansTable->setItem(row, 2, new QTableWidgetItem(qs(loan.bookTitle)));
        m_loansTable->setItem(row, 3, new QTableWidgetItem(qs(loan.borrowedAt)));
        m_loansTable->setItem(row, 4, new QTableWidgetItem(qs(loan.dueAt)));
        m_loansTable->setItem(row, 5, new QTableWidgetItem(loanStatusLabel(loan)));
    }

    if (loans.empty()) {
        m_previewCoverPath.clear();
        m_previewPhotoPath.clear();
        renderPreviewImages();
        clearLoanDetails();
        m_extendButton->setEnabled(false);
        m_returnButton->setEnabled(false);
        m_deleteButton->setEnabled(false);
    } else if (selectedLoanId() <= 0) {
        VLMS::selectTableRow(m_loansTable, 0);
    } else {
        onSelectionChanged();
    }
    m_checks->clear();
}

void CirculationPage::focusMemberLoans(const QString& membershipNumber) {
    {
        // All, not Open: the books blocking the removal include overdue ones,
        // which Open no longer lists. The default order puts overdue and open
        // loans first and the member's returned history after them.
        // Moved without letting onFilterChanged run per item: it would refresh
        // against a half-applied selection, and the single refresh below
        // covers both changes anyway.
        const QSignalBlocker blocker(m_filterList);
        m_updatingFilter = true;
        m_filterList->clearSelection();
        for (int row = 0; row < m_filterList->count(); ++row) {
            QListWidgetItem* item = m_filterList->item(row);
            if (isAllFilter(item->data(Qt::UserRole).toString())) {
                item->setSelected(true);
                break;
            }
        }
        m_updatingFilter = false;
    }
    // A borrower filter left on from earlier could hide this member's loans.
    m_memberFilters->reset();

    // Blocked as well, so that setText does not fire its own refresh against
    // the pager's old offset before resetPagerAndRefresh gets to it.
    {
        const QSignalBlocker blocker(m_searchEdit);
        m_searchEdit->setText(membershipNumber);
    }

    resetPagerAndRefresh();
    m_searchEdit->setFocus();
}

void CirculationPage::onSearchChanged() {
    resetPagerAndRefresh();
}

void CirculationPage::onFilterChanged() {
    if (m_updatingFilter || m_filterList->count() == 0) {
        return;
    }

    if (m_filterList->selectedItems().isEmpty()) {
        m_updatingFilter = true;
        m_filterList->item(0)->setSelected(true); // All
        m_updatingFilter = false;
    }

    resetPagerAndRefresh();
}

qint64 CirculationPage::selectedLoanId() const {
    const auto items = m_loansTable->selectedItems();
    if (items.isEmpty()) {
        return 0;
    }
    return m_loansTable->item(items.first()->row(), 0)->data(Qt::UserRole).toLongLong();
}

void CirculationPage::selectLoanId(const qint64 id)
{
    if (id <= 0) {
        return;
    }
    for (int row = 0; row < m_loansTable->rowCount(); ++row) {
        if (m_loansTable->item(row, 0)->data(Qt::UserRole).toLongLong() == id) {
            VLMS::selectTableRow(m_loansTable, row);
            return;
        }
    }
}

void CirculationPage::onSortChanged(int, bool)
{
    const qint64 id = selectedLoanId();
    if (id <= 0) {
        m_pager->setCurrentPage(1);
    } else {
        VLMS::Repositories::LoanQuery query = currentLoanQuery();
        const auto rank = m_repository.rankOfLoan(id, query);
        if (rank) {
            m_pager->setCurrentPage(rank.value() / m_pager->pageSize() + 1);
        } else {
            m_pager->setCurrentPage(1);
        }
    }
    refreshLoans();
    selectLoanId(id);
}

void CirculationPage::onSelectionChanged() {
    const bool anyTicked = !m_checks->checkedIds().isEmpty();
    const qint64 loanId = selectedLoanId();
    if (loanId <= 0) {
        m_extendButton->setEnabled(false);
        m_returnButton->setEnabled(false);
        m_deleteButton->setEnabled(anyTicked);
        return;
    }

    const auto loan = m_repository.getLoan(loanId);
    if (!loan.has_value()) {
        m_extendButton->setEnabled(false);
        m_returnButton->setEnabled(false);
        m_deleteButton->setEnabled(anyTicked);
        return;
    }

    updatePreview(loan.value());
    const bool isOpen = loan->returnedAt.empty();
    m_extendButton->setEnabled(isOpen);
    m_returnButton->setEnabled(isOpen);
    // Only history is archived; an open or overdue loan stays in Circulation.
    // Ticked rows still need the button, because the highlighted row may be open.
    m_deleteButton->setEnabled(!isOpen || anyTicked);
}

void CirculationPage::clearLoanDetails() {
    for (LoanDetailRow& row : m_detailRows) {
        row.label->clear();
        VLMS::clearDetailValueText(row.value);
    }
}

void CirculationPage::refreshSelectedLoanPreview() {
    const qint64 loanId = selectedLoanId();
    if (loanId <= 0) {
        // Painted again rather than left: this is also the theme-switch path,
        // and the placeholder already on screen is in the old theme's colours.
        m_previewCoverPath.clear();
        m_previewPhotoPath.clear();
        renderPreviewImages();
        clearLoanDetails();
        return;
    }

    const auto loan = m_repository.getLoan(loanId);
    if (!loan.has_value()) {
        clearLoanDetails();
        return;
    }

    updatePreview(loan.value());
}

void CirculationPage::renderPreviewImages()
{
    if (m_coverPreview == nullptr || m_photoPreview == nullptr || m_previewPanel == nullptr) {
        return;
    }
    // One size for both boxes, so the pair reads as a pair: the photo is
    // fitted inside the cover's box rather than given its own proportions.
    const QSize size =
        VLMS::adaptivePreviewImageSize(m_previewPanel, VLMS::bookCoverPreviewBounds(), 2);
    VLMS::showBookCover(m_coverPreview, m_previewCoverPath, size);
    VLMS::showMemberPhoto(m_photoPreview, m_previewPhotoPath, size);
}

void CirculationPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_memberFilters->refresh();
}

bool CirculationPage::eventFilter(QObject* watched, QEvent* event)
{
    if (VLMS::shouldSyncPreviewPanel(watched, m_previewPanel, event)) {
        renderPreviewImages();
    }
    return QWidget::eventFilter(watched, event);
}

void CirculationPage::updatePreview(const VLMS::Repositories::LoanRecord& loan) {
    if (loan.coverImagePath.empty()) {
        m_previewCoverPath.clear();
    } else {
        m_previewCoverPath = qs(m_catalogRepository.resolveCoverPath(loan.coverImagePath));
    }
    m_previewPhotoPath = qs(m_memberRepository.resolveImagePath(loan.memberPhotoPath));
    renderPreviewImages();

    const QList<QPair<QString, QString>> rows = {
        {QStringLiteral("loan.field.notes"), VLMS::dashIfEmpty(loan.notes)},
        {QStringLiteral("book.field.title"), qs(loan.bookTitle)},
        {QStringLiteral("book.field.author"), VLMS::dashIfEmpty(loan.authorName)},
        {QStringLiteral("loan.field.member"), qs(loan.memberName)},
        {QStringLiteral("member.field.number"), qs(loan.membershipNumber)},
        {QStringLiteral("loan.field.copy"), qs(loan.copyCode)},
        {QStringLiteral("loan.field.borrowedAt"), qs(loan.borrowedAt)},
        {QStringLiteral("loan.field.dueAt"), qs(loan.dueAt)},
        {QStringLiteral("loan.field.returnedAt"), VLMS::dashIfEmpty(loan.returnedAt)},
        {QStringLiteral("circulation.col.status"), loanStatusLabel(loan)},
    };

    for (int row = 0; row < rows.size() && row < m_detailRows.size(); ++row) {
        m_detailRows[row].label->setText(T(ss(rows.at(row).first)) + QStringLiteral(":"));
        VLMS::setDetailValueText(m_detailRows[row].value, rows.at(row).second);
    }
}

void CirculationPage::extendLoan() {
    const qint64 loanId = selectedLoanId();
    if (loanId <= 0) {
        VLMS::showInformation(
            this,
            T("circulation.extend"),
            T("circulation.selectLoanFirst"));
        return;
    }

    const auto loan = m_repository.getLoan(loanId);
    if (!loan) {
        VLMS::showRepoError(this, loan.error());
        return;
    }
    if (!loan->returnedAt.empty()) {
        VLMS::showInformation(
            this,
            T("circulation.extend"),
            T("circulation.alreadyReturned"));
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

    refreshLoans();
}

void CirculationPage::checkoutLoan() {
    LoanCheckoutDialog dialog(m_repository, {}, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (const auto created = m_repository.createLoan(dialog.loanInput()); !created) {
        VLMS::showRepoError(this, created.error());
        return;
    }

    refreshLoans();
}

void CirculationPage::returnLoan() {
    const qint64 loanId = selectedLoanId();
    if (loanId <= 0) {
        VLMS::showInformation(
            this,
            T("circulation.return"),
            T("circulation.selectLoanFirst"));
        return;
    }

    const auto loan = m_repository.getLoan(loanId);
    if (!loan) {
        VLMS::showRepoError(this, loan.error());
        return;
    }
    if (!loan->returnedAt.empty()) {
        VLMS::showInformation(
            this,
            T("circulation.return"),
            T("circulation.alreadyReturned"));
        return;
    }

    LoanReturnDialog dialog(loan.value(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (const auto returned = m_repository.returnLoan(loanId, ss(dialog.returnedAt()), ss(dialog.notes()));
        !returned) {
        VLMS::showRepoError(this, returned.error());
        return;
    }

    refreshLoans();
}

void CirculationPage::archiveLoan() {
    if (!m_checks->checkedIds().isEmpty()) {
        QList<BulkRow> rows;
        for (const int row : m_checks->checkedRows()) {
            const QTableWidgetItem* member = m_loansTable->item(row, 0);
            const QTableWidgetItem* title = m_loansTable->item(row, 2);
            if (member == nullptr) {
                continue;
            }
            BulkRow bulk;
            bulk.id = member->data(Qt::UserRole).toLongLong();
            bulk.row = row;
            bulk.label = member->text() + QStringLiteral(" — ")
                + (title != nullptr ? title->text() : QString());
            rows.append(bulk);
        }
        BulkActionTexts texts;
        texts.title = T("circulation.delete");
        texts.verb = T("bulk.verb.archive");
        texts.passive = T("bulk.passive.archive");
        texts.noun = T("bulk.noun.loans");
        const int done = runBulkAction(
            this, texts, rows,
            [this](const BulkRow& bulk) { return m_repository.canArchiveLoan(bulk.id); },
            [this](const BulkRow& bulk) { return m_repository.archiveLoan(bulk.id); });
        if (done > 0) {
            refreshLoans();
        }
        return;
    }

    const qint64 loanId = selectedLoanId();
    if (loanId <= 0) {
        return;
    }
    if (!VLMS::askYesNo(this, T("circulation.delete"), T("circulation.archiveLoan"))) {
        return;
    }
    if (const auto archived = m_repository.archiveLoan(loanId); !archived) {
        VLMS::showRepoError(this, archived.error());
        return;
    }
    refreshLoans();
}
