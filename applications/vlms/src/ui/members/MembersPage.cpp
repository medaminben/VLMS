#include "ui/members/MembersPage.h"

#include <VLMS/Core/Strings.h>
#include "ui/BulkAction.h"
#include "ui/ListPageFrame.h"
#include "ui/TableRowChecks.h"
#include "ui/TablePager.h"
#include "ui/UiHelpers.h"
#include "QtBridge.h"
#include "ui/members/MemberEditorDialog.h"
#include "ui/members/MemberFacetFilters.h"
#include "ui/members/MemberLoansDialog.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPair>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
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

using VLMS::Strings;

constexpr int kNameColumnWidth = 260;
constexpr int kPhoneColumnWidth = 160;
constexpr int kPreviewPanelMinWidth = 200;

void showMemberPhotoPlaceholder(QLabel* label, const QSize& size)
{
    label->setPixmap({});
    label->setText(T("members.noPhoto"));
    label->setWordWrap(true);
    if (size.isValid()) {
        label->setAlignment(Qt::AlignCenter);
    }
}

QStringList memberDetailLabelKeys() {
    return {
        QStringLiteral("member.field.notes"),
        QStringLiteral("members.col.name"),
        QStringLiteral("member.field.number"),
        QStringLiteral("member.field.sex"),
        QStringLiteral("member.field.status"),
        QStringLiteral("member.field.activeUntil"),
        QStringLiteral("member.field.occupation"),
        QStringLiteral("member.field.ageGroup"),
        QStringLiteral("member.field.phone"),
        QStringLiteral("member.field.city"),
        QStringLiteral("member.field.address"),
        QStringLiteral("member.field.dateOfBirth"),
        QStringLiteral("member.field.registeredAt"),
        QStringLiteral("members.col.loans"),
        QStringLiteral("member.field.idImage"),
    };
}

}  // namespace

MembersPage::MembersPage(MemberRepository& repository,
                         CirculationRepository& circulationRepository,
                         QWidget* parent)
    : QWidget(parent),
      m_repository(repository),
      m_circulationRepository(circulationRepository) {
    buildUi();
    retranslateUi();
}

void MembersPage::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* frame = new VLMS::ListPageFrame(this);
    VLMS::ListPageFrame::ListConfig config;
    config.subtitle = T("page.members.body");
    config.tableColumnCount = 6;
    config.tableColumnWidths = {140, kNameColumnWidth, kPhoneColumnWidth, 140, 120, 80};
    config.imageObjectName = QStringLiteral("memberPhoto");
    config.imageBounds = VLMS::memberPhotoPreviewBounds();
    config.detailsScrollObjectName = QStringLiteral("memberDetailsScroll");
    config.previewPanelMinWidth = kPreviewPanelMinWidth;
    frame->buildList(config);
    rootLayout->addWidget(frame);

    m_filters = new VLMS::MemberFacetFilters(
        m_repository, ArchiveScope::Live, frame->filterColumn());
    connect(m_filters, &VLMS::MemberFacetFilters::changed,
            this, &MembersPage::resetPagerAndRefresh);
    frame->addFilter(m_filters, 1);

    m_searchEdit = frame->searchEdit();
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MembersPage::onSearchChanged);

    m_loansButton = VLMS::makeSecondaryButton({});
    m_addButton = VLMS::makePrimaryButton({});
    m_editButton = VLMS::makeSecondaryButton({});
    m_deleteButton = VLMS::makeSecondaryButton({});
    connect(m_loansButton, &QPushButton::clicked, this, &MembersPage::showLoanHistory);
    connect(m_addButton, &QPushButton::clicked, this, &MembersPage::addMember);
    connect(m_editButton, &QPushButton::clicked, this, &MembersPage::editMember);
    connect(m_deleteButton, &QPushButton::clicked, this, &MembersPage::deleteMember);
    frame->addButton(m_loansButton);
    frame->addButton(m_addButton);
    frame->addButton(m_editButton);
    frame->addButton(m_deleteButton);

    m_membersTable = frame->table();
    m_checks = new TableRowChecks(m_membersTable, this);
    connect(m_membersTable, &QTableWidget::itemSelectionChanged, this, &MembersPage::onSelectionChanged);
    m_pager = frame->pager();
    connect(m_pager, &VLMS::TablePager::pageChanged, this, &MembersPage::refreshMembers);

    m_sort = new VLMS::TableHeaderSort(m_membersTable, this);
    m_sort->setColumnKeys({
        QString::fromLatin1(MemberSort::kNumber),
        QString::fromLatin1(MemberSort::kName),
        QString::fromLatin1(MemberSort::kPhone),
        QString::fromLatin1(MemberSort::kCity),
        QString::fromLatin1(MemberSort::kStatus),
        QString::fromLatin1(MemberSort::kLoans),
    });
    connect(m_sort, &VLMS::TableHeaderSort::sortChanged,
            this, &MembersPage::onSortChanged);

    m_photoPreview = frame->imageLabel();
    m_previewPanel = frame->previewPanel();
    m_detailsPanel = frame->detailsPanel();
    showMemberPhotoPlaceholder(
        m_photoPreview,
        VLMS::adaptivePreviewImageSize(m_previewPanel, VLMS::memberPhotoPreviewBounds()));

    auto* detailsLayout = qobject_cast<QVBoxLayout*>(m_detailsPanel->layout());
    const QStringList detailKeys = memberDetailLabelKeys();
    m_detailRows.reserve(detailKeys.size());
    for (const QString& key : detailKeys) {
        MemberDetailRow detail;
        detail.labelKey = key;

        auto* fieldWidget = new QWidget(m_detailsPanel);
        auto* fieldLayout = new QVBoxLayout(fieldWidget);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(2);

        detail.label = new QLabel(fieldWidget);
        detail.label->setObjectName(QStringLiteral("bookDetailLabel"));
        detail.label->setAlignment(Qt::AlignLeading);

        const bool scrollable = key == QStringLiteral("member.field.notes");
        detail.value = VLMS::makeDetailValueWidget(fieldWidget, scrollable);

        fieldLayout->addWidget(detail.label);
        fieldLayout->addWidget(detail.value);

        detailsLayout->addWidget(fieldWidget);
        m_detailRows.append(detail);
    }
    detailsLayout->addStretch(1);

    m_previewPanel->installEventFilter(this);
}

void MembersPage::retranslateUi() {
    if (auto* title = findChild<QLabel*>(QStringLiteral("pageTitle"))) {
        title->setText(T("page.members.title"));
    }
    if (auto* subtitle = findChild<QLabel*>(QStringLiteral("pageSubtitle"))) {
        subtitle->setText(T("page.members.body"));
    }

    m_searchEdit->setPlaceholderText(T("members.searchPlaceholder"));
    m_loansButton->setText(T("members.loans"));
    m_addButton->setText(T("members.addMember"));
    m_editButton->setText(T("members.edit"));
    m_deleteButton->setText(T("members.delete"));
    m_pager->retranslateUi();

    m_membersTable->setHorizontalHeaderLabels({
        T("members.col.number"),
        T("members.col.name"),
        T("members.col.phone"),
        T("members.col.city"),
        T("members.col.status"),
        T("members.col.loans"),
    });

    refreshAllFilters();

    const qint64 selectedId = selectedMemberId();
    refreshMembers();
    selectMemberId(selectedId);

    refreshSelectedMemberPreview();
}


void MembersPage::refreshAllFilters()
{
    m_filters->refresh();
}

void MembersPage::resetPagerAndRefresh() {
    m_pager->resetToFirstPage();
    refreshMembers();
}

MemberQuery MembersPage::currentMemberQuery() const {
    MemberQuery query;
    query.search = ss(m_searchEdit->text());
    const MemberFacets facets = m_filters->facets();
    query.statuses = facets.statuses;
    query.sexes = facets.sexes;
    query.inscriptionYears = facets.inscriptionYears;
    query.ageGroups = facets.ageGroups;
    query.cities = facets.cities;
    query.limit = m_pager->pageSize();
    query.offset = m_pager->offset();
    if (m_sort != nullptr && m_sort->isActive()) {
        query.sortColumn = ss(m_sort->columnKey());
        query.sortAscending = m_sort->ascending();
    }
    return query;
}

void MembersPage::refreshMembers() {
    MemberQuery query = currentMemberQuery();
    const auto totalCount = m_repository.countMembers(query);
    if (!totalCount) {
        VLMS::showRepoError(this, totalCount.error());
        return;
    }
    m_pager->setTotalCount(totalCount.value());

    query.limit = m_pager->pageSize();
    query.offset = m_pager->offset();
    const auto membersResult = m_repository.listMembers(query);
    if (!membersResult) {
        VLMS::showRepoError(this, membersResult.error());
        return;
    }
    const auto& members = membersResult.value();
    m_membersTable->setRowCount(members.size());

    for (int row = 0; row < members.size(); ++row) {
        const MemberRecord& member = members.at(row);
        const QString fullName = member.fullName.empty()
            ? QStringLiteral("%1 %2").arg(qs(member.firstName), qs(member.lastName)).trimmed()
            : qs(member.fullName);

        auto* numberItem = new QTableWidgetItem(qs(member.membershipNumber));
        numberItem->setData(Qt::UserRole, QVariant::fromValue(member.id));
        m_membersTable->setItem(row, 0, numberItem);
        m_membersTable->setItem(row, 1, new QTableWidgetItem(fullName));
        m_membersTable->setItem(row, 2, new QTableWidgetItem(qs(member.phone)));
        m_membersTable->setItem(row, 3, new QTableWidgetItem(qs(member.city)));
        m_membersTable->setItem(row, 4, new QTableWidgetItem(qs(Strings::memberStatusLabel(member.status))));
        auto* loansItem = new QTableWidgetItem;
        loansItem->setData(Qt::DisplayRole, member.activeLoanCount);
        m_membersTable->setItem(row, 5, loansItem);
    }

    if (members.empty()) {
        m_previewPhotoPath.clear();
        renderPhotoPreview();
        clearMemberDetails();
    } else if (selectedMemberId() <= 0) {
        VLMS::selectTableRow(m_membersTable, 0);
    } else {
        // The same row can stay selected while the person in it changes, and
        // selectRow then does not ask for the preview again.
        refreshSelectedMemberPreview();
    }
    m_checks->clear();
}

void MembersPage::onSearchChanged() {
    resetPagerAndRefresh();
}

qint64 MembersPage::selectedMemberId() const {
    const auto items = m_membersTable->selectedItems();
    if (items.isEmpty()) {
        return 0;
    }
    return m_membersTable->item(items.first()->row(), 0)->data(Qt::UserRole).toLongLong();
}

void MembersPage::selectMemberId(const qint64 id)
{
    if (id <= 0) {
        return;
    }
    for (int row = 0; row < m_membersTable->rowCount(); ++row) {
        if (m_membersTable->item(row, 0)->data(Qt::UserRole).toLongLong() == id) {
            VLMS::selectTableRow(m_membersTable, row);
            return;
        }
    }
}

void MembersPage::onSortChanged(int, bool)
{
    const qint64 id = selectedMemberId();
    if (id <= 0) {
        m_pager->setCurrentPage(1);
    } else {
        MemberQuery query = currentMemberQuery();
        const auto rank = m_repository.rankOfMember(id, query);
        if (rank) {
            m_pager->setCurrentPage(rank.value() / m_pager->pageSize() + 1);
        } else {
            m_pager->setCurrentPage(1);
        }
    }
    refreshMembers();
    selectMemberId(id);
}

void MembersPage::onSelectionChanged() {
    const qint64 memberId = selectedMemberId();
    if (memberId <= 0) {
        return;
    }

    const auto member = m_repository.getMember(memberId);
    if (!member.has_value()) {
        return;
    }

    updatePreview(member.value());
}

void MembersPage::showLoanHistory() {
    const qint64 memberId = selectedMemberId();
    if (memberId <= 0) {
        return;
    }

    const auto member = m_repository.getMember(memberId);
    if (!member.has_value()) {
        return;
    }

    const QString fullName =
        QStringLiteral("%1 %2").arg(qs(member->firstName), qs(member->lastName)).trimmed();

    MemberLoansDialog dialog(m_circulationRepository, memberId, fullName, this);
    dialog.exec();
    // A loan made in there changes the member's loan count behind it.
    refreshMembers();
    selectMemberId(memberId);
}

void MembersPage::clearMemberDetails() {
    for (MemberDetailRow& row : m_detailRows) {
        row.label->clear();
        VLMS::clearDetailValueText(row.value);
    }
}

void MembersPage::refreshSelectedMemberPreview() {
    const qint64 memberId = selectedMemberId();
    if (memberId <= 0) {
        clearMemberDetails();
        return;
    }

    const auto member = m_repository.getMember(memberId);
    if (!member.has_value()) {
        clearMemberDetails();
        return;
    }

    updatePreview(member.value());
}

void MembersPage::renderPhotoPreview()
{
    if (m_photoPreview == nullptr || m_previewPanel == nullptr) {
        return;
    }

    const QSize photoSize =
        VLMS::adaptivePreviewImageSize(m_previewPanel, VLMS::memberPhotoPreviewBounds());
    VLMS::applyPreviewLabelGeometry(m_photoPreview, photoSize);

    if (m_previewPhotoPath.isEmpty()) {
        showMemberPhotoPlaceholder(m_photoPreview, photoSize);
        return;
    }

    const QPixmap pixmap(m_previewPhotoPath);
    if (pixmap.isNull()) {
        showMemberPhotoPlaceholder(m_photoPreview, photoSize);
        return;
    }

    m_photoPreview->setText({});
    m_photoPreview->setPixmap(
        pixmap.scaled(photoSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

bool MembersPage::eventFilter(QObject* watched, QEvent* event)
{
    if (VLMS::shouldSyncPreviewPanel(watched, m_previewPanel, event)) {
        renderPhotoPreview();
    }
    return QWidget::eventFilter(watched, event);
}

void MembersPage::updatePreview(const MemberRecord& member) {
    if (member.photoPath.empty()) {
        m_previewPhotoPath.clear();
    } else {
        m_previewPhotoPath = qs(m_repository.resolveImagePath(member.photoPath));
    }
    renderPhotoPreview();

    const QString fullName = member.fullName.empty()
        ? QStringLiteral("%1 %2").arg(qs(member.firstName), qs(member.lastName)).trimmed()
        : qs(member.fullName);

    const QList<QPair<QString, QString>> rows = {
        {QStringLiteral("member.field.notes"), VLMS::dashIfEmpty(member.notes)},
        {QStringLiteral("members.col.name"), fullName},
        {QStringLiteral("member.field.number"), qs(member.membershipNumber)},
        {QStringLiteral("member.field.sex"),
         member.sex.empty() ? VLMS::dashIfEmpty(member.sex)
                            : qs(Strings::memberSexLabel(member.sex))},
        {QStringLiteral("member.field.status"), qs(Strings::memberStatusLabel(member.status))},
        {QStringLiteral("member.field.activeUntil"), VLMS::dashIfEmpty(member.activeUntil)},
        {QStringLiteral("member.field.occupation"), VLMS::dashIfEmpty(member.occupation)},
        {QStringLiteral("member.field.ageGroup"),
         member.ageGroup.empty() ? VLMS::dashIfEmpty(member.ageGroup)
                                 : qs(Strings::memberAgeGroupLabel(member.ageGroup))},
        {QStringLiteral("member.field.phone"), VLMS::dashIfEmpty(member.phone)},
        {QStringLiteral("member.field.city"), VLMS::dashIfEmpty(member.city)},
        {QStringLiteral("member.field.address"), VLMS::dashIfEmpty(member.address)},
        {QStringLiteral("member.field.dateOfBirth"), VLMS::dashIfEmpty(member.dateOfBirth)},
        {QStringLiteral("member.field.registeredAt"), VLMS::dashIfEmpty(member.registeredAt)},
        {QStringLiteral("members.col.loans"), QString::number(member.activeLoanCount)},
        {QStringLiteral("member.field.idImage"),
         member.idImagePath.empty() ? T("common.emDash")
                                      : T("members.hasIdImage")},
    };

    for (int row = 0; row < rows.size() && row < m_detailRows.size(); ++row) {
        m_detailRows[row].label->setText(T(ss(rows.at(row).first)) + QStringLiteral(":"));
        VLMS::setDetailValueText(m_detailRows[row].value, rows.at(row).second);
    }
}

void MembersPage::addMember() {
    MemberEditorDialog dialog(m_repository, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    MemberWrite write;
    write.member = dialog.memberInput();
    if (dialog.photoChanged()) {
        write.photoSourcePath = ss(dialog.photoSourcePath());
    }
    if (dialog.idImageChanged()) {
        write.idImageSourcePath = ss(dialog.idImageSourcePath());
    }
    write.clearPhoto = dialog.photoRemoved();
    write.clearIdImage = dialog.idImageRemoved();
    if (const auto created = m_repository.saveNewMember(write); !created) {
        VLMS::showRepoError(this, created.error());
        return;
    }

    refreshAllFilters();
    refreshMembers();
}

void MembersPage::editMember() {
    const qint64 memberId = selectedMemberId();
    if (memberId <= 0) {
        VLMS::showInformation(
            this,
            T("members.editMember"),
            T("members.selectMemberFirst"));
        return;
    }

    const auto member = m_repository.getMember(memberId);
    if (!member) {
        VLMS::showRepoError(this, member.error());
        return;
    }

    MemberEditorDialog dialog(m_repository, member.value(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    MemberWrite write;
    write.member = dialog.memberInput();
    if (dialog.photoChanged()) {
        write.photoSourcePath = ss(dialog.photoSourcePath());
    }
    if (dialog.idImageChanged()) {
        write.idImageSourcePath = ss(dialog.idImageSourcePath());
    }
    write.clearPhoto = dialog.photoRemoved();
    write.clearIdImage = dialog.idImageRemoved();
    if (const auto saved = m_repository.saveExistingMember(memberId, write); !saved) {
        VLMS::showRepoError(this, saved.error());
        return;
    }

    refreshAllFilters();
    refreshMembers();
}

void MembersPage::deleteMember() {
    if (!m_checks->checkedIds().isEmpty()) {
        QList<BulkRow> rows;
        for (const int row : m_checks->checkedRows()) {
            const QTableWidgetItem* item = m_membersTable->item(row, 0);
            const QTableWidgetItem* name = m_membersTable->item(row, 1);
            if (item == nullptr) {
                continue;
            }
            BulkRow bulk;
            bulk.id = item->data(Qt::UserRole).toLongLong();
            bulk.row = row;
            bulk.label = name != nullptr ? name->text() : item->text();
            rows.append(bulk);
        }
        BulkActionTexts texts;
        texts.title = T("members.deleteMember");
        texts.verb = T("bulk.verb.archive");
        texts.passive = T("bulk.passive.archive");
        texts.noun = T("bulk.noun.members");
        const int done = runBulkAction(
            this, texts, rows,
            [this](const BulkRow& bulk) { return m_repository.canArchiveMember(bulk.id); },
            [this](const BulkRow& bulk) { return m_repository.archiveMember(bulk.id); });
        if (done > 0) {
            refreshAllFilters();
            refreshMembers();
        }
        return;
    }

    const qint64 memberId = selectedMemberId();
    if (memberId <= 0) {
        VLMS::showInformation(
            this,
            T("members.deleteMember"),
            T("members.selectMemberFirst"));
        return;
    }

    const QString title = T("members.deleteMember");

    // Asked before the confirmation rather than after it: what the librarian is
    // about to be offered depends on the answer, and being asked "delete this
    // member?" only to be told afterwards that it was never possible is the
    // long way round to the same no.
    const auto block = m_repository.removalBlock(memberId);
    if (!block) {
        VLMS::showRepoError(this, block.error());
        return;
    }
    if (block.value() == MemberRepository::MemberRemovalBlock::OpenLoans) {
        if (VLMS::showWarningWithAction(
                this,
                title,
                T("members.deleteBlockedByLoans"),
                T("members.goToLoans"))) {
            const auto member = m_repository.getMember(memberId);
            emit memberLoansRequested(member ? qs(member->membershipNumber) : QString());
        }
        return;
    }

    if (!VLMS::askYesNo(this, title, T("members.deleteConfirm"))) {
        return;
    }

    // Delete archives, always. Destroying the record is the Archive's, behind
    // its own button and its own gate -- so a member with borrowing history is
    // no longer a special case here.
    if (const auto archived = m_repository.archiveMember(memberId); !archived) {
        VLMS::showRepoError(this, archived.error());
        return;
    }

    refreshAllFilters();
    refreshMembers();
}
