#include "ui/archive/ArchivePage.h"
#include "ui/archive/ReuseNumberFlow.h"
#include "ui/archive/ArchiveLoansDialog.h"

#include <VLMS/Core/Strings.h>

#include "QtBridge.h"
#include "ui/BulkAction.h"
#include "ui/PreviewImages.h"
#include "ui/catalog/BookFacetFilters.h"
#include "ui/members/MemberFacetFilters.h"
#include "ui/ListPageFrame.h"
#include "ui/TableRowChecks.h"
#include "ui/TableHeaderSort.h"
#include "ui/TablePager.h"
#include "ui/UiHelpers.h"

#include <algorithm>
#include <utility>

#include <QAbstractItemView>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

using VLMS::T;
using VLMS::qs;
using VLMS::ss;
using VLMS::Strings;

namespace {

constexpr int kMaxColumns = 5;

struct TypeEntry {
    ArchivePage::Type type;
    const char* code;
    const char* labelKey;
};

constexpr TypeEntry kTypes[] = {
    {ArchivePage::Type::Members, "members", "archive.type.members"},
    {ArchivePage::Type::Books, "books", "archive.type.books"},
    {ArchivePage::Type::Copies, "copies", "archive.type.copies"},
    {ArchivePage::Type::Loans, "loans", "archive.type.loans"},
};

struct Column {
    const char* labelKey;
    const char* sortKey;
};

std::vector<Column> columnsFor(const ArchivePage::Type type)
{
    switch (type) {
    case ArchivePage::Type::Members:
        return {{"archive.col.number", MemberSort::kNumber},
                {"archive.col.name", MemberSort::kName},
                {"archive.col.city", MemberSort::kCity},
                {"archive.col.status", MemberSort::kStatus},
                {"archive.col.loans", MemberSort::kAllLoans},
                {"archive.col.archivedAt", MemberSort::kArchivedAt}};
    case ArchivePage::Type::Books:
        return {{"archive.col.title", BookSort::kTitle},
                {"archive.col.author", BookSort::kAuthor},
                {"archive.col.copies", BookSort::kCopies},
                {"archive.col.archivedAt", BookSort::kArchivedAt}};
    case ArchivePage::Type::Copies:
        return {{"archive.col.localId", CopySort::kLocalId},
                {"archive.col.source", CopySort::kSource},
                {"archive.col.title", CopySort::kTitle},
                {"archive.col.loans", CopySort::kLoans},
                {"archive.col.archivedAt", CopySort::kArchivedAt}};
    case ArchivePage::Type::Loans:
        return {{"archive.col.member", LoanSort::kMember},
                {"archive.col.copy", LoanSort::kTitle},
                {"archive.col.returnedAt", LoanSort::kReturned},
                {"archive.col.archivedAt", LoanSort::kArchivedAt}};
    }
    return {};
}

QTableWidgetItem* idItem(const QString& text, const qint64 id)
{
    auto* item = new QTableWidgetItem(text);
    item->setData(Qt::UserRole, QVariant::fromValue(id));
    return item;
}

QString sourceLabel(const std::string& source)
{
    return T(source == "arabic" ? "book.copy.source.arabic" : "book.copy.source.foreign");
}

QString memberName(const MemberRecord& member)
{
    return qs(member.firstName + " " + member.lastName);
}

}  // namespace

ArchivePage::ArchivePage(MemberRepository& members,
                         CatalogRepository& catalog,
                         CirculationRepository& circulation,
                         QWidget* parent)
    : QWidget(parent),
      m_members(members),
      m_catalog(catalog),
      m_circulation(circulation)
{
    buildUi();
    retranslateUi();
}

void ArchivePage::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* frame = new VLMS::ListPageFrame(this);
    VLMS::ListPageFrame::ListConfig config;
    config.subtitle = T("page.archive.body");
    config.tableColumnCount = kMaxColumns;
    config.tableColumnWidths = {140, 240, 160, 140, 160};
    config.imageObjectName = QStringLiteral("archiveImage");
    config.imageBounds = VLMS::bookCoverPreviewBounds();
    config.secondImageObjectName = QStringLiteral("archiveMemberPhoto");
    config.secondImageBounds = VLMS::bookCoverPreviewBounds();
    config.detailsScrollObjectName = QStringLiteral("archiveDetailsScroll");
    frame->buildList(config);
    rootLayout->addWidget(frame);

    m_typeList = new QListWidget(frame->filterColumn());
    m_typeList->setObjectName(QStringLiteral("archiveType"));
    m_typeList->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const TypeEntry& entry : kTypes) {
        auto* item = new QListWidgetItem(m_typeList);
        item->setData(Qt::UserRole, QString::fromLatin1(entry.code));
    }
    m_typeList->item(0)->setSelected(true);
    m_typeList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_typeList, &QListWidget::itemSelectionChanged, this, &ArchivePage::onTypeChanged);
    frame->addFilter(m_typeList, 0);

    // Members and loans take the Members page's filters, titles and copies the
    // Catalogue's; applyFilters shows the set the current type uses.
    m_memberFilters = new VLMS::MemberFacetFilters(
        m_members, ArchiveScope::Archived, frame->filterColumn());
    connect(m_memberFilters, &VLMS::MemberFacetFilters::changed,
            this, &ArchivePage::onFacetsChanged);
    frame->addFilter(m_memberFilters, 1);
    m_bookFilters = new VLMS::BookFacetFilters(
        m_catalog, ArchiveScope::Archived, frame->filterColumn());
    connect(m_bookFilters, &VLMS::BookFacetFilters::changed,
            this, &ArchivePage::onFacetsChanged);
    frame->addFilter(m_bookFilters, 1);

    m_searchEdit = frame->searchEdit();
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ArchivePage::onSearchChanged);

    m_loansButton = VLMS::makeSecondaryButton({});
    connect(m_loansButton, &QPushButton::clicked, this, &ArchivePage::showLoanHistory);
    // First after the search: the history of what is on screen, before
    // anything is done to it.
    frame->addButton(m_loansButton);
    m_reuseButton = VLMS::makeSecondaryButton({});
    m_restoreButton = VLMS::makePrimaryButton({});
    connect(m_restoreButton, &QPushButton::clicked, this, &ArchivePage::restoreSelected);
    connect(m_reuseButton, &QPushButton::clicked, this, &ArchivePage::reuseSelected);
    // Reuse sits next to the search: the number being looked for is typed
    // there, and the button exists only on the Copies list.
    frame->addButton(m_reuseButton);
    frame->addButton(m_restoreButton);
    m_purgeButton = VLMS::makeSecondaryButton({});
    connect(m_purgeButton, &QPushButton::clicked, this, &ArchivePage::purgeSelected);
    frame->addButton(m_purgeButton);

    m_table = frame->table();
    m_checks = new TableRowChecks(m_table, this);
    connect(m_checks, &TableRowChecks::checkedCountChanged, this, &ArchivePage::onSelectionChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ArchivePage::onSelectionChanged);
    m_pager = frame->pager();
    connect(m_pager, &VLMS::TablePager::pageChanged, this, &ArchivePage::refreshRows);

    m_sort = new VLMS::TableHeaderSort(m_table, this);
    connect(m_sort, &VLMS::TableHeaderSort::sortChanged, this, &ArchivePage::onSortChanged);

    m_image = frame->imageLabel();
    m_photo = frame->secondImageLabel();
    m_previewPanel = frame->previewPanel();
    m_detailsPanel = frame->detailsPanel();
    if (auto* detailsLayout = qobject_cast<QVBoxLayout*>(m_detailsPanel->layout())) {
        detailsLayout->addStretch(1);
    }
    m_previewPanel->installEventFilter(this);
}

bool ArchivePage::eventFilter(QObject* watched, QEvent* event)
{
    if (VLMS::shouldSyncPreviewPanel(watched, m_previewPanel, event)) {
        renderImages();
    }
    return QWidget::eventFilter(watched, event);
}

void ArchivePage::applyFilters()
{
    const bool memberType = m_type == Type::Members || m_type == Type::Loans;
    m_memberFilters->setVisible(memberType);
    m_bookFilters->setVisible(!memberType);
    // A loan's borrower can be live or archived, and an archived copy can
    // belong to a live title, so those two read their values from every row.
    m_memberFilters->setValueScope(m_type == Type::Loans ? ArchiveScope::Any
                                                         : ArchiveScope::Archived);
    // For a loan the year is the year it was made, not its borrower's.
    if (m_type == Type::Loans) {
        m_memberFilters->useLoanYears(
            [this]() { return m_circulation.listLoanYears(ArchiveScope::Archived); });
    } else {
        m_memberFilters->useLoanYears({});
    }
    m_bookFilters->setScope(m_type == Type::Copies ? ArchiveScope::Any : ArchiveScope::Archived);
    if (memberType) {
        m_memberFilters->refresh();
    } else {
        m_bookFilters->refresh();
    }
}

void ArchivePage::onFacetsChanged()
{
    m_pager->resetToFirstPage();
    refreshRows();
}

void ArchivePage::retranslateUi()
{
    if (auto* title = findChild<QLabel*>(QStringLiteral("pageTitle"))) {
        title->setText(T("page.archive.title"));
    }
    if (auto* subtitle = findChild<QLabel*>(QStringLiteral("pageSubtitle"))) {
        subtitle->setText(T("page.archive.body"));
    }
    m_searchEdit->setPlaceholderText(T("archive.searchPlaceholder"));
    m_loansButton->setText(T("archive.loans"));
    m_restoreButton->setText(T("archive.restore"));
    m_reuseButton->setText(T("archive.reuse"));
    m_purgeButton->setText(T("archive.purge"));
    for (int row = 0; row < m_typeList->count(); ++row) {
        m_typeList->item(row)->setText(T(kTypes[row].labelKey));
    }
    sizeTypeList();
    m_pager->retranslateUi();
    applyColumns();
    applyFilters();
    refreshRows();
}

void ArchivePage::sizeTypeList()
{
    // No scrollbar, so every row must fit: a short list scrolls the first
    // types out of sight when a lower one is picked. Measured once the labels
    // are set, and with the theme's frame and padding, not a guessed slack.
    m_typeList->ensurePolished();
    int rows = 0;
    for (int row = 0; row < m_typeList->count(); ++row) {
        rows += std::max(m_typeList->sizeHintForRow(row), 20);
    }
    const QMargins margins = m_typeList->contentsMargins();
    m_typeList->setFixedHeight(rows + 2 * m_typeList->frameWidth() + margins.top()
                               + margins.bottom());
}

void ArchivePage::refresh()
{
    // Something archived since the last visit can bring a new year, city or
    // language with it.
    applyFilters();
    refreshRows();
}

void ArchivePage::applyColumns()
{
    const std::vector<Column> columns = columnsFor(m_type);
    QStringList labels;
    QStringList keys;
    for (const Column& column : columns) {
        labels.append(T(column.labelKey));
        keys.append(QString::fromLatin1(column.sortKey));
    }
    m_table->setColumnCount(static_cast<int>(columns.size()));
    m_table->setHorizontalHeaderLabels(labels);
    m_sort->setColumnKeys(keys);
}

void ArchivePage::onTypeChanged()
{
    const auto selected = m_typeList->selectedItems();
    if (selected.isEmpty()) {
        // Single-select with nothing selected is not a state: put it back.
        const QSignalBlocker blocker(m_typeList);
        m_typeList->item(static_cast<int>(m_type))->setSelected(true);
        return;
    }
    m_type = kTypes[m_typeList->row(selected.first())].type;
    m_sort->reset();
    // Picks made for one type mean nothing to the next.
    m_memberFilters->reset();
    m_bookFilters->reset();
    applyFilters();
    applyColumns();
    m_pager->resetToFirstPage();
    refreshRows();
    // Rebuilding the table does not always change the selection -- row 0 of
    // the new list is row 0 of the old one -- so the buttons would keep the
    // previous type's state, Reuse included.
    onSelectionChanged();
}

void ArchivePage::onSearchChanged()
{
    m_pager->resetToFirstPage();
    refreshRows();
}

void ArchivePage::onSortChanged(int, bool)
{
    m_pager->setCurrentPage(1);
    refreshRows();
}

void ArchivePage::refreshRows()
{
    {
        const QSignalBlocker blocker(m_table);
        m_table->clearSelection();
        m_table->clearContents();
        if (!fillRows()) {
            return;
        }
    }
    if (m_table->rowCount() > 0) {
        VLMS::selectTableRow(m_table, 0);
    } else {
        onSelectionChanged();
    }
}

bool ArchivePage::fillRows()
{
    const std::string search = ss(m_searchEdit->text());
    const std::string sortColumn = m_sort->isActive() ? ss(m_sort->columnKey()) : std::string();
    const bool ascending = m_sort->ascending();

    const auto fail = [this](const VLMS::Error& error) {
        VLMS::showRepoError(this, error);
        return false;
    };

    switch (m_type) {
    case Type::Members: {
        MemberQuery query;
        query.archive = ArchiveScope::Archived;
        query.search = search;
        const MemberFacets facets = m_memberFilters->facets();
        query.statuses = facets.statuses;
        query.sexes = facets.sexes;
        query.inscriptionYears = facets.inscriptionYears;
        query.ageGroups = facets.ageGroups;
        query.cities = facets.cities;
        query.sortColumn = sortColumn;
        query.sortAscending = ascending;
        const auto total = m_members.countMembers(query);
        if (!total) {
            return fail(total.error());
        }
        m_pager->setTotalCount(total.value());
        query.limit = m_pager->pageSize();
        query.offset = m_pager->offset();
        const auto rows = m_members.listMembers(query);
        if (!rows) {
            return fail(rows.error());
        }
        m_memberRows = rows.value();
        m_table->setRowCount(static_cast<int>(m_memberRows.size()));
        for (int row = 0; row < m_table->rowCount(); ++row) {
            const MemberRecord& member = m_memberRows.at(static_cast<std::size_t>(row));
            m_table->setItem(row, 0, idItem(qs(member.membershipNumber), member.id));
            m_table->setItem(row, 1, new QTableWidgetItem(memberName(member)));
            m_table->setItem(row, 2, new QTableWidgetItem(VLMS::dashIfEmpty(member.city)));
            m_table->setItem(row, 3,
                             new QTableWidgetItem(qs(Strings::memberStatusLabel(member.status))));
            m_table->setItem(row, 4, new QTableWidgetItem(QString::number(member.loanCount)));
            m_table->setItem(row, 5, new QTableWidgetItem(qs(member.archivedAt)));
        }
        m_checks->clear();
        return true;
    }
    case Type::Books: {
        BookQuery query;
        query.archive = ArchiveScope::Archived;
        query.search = search;
        query.languages = VLMS::svl(m_bookFilters->languages());
        query.categoryCodes = VLMS::svl(m_bookFilters->categoryCodes());
        query.coverFilter = m_bookFilters->coverFilter();
        query.sortColumn = sortColumn;
        query.sortAscending = ascending;
        const auto total = m_catalog.countBooks(query);
        if (!total) {
            return fail(total.error());
        }
        m_pager->setTotalCount(total.value());
        query.limit = m_pager->pageSize();
        query.offset = m_pager->offset();
        const auto rows = m_catalog.listBooks(query);
        if (!rows) {
            return fail(rows.error());
        }
        m_bookRows = rows.value();
        m_table->setRowCount(static_cast<int>(m_bookRows.size()));
        for (int row = 0; row < m_table->rowCount(); ++row) {
            const BookRecord& book = m_bookRows.at(static_cast<std::size_t>(row));
            m_table->setItem(row, 0, idItem(qs(book.title), book.id));
            m_table->setItem(row, 1, new QTableWidgetItem(VLMS::dashIfEmpty(book.authorName)));
            m_table->setItem(row, 2, new QTableWidgetItem(QString::number(book.totalCopies)));
            m_table->setItem(row, 3, new QTableWidgetItem(qs(book.archivedAt)));
        }
        m_checks->clear();
        return true;
    }
    case Type::Copies: {
        CopyQuery query;
        query.archive = ArchiveScope::Archived;
        query.search = search;
        query.languages = VLMS::svl(m_bookFilters->languages());
        query.categoryCodes = VLMS::svl(m_bookFilters->categoryCodes());
        query.coverFilter = m_bookFilters->coverFilter();
        query.sortColumn = sortColumn;
        query.sortAscending = ascending;
        const auto total = m_catalog.countCopyRows(query);
        if (!total) {
            return fail(total.error());
        }
        m_pager->setTotalCount(total.value());
        query.limit = m_pager->pageSize();
        query.offset = m_pager->offset();
        const auto rows = m_catalog.listCopyRows(query);
        if (!rows) {
            return fail(rows.error());
        }
        m_copyRows = rows.value();
        m_table->setRowCount(static_cast<int>(m_copyRows.size()));
        for (int row = 0; row < m_table->rowCount(); ++row) {
            const BookCopyRecord& copy = m_copyRows.at(static_cast<std::size_t>(row));
            m_table->setItem(row, 0, idItem(VLMS::dashIfEmpty(copy.localId), copy.id));
            m_table->setItem(row, 1, new QTableWidgetItem(sourceLabel(copy.source)));
            m_table->setItem(row, 2, new QTableWidgetItem(qs(copy.bookTitle)));
            m_table->setItem(row, 3, new QTableWidgetItem(QString::number(copy.loanCount)));
            m_table->setItem(row, 4, new QTableWidgetItem(qs(copy.archivedAt)));
        }
        m_checks->clear();
        return true;
    }
    case Type::Loans: {
        LoanQuery query;
        query.archive = ArchiveScope::Archived;
        query.search = search;
        query.member = m_memberFilters->facets();
        query.loanYears = VLMS::svl(m_memberFilters->loanYears());
        query.sortColumn = sortColumn;
        query.sortAscending = ascending;
        const auto total = m_circulation.countLoans(query);
        if (!total) {
            return fail(total.error());
        }
        m_pager->setTotalCount(total.value());
        query.limit = m_pager->pageSize();
        query.offset = m_pager->offset();
        const auto rows = m_circulation.listLoans(query);
        if (!rows) {
            return fail(rows.error());
        }
        m_loanRows = rows.value();
        m_table->setRowCount(static_cast<int>(m_loanRows.size()));
        for (int row = 0; row < m_table->rowCount(); ++row) {
            const LoanRecord& loan = m_loanRows.at(static_cast<std::size_t>(row));
            m_table->setItem(row, 0, idItem(qs(loan.memberName), loan.id));
            m_table->setItem(row, 1,
                             new QTableWidgetItem(qs(loan.copyCode) + QStringLiteral(" / ")
                                                  + qs(loan.bookTitle)));
            m_table->setItem(row, 2, new QTableWidgetItem(qs(loan.returnedAt)));
            m_table->setItem(row, 3, new QTableWidgetItem(qs(loan.archivedAt)));
        }
        m_checks->clear();
        return true;
    }
    }
    return true;
}

qint64 ArchivePage::selectedId() const
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty()) {
        return 0;
    }
    const QTableWidgetItem* first = m_table->item(items.first()->row(), 0);
    return first == nullptr ? 0 : first->data(Qt::UserRole).toLongLong();
}

const BookCopyRecord* ArchivePage::selectedCopy() const
{
    if (m_type != Type::Copies) {
        return nullptr;
    }
    const qint64 id = selectedId();
    for (const BookCopyRecord& copy : m_copyRows) {
        if (copy.id == id) {
            return &copy;
        }
    }
    return nullptr;
}

bool ArchivePage::selectedMayBePurged() const
{
    const qint64 id = selectedId();
    if (id <= 0) {
        return false;
    }
    switch (m_type) {
    case Type::Members:
        for (const MemberRecord& member : m_memberRows) {
            if (member.id == id) {
                return member.loanCount == 0;
            }
        }
        return false;
    case Type::Books:
        for (const BookRecord& book : m_bookRows) {
            if (book.id == id) {
                return book.totalCopies == 0;
            }
        }
        return false;
    case Type::Copies: {
        const BookCopyRecord* copy = selectedCopy();
        return copy != nullptr && copy->loanCount == 0;
    }
    case Type::Loans:
        return true;
    }
    return false;
}

bool ArchivePage::selectedHasLoans() const
{
    const qint64 id = selectedId();
    if (id <= 0) {
        return false;
    }
    switch (m_type) {
    case Type::Members:
        for (const MemberRecord& member : m_memberRows) {
            if (member.id == id) {
                return member.loanCount > 0;
            }
        }
        return false;
    case Type::Books: {
        LoanQuery query;
        query.bookId = id;
        query.archive = ArchiveScope::Any;
        const auto count = m_circulation.countLoans(query);
        return count && count.value() > 0;
    }
    case Type::Copies: {
        const BookCopyRecord* copy = selectedCopy();
        return copy != nullptr && copy->loanCount > 0;
    }
    case Type::Loans:
        return false;
    }
    return false;
}

void ArchivePage::showLoanHistory()
{
    const qint64 id = selectedId();
    if (id <= 0) {
        return;
    }
    LoanQuery query;
    QString name;
    switch (m_type) {
    case Type::Members:
        query.memberId = id;
        for (const MemberRecord& member : m_memberRows) {
            if (member.id == id) {
                name = memberName(member);
            }
        }
        break;
    case Type::Books:
        query.bookId = id;
        for (const BookRecord& book : m_bookRows) {
            if (book.id == id) {
                name = qs(book.title);
            }
        }
        break;
    case Type::Copies:
        query.copyId = id;
        if (const BookCopyRecord* copy = selectedCopy()) {
            name = VLMS::dashIfEmpty(copy->localId) + QStringLiteral(" / ")
                + qs(copy->bookTitle);
        }
        break;
    case Type::Loans:
        return;
    }
    ArchiveLoansDialog dialog(m_circulation, query, name, this);
    dialog.exec();
}

void ArchivePage::onSelectionChanged()
{
    const qint64 id = selectedId();
    const bool anyTicked = !m_checks->checkedIds().isEmpty();
    m_restoreButton->setEnabled(id > 0 || anyTicked);
    // A loan is its own history, so the button is for the other three.
    m_loansButton->setVisible(m_type != Type::Loans);
    m_loansButton->setEnabled(selectedHasLoans());
    m_reuseButton->setVisible(m_type == Type::Copies);
    const BookCopyRecord* copy = selectedCopy();
    m_reuseButton->setEnabled(copy != nullptr && !copy->localId.empty());
    m_purgeButton->setEnabled(selectedMayBePurged() || anyTicked);
    showDetails(id);
}

void ArchivePage::showDetails(const qint64 id)
{
    clearDetails();
    m_coverPath.clear();
    m_photoPath.clear();
    renderImages();
    if (id <= 0) {
        return;
    }
    switch (m_type) {
    case Type::Members:
        for (const MemberRecord& member : m_memberRows) {
            if (member.id == id) {
                m_photoPath = qs(m_members.resolveImagePath(member.photoPath));
                addDetail("archive.col.number", qs(member.membershipNumber));
                addDetail("archive.col.name", memberName(member));
                addDetail("archive.col.city", VLMS::dashIfEmpty(member.city));
                addDetail("archive.col.status", qs(Strings::memberStatusLabel(member.status)));
                addDetail("archive.col.archivedAt", qs(member.archivedAt));
            }
        }
        break;
    case Type::Books:
        for (const BookRecord& book : m_bookRows) {
            if (book.id == id) {
                m_coverPath = qs(m_catalog.resolveCoverPath(book.coverImagePath));
                addDetail("archive.col.title", qs(book.title));
                addDetail("archive.col.author", VLMS::dashIfEmpty(book.authorName));
                addDetail("archive.col.copies", QString::number(book.totalCopies));
                addDetail("archive.col.archivedAt", qs(book.archivedAt));
            }
        }
        break;
    case Type::Copies:
        for (const BookCopyRecord& copy : m_copyRows) {
            if (copy.id == id) {
                m_coverPath = qs(m_catalog.resolveCoverPath(copy.coverImagePath));
                addDetail("archive.col.localId", VLMS::dashIfEmpty(copy.localId));
                addDetail("archive.col.source", sourceLabel(copy.source));
                addDetail("archive.col.title", qs(copy.bookTitle));
                addDetail("archive.col.archivedAt", qs(copy.archivedAt));
                addDetail("archive.col.notes", VLMS::dashIfEmpty(copy.notes));
            }
        }
        break;
    case Type::Loans:
        for (const LoanRecord& loan : m_loanRows) {
            if (loan.id == id) {
                m_coverPath = qs(m_catalog.resolveCoverPath(loan.coverImagePath));
                m_photoPath = qs(m_members.resolveImagePath(loan.memberPhotoPath));
                addDetail("archive.col.member", qs(loan.memberName));
                addDetail("member.field.number", qs(loan.membershipNumber));
                addDetail("archive.col.copy", qs(loan.copyCode) + QStringLiteral(" / ")
                                                  + qs(loan.bookTitle));
                addDetail("archive.col.returnedAt", qs(loan.returnedAt));
                addDetail("archive.col.archivedAt", qs(loan.archivedAt));
                // Last, in its own scrolling box, as Circulation shows notes.
                addDetail("loan.field.notes", VLMS::dashIfEmpty(loan.notes), true);
            }
        }
        break;
    }
    renderImages();
}

void ArchivePage::clearDetails()
{
    for (QWidget* widget : std::as_const(m_detailWidgets)) {
        delete widget;
    }
    m_detailWidgets.clear();
}

void ArchivePage::addDetail(const char* labelKey, const QString& value, const bool scrollable)
{
    auto* detailsLayout = qobject_cast<QVBoxLayout*>(m_detailsPanel->layout());
    if (detailsLayout == nullptr) {
        return;
    }
    auto* field = new QWidget(m_detailsPanel);
    auto* fieldLayout = new QVBoxLayout(field);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(2);

    auto* label = new QLabel(T(labelKey), field);
    label->setObjectName(QStringLiteral("bookDetailLabel"));
    label->setAlignment(Qt::AlignLeading);
    QWidget* valueWidget = VLMS::makeDetailValueWidget(field, scrollable);
    VLMS::setDetailValueText(valueWidget, value);

    fieldLayout->addWidget(label);
    fieldLayout->addWidget(valueWidget);
    detailsLayout->insertWidget(detailsLayout->count() - 1, field);
    m_detailWidgets.append(field);
}

void ArchivePage::renderImages()
{
    if (m_image == nullptr || m_photo == nullptr || m_previewPanel == nullptr) {
        return;
    }
    using VLMS::adaptivePreviewImageSize;
    switch (m_type) {
    case Type::Members:
        m_photo->hide();
        VLMS::showMemberPhoto(
            m_image, m_photoPath,
            adaptivePreviewImageSize(m_previewPanel, VLMS::memberPhotoPreviewBounds()));
        return;
    case Type::Books:
    case Type::Copies:
        m_photo->hide();
        VLMS::showBookCover(
            m_image, m_coverPath,
            adaptivePreviewImageSize(m_previewPanel, VLMS::bookCoverPreviewBounds()));
        return;
    case Type::Loans: {
        m_photo->show();
        // One size for both, as in Circulation.
        const QSize size =
            adaptivePreviewImageSize(m_previewPanel, VLMS::bookCoverPreviewBounds(), 2);
        VLMS::showBookCover(m_image, m_coverPath, size);
        VLMS::showMemberPhoto(m_photo, m_photoPath, size);
        return;
    }
    }
}

namespace {

const char* bulkNounKey(const ArchivePage::Type type)
{
    switch (type) {
    case ArchivePage::Type::Members:
        return "bulk.noun.members";
    case ArchivePage::Type::Books:
        return "bulk.noun.books";
    case ArchivePage::Type::Copies:
        return "bulk.noun.copies";
    case ArchivePage::Type::Loans:
        return "bulk.noun.loans";
    }
    return "bulk.noun.books";
}

QString cellText(const QTableWidget* table, int row, int column)
{
    const QTableWidgetItem* item = table->item(row, column);
    return item != nullptr ? item->text() : QString();
}

QList<BulkRow> tickedArchiveRows(const TableRowChecks* checks,
                                 const QTableWidget* table,
                                 const ArchivePage::Type type)
{
    QList<BulkRow> rows;
    for (const int row : checks->checkedRows()) {
        const QTableWidgetItem* first = table->item(row, 0);
        if (first == nullptr) {
            continue;
        }
        BulkRow bulk;
        bulk.id = first->data(Qt::UserRole).toLongLong();
        bulk.row = row;
        switch (type) {
        case ArchivePage::Type::Members:
            bulk.label = cellText(table, row, 1);
            break;
        case ArchivePage::Type::Books:
            bulk.label = first->text();
            break;
        case ArchivePage::Type::Copies:
            bulk.label = first->text() + QStringLiteral(" — ") + cellText(table, row, 2);
            break;
        case ArchivePage::Type::Loans: {
            const QString copyTitle = cellText(table, row, 1);
            const int slash = copyTitle.indexOf(QStringLiteral(" / "));
            const QString title = slash >= 0 ? copyTitle.mid(slash + 3) : copyTitle;
            bulk.label = first->text() + QStringLiteral(" — ") + title;
            break;
        }
        }
        rows.append(bulk);
    }
    return rows;
}

}  // namespace

void ArchivePage::restoreSelected()
{
    if (!m_checks->checkedIds().isEmpty()) {
        BulkActionTexts texts;
        texts.title = T("archive.restore");
        texts.verb = T("bulk.verb.restore");
        texts.passive = T("bulk.passive.restore");
        texts.noun = T(bulkNounKey(m_type));
        const int done = runBulkAction(
            this, texts, tickedArchiveRows(m_checks, m_table, m_type),
            [](const BulkRow&) { return VLMS::Status::ok(); },
            [this](const BulkRow& bulk) {
                switch (m_type) {
                case Type::Members:
                    return m_members.restoreMember(bulk.id);
                case Type::Books:
                    return m_catalog.restoreBook(bulk.id);
                case Type::Copies:
                    return m_catalog.restoreCopy(bulk.id);
                case Type::Loans:
                    return m_circulation.restoreLoan(bulk.id);
                }
                return VLMS::Status::ok();
            });
        if (done > 0) {
            refreshRows();
            emit recordRestored();
        }
        return;
    }

    const qint64 id = selectedId();
    if (id <= 0) {
        return;
    }
    if (!VLMS::askYesNo(this, T("archive.restore"), T("archive.restoreConfirm"))) {
        return;
    }
    const auto restored = [&]() -> VLMS::Status {
        switch (m_type) {
        case Type::Members:
            return m_members.restoreMember(id);
        case Type::Books:
            return m_catalog.restoreBook(id);
        case Type::Copies:
            return m_catalog.restoreCopy(id);
        case Type::Loans:
            return m_circulation.restoreLoan(id);
        }
        return VLMS::Status::ok();
    }();
    if (!restored) {
        VLMS::showRepoError(this, restored.error());
        return;
    }
    refreshRows();
    emit recordRestored();
}

void ArchivePage::reuseSelected()
{
    const BookCopyRecord* selected = selectedCopy();
    if (selected == nullptr || selected->localId.empty()) {
        return;
    }
    // Copied: the flow's save refreshes nothing here, but the next refresh
    // would free m_copyRows under a pointer into it.
    const BookCopyRecord copy = *selected;
    emit reuseNumberRequested(copy.id);
    if (VLMS::runReuseNumberFlow(this, m_catalog, copy)) {
        refreshRows();
        emit recordRestored();
    }
}

void ArchivePage::purgeSelected()
{
    if (!m_checks->checkedIds().isEmpty()) {
        BulkActionTexts texts;
        texts.title = T("archive.purge");
        texts.verb = T("bulk.verb.purge");
        texts.passive = T("bulk.passive.purge");
        texts.noun = T(bulkNounKey(m_type));
        const int done = runBulkAction(
            this, texts, tickedArchiveRows(m_checks, m_table, m_type),
            [this](const BulkRow& bulk) {
                switch (m_type) {
                case Type::Members:
                    return m_members.canPurgeMember(bulk.id);
                case Type::Books:
                    return m_catalog.canPurgeBook(bulk.id);
                case Type::Copies:
                    return m_catalog.canPurgeCopy(bulk.id);
                case Type::Loans:
                    return m_circulation.canPurgeLoan(bulk.id);
                }
                return VLMS::Status::ok();
            },
            [this](const BulkRow& bulk) {
                switch (m_type) {
                case Type::Members:
                    return m_members.purgeMember(bulk.id);
                case Type::Books:
                    return m_catalog.purgeBook(bulk.id);
                case Type::Copies:
                    return m_catalog.purgeCopy(bulk.id);
                case Type::Loans:
                    return m_circulation.purgeLoan(bulk.id);
                }
                return VLMS::Status::ok();
            });
        if (done > 0) {
            refreshRows();
        }
        return;
    }

    const qint64 id = selectedId();
    if (id <= 0) {
        return;
    }
    const char* confirmKey = "archive.purgeConfirmMember";
    switch (m_type) {
    case Type::Members:
        confirmKey = "archive.purgeConfirmMember";
        break;
    case Type::Books:
        confirmKey = "archive.purgeConfirmBook";
        break;
    case Type::Copies:
        confirmKey = "archive.purgeConfirmCopy";
        break;
    case Type::Loans:
        confirmKey = "archive.purgeConfirmLoan";
        break;
    }
    if (!VLMS::askYesNo(this, T("archive.purge"), T(confirmKey))) {
        return;
    }

    const auto removed = [&]() -> VLMS::Status {
        switch (m_type) {
        case Type::Members:
            return m_members.purgeMember(id);
        case Type::Books:
            return m_catalog.purgeBook(id);
        case Type::Copies:
            return m_catalog.purgeCopy(id);
        case Type::Loans:
            return m_circulation.purgeLoan(id);
        }
        return VLMS::Status::ok();
    }();
    if (!removed) {
        // The button is disabled when the gate is shut, so this is a race or a
        // rule the row could not see -- either way it is the repository's word.
        VLMS::showRepoError(this, removed.error());
        return;
    }

    // No recordRestored(): nothing went back to a live page, and the live lists
    // never showed this row.
    refreshRows();
}
