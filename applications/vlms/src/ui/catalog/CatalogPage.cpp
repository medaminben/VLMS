#include "ui/catalog/CatalogPage.h"

#include <VLMS/Core/Strings.h>
#include "ui/BulkAction.h"
#include "ui/ListPageFrame.h"
#include "ui/TableRowChecks.h"
#include "ui/TablePager.h"
#include "ui/Theme.h"
#include "ui/UiHelpers.h"
#include "QtBridge.h"
#include "ui/catalog/BookEditorDialog.h"
#include "ui/catalog/BookFacetFilters.h"
#include "ui/catalog/BookLoansDialog.h"
#include "ui/catalog/LocalNumberDelegate.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPair>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStringList>
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

constexpr int kTitleColumnWidth = 320;
constexpr int kAuthorColumnWidth = 200;
constexpr int kPreviewPanelMinWidth = 200;
constexpr int kLocalNumberColumnWidth = 110;
constexpr int kLocalNumberColumn = 3;

QPixmap bookCoverPlaceholder(const QSize& size) {
    static const QPixmap source(QStringLiteral(":/images/book-placeholder.png"));
    if (source.isNull()) {
        return {};
    }
    // Recoloured after scaling, on the smaller image: the artwork ships in the
    // light palette and would otherwise be a white card on a dark window.
    return VLMS::themedArtwork(
        source.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void showCoverPlaceholder(QLabel* label, const QSize& size) {
    label->setText({});
    label->setPixmap(bookCoverPlaceholder(size));
}

QStringList bookDetailLabelKeys() {
    return {
        QStringLiteral("book.field.description"),
        QStringLiteral("book.field.title"),
        QStringLiteral("book.field.author"),
        QStringLiteral("book.field.publisher"),
        QStringLiteral("book.field.category"),
        QStringLiteral("book.field.isbn"),
        QStringLiteral("book.field.publicationDate"),
        QStringLiteral("book.field.place"),
        QStringLiteral("book.field.pages"),
        QStringLiteral("book.field.dimensions"),
        QStringLiteral("book.field.language"),
        QStringLiteral("catalog.col.copies"),
        QStringLiteral("catalog.col.available"),
    };
}

}  // namespace

CatalogPage::CatalogPage(VLMS::Repositories::CatalogRepository& repository,
                         VLMS::Repositories::CirculationRepository& circulation,
                         QWidget* parent)
    : QWidget(parent),
      m_repository(repository),
      m_circulation(circulation) {
    buildUi();
    retranslateUi();
}

void CatalogPage::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* frame = new VLMS::ListPageFrame(this);
    VLMS::ListPageFrame::ListConfig config;
    config.subtitle = T("page.catalog.body");
    config.tableColumnCount = 6;
    config.tableColumnWidths = {
        kTitleColumnWidth, kAuthorColumnWidth, 130, kLocalNumberColumnWidth, 70, 70};
    config.imageObjectName = QStringLiteral("bookCover");
    config.imageBounds = VLMS::bookCoverPreviewBounds();
    config.detailsScrollObjectName = QStringLiteral("bookDetailsScroll");
    config.previewPanelMinWidth = kPreviewPanelMinWidth;
    frame->buildList(config);
    rootLayout->addWidget(frame);

    m_filters = new VLMS::BookFacetFilters(
        m_repository, VLMS::Repositories::ArchiveScope::Live, frame->filterColumn());
    connect(m_filters, &VLMS::BookFacetFilters::changed,
            this, &CatalogPage::resetPagerAndRefresh);
    frame->addFilter(m_filters, 1);

    m_searchEdit = frame->searchEdit();
    connect(m_searchEdit, &QLineEdit::textChanged, this, &CatalogPage::onSearchChanged);

    m_loansButton = VLMS::makeSecondaryButton({});
    m_addButton = VLMS::makePrimaryButton({});
    m_editButton = VLMS::makeSecondaryButton({});
    m_deleteButton = VLMS::makeSecondaryButton({});
    connect(m_loansButton, &QPushButton::clicked, this, &CatalogPage::showLoanHistory);
    connect(m_addButton, &QPushButton::clicked, this, &CatalogPage::addBook);
    connect(m_editButton, &QPushButton::clicked, this, &CatalogPage::editBook);
    connect(m_deleteButton, &QPushButton::clicked, this, &CatalogPage::deleteBook);
    frame->addButton(m_loansButton);
    frame->addButton(m_addButton);
    frame->addButton(m_editButton);
    frame->addButton(m_deleteButton);

    m_booksTable = frame->table();
    m_checks = new TableRowChecks(m_booksTable, this);
    connect(m_booksTable, &QTableWidget::itemSelectionChanged, this, &CatalogPage::onSelectionChanged);
    m_booksTable->setItemDelegateForColumn(kLocalNumberColumn,
                                           new LocalNumberDelegate(m_booksTable));
    m_pager = frame->pager();
    connect(m_pager, &VLMS::TablePager::pageChanged, this, &CatalogPage::refreshBooks);

    m_sort = new VLMS::TableHeaderSort(m_booksTable, this);
    m_sort->setColumnKeys({
        QString::fromLatin1(VLMS::Repositories::BookSort::kTitle),
        QString::fromLatin1(VLMS::Repositories::BookSort::kAuthor),
        QString::fromLatin1(VLMS::Repositories::BookSort::kCategory),
        QString::fromLatin1(VLMS::Repositories::BookSort::kLocalNumber),
        QString::fromLatin1(VLMS::Repositories::BookSort::kCopies),
        QString::fromLatin1(VLMS::Repositories::BookSort::kAvailable),
    });
    connect(m_sort, &VLMS::TableHeaderSort::sortChanged,
            this, &CatalogPage::onSortChanged);

    m_coverPreview = frame->imageLabel();
    m_previewPanel = frame->previewPanel();
    m_detailsPanel = frame->detailsPanel();
    showCoverPlaceholder(
        m_coverPreview,
        VLMS::adaptivePreviewImageSize(m_previewPanel, VLMS::bookCoverPreviewBounds()));

    auto* detailsLayout = qobject_cast<QVBoxLayout*>(m_detailsPanel->layout());
    const QStringList detailKeys = bookDetailLabelKeys();
    m_detailRows.reserve(detailKeys.size());
    for (const QString& key : detailKeys) {
        BookDetailRow detail;
        detail.labelKey = key;

        auto* fieldWidget = new QWidget(m_detailsPanel);
        auto* fieldLayout = new QVBoxLayout(fieldWidget);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(2);

        detail.label = new QLabel(fieldWidget);
        detail.label->setObjectName(QStringLiteral("bookDetailLabel"));
        detail.label->setAlignment(Qt::AlignLeading);

        const bool scrollable = key == QStringLiteral("book.field.description");
        detail.value = VLMS::makeDetailValueWidget(fieldWidget, scrollable);

        fieldLayout->addWidget(detail.label);
        fieldLayout->addWidget(detail.value);

        detailsLayout->addWidget(fieldWidget);
        m_detailRows.append(detail);
    }
    detailsLayout->addStretch(1);

    m_previewPanel->installEventFilter(this);
}

void CatalogPage::retranslateUi()
{
    if (auto* title = findChild<QLabel*>(QStringLiteral("pageTitle"))) {
        title->setText(T("page.catalog.title"));
    }
    if (auto* subtitle = findChild<QLabel*>(QStringLiteral("pageSubtitle"))) {
        subtitle->setText(T("page.catalog.body"));
    }

    m_searchEdit->setPlaceholderText(T("catalog.searchPlaceholder"));
    m_addButton->setText(T("catalog.addBook"));
    m_editButton->setText(T("catalog.edit"));
    m_deleteButton->setText(T("catalog.delete"));
    m_loansButton->setText(T("catalog.loans"));
    m_pager->retranslateUi();

    m_booksTable->setHorizontalHeaderLabels({
        T("catalog.col.title"),
        T("catalog.col.author"),
        T("catalog.col.category"),
        T("catalog.col.localNumber"),
        T("catalog.col.copies"),
        T("catalog.col.available"),
    });

    m_filters->refresh();

    const qint64 selectedId = selectedBookId();
    refreshBooks();
    selectBookId(selectedId);

    refreshSelectedBookPreview();
}

void CatalogPage::resetPagerAndRefresh() {
    m_pager->resetToFirstPage();
    refreshBooks();
}

VLMS::Repositories::BookQuery CatalogPage::currentBookQuery() const {
    VLMS::Repositories::BookQuery query;
    query.search = ss(m_searchEdit->text());
    query.categoryCodes = svl(m_filters->categoryCodes());
    query.languages = svl(m_filters->languages());
    query.coverFilter = m_filters->coverFilter();
    query.limit = m_pager->pageSize();
    query.offset = m_pager->offset();
    if (m_sort != nullptr && m_sort->isActive()) {
        query.sortColumn = ss(m_sort->columnKey());
        query.sortAscending = m_sort->ascending();
    }
    return query;
}

void CatalogPage::refreshBooks() {
    VLMS::Repositories::BookQuery query = currentBookQuery();
    const auto totalCount = m_repository.countBooks(query);
    if (!totalCount) {
        VLMS::showRepoError(this, totalCount.error());
        return;
    }
    m_pager->setTotalCount(totalCount.value());

    query.limit = m_pager->pageSize();
    query.offset = m_pager->offset();
    const auto booksResult = m_repository.listBooks(query);
    if (!booksResult) {
        VLMS::showRepoError(this, booksResult.error());
        return;
    }
    const auto& books = booksResult.value();
    m_booksTable->setRowCount(books.size());

    for (int row = 0; row < static_cast<int>(books.size()); ++row) {
        const VLMS::Repositories::BookRecord& book = books.at(row);

        auto* titleItem = new QTableWidgetItem(qs(book.title));
        titleItem->setData(Qt::UserRole, QVariant::fromValue(book.id));
        m_booksTable->setItem(row, 0, titleItem);
        m_booksTable->setItem(row, 1, new QTableWidgetItem(qs(book.authorName)));
        m_booksTable->setItem(row, 2, new QTableWidgetItem(qs(book.categoryLabel)));

        // The numbers only, no text: LocalNumberDelegate paints the cell and
        // opens the popup. One item per row, same cost as any other column.
        auto* localItem = new QTableWidgetItem;
        QStringList localNumbers;
        localNumbers.reserve(static_cast<int>(book.localIds.size()));
        for (const std::string& localId : book.localIds) {
            localNumbers.append(qs(localId));
        }
        localItem->setData(LocalNumberDelegate::kNumbersRole, localNumbers);
        // A number search puts the book on screen through one particular copy,
        // which is rarely the lowest one the cell would otherwise show. Mark it
        // the same way a pick from the drop-down does, so the row says why it is
        // here without disturbing the order the numbers are listed in.
        if (!book.matchedLocalId.empty()) {
            localItem->setData(LocalNumberDelegate::kSelectedRole, qs(book.matchedLocalId));
        }
        // Which of those copies are out, so the delegate can colour the number
        // it shows. The subset rides back on the same query that counts the
        // available copies for the column at the end of the row.
        QStringList onLoanNumbers;
        onLoanNumbers.reserve(static_cast<int>(book.localIdsOnLoan.size()));
        for (const std::string& localId : book.localIdsOnLoan) {
            onLoanNumbers.append(qs(localId));
        }
        localItem->setData(LocalNumberDelegate::kOnLoanRole, onLoanNumbers);
        m_booksTable->setItem(row, kLocalNumberColumn, localItem);

        m_booksTable->setItem(row, 4, new QTableWidgetItem(QString::number(book.totalCopies)));
        m_booksTable->setItem(row, 5, new QTableWidgetItem(QString::number(book.availableCopies)));
    }

    if (books.empty()) {
        m_previewCoverPath.clear();
        renderCoverPreview();
        clearBookDetails();
    } else if (selectedBookId() <= 0) {
        VLMS::selectTableRow(m_booksTable, 0);
    }
    m_checks->clear();
}

void CatalogPage::onSearchChanged() {
    resetPagerAndRefresh();
}

qint64 CatalogPage::selectedBookId() const {
    const auto items = m_booksTable->selectedItems();
    if (items.isEmpty()) {
        return 0;
    }
    return m_booksTable->item(items.first()->row(), 0)->data(Qt::UserRole).toLongLong();
}

void CatalogPage::selectBookId(const qint64 id)
{
    if (id <= 0) {
        return;
    }
    for (int row = 0; row < m_booksTable->rowCount(); ++row) {
        if (m_booksTable->item(row, 0)->data(Qt::UserRole).toLongLong() == id) {
            VLMS::selectTableRow(m_booksTable, row);
            return;
        }
    }
}

void CatalogPage::onSortChanged(int, bool)
{
    const qint64 id = selectedBookId();
    if (id <= 0) {
        m_pager->setCurrentPage(1);
    } else {
        VLMS::Repositories::BookQuery query = currentBookQuery();
        const auto rank = m_repository.rankOfBook(id, query);
        if (rank) {
            m_pager->setCurrentPage(rank.value() / m_pager->pageSize() + 1);
        } else {
            m_pager->setCurrentPage(1);
        }
    }
    refreshBooks();
    selectBookId(id);
}

void CatalogPage::onSelectionChanged() {
    const qint64 bookId = selectedBookId();
    if (bookId <= 0) {
        return;
    }

    const auto book = m_repository.getBook(bookId);
    if (!book) {
        if (book.kind() == VLMS::Core::ErrorKind::Sql) {
            VLMS::showRepoError(this, book.error());
        }
        return;
    }

    updateCoverPreview(book.value());
}

void CatalogPage::clearBookDetails() {
    for (BookDetailRow& row : m_detailRows) {
        row.label->clear();
        VLMS::clearDetailValueText(row.value);
    }
}

void CatalogPage::refreshSelectedBookPreview() {
    const qint64 bookId = selectedBookId();
    if (bookId <= 0) {
        // Painted again rather than left: this is also the theme-switch path,
        // and the placeholder already on screen is in the old theme's colours.
        m_previewCoverPath.clear();
        renderCoverPreview();
        clearBookDetails();
        return;
    }

    const auto book = m_repository.getBook(bookId);
    if (!book) {
        if (book.kind() == VLMS::Core::ErrorKind::Sql) {
            VLMS::showRepoError(this, book.error());
        }
        clearBookDetails();
        return;
    }

    updateCoverPreview(book.value());
}

void CatalogPage::renderCoverPreview()
{
    if (m_coverPreview == nullptr || m_previewPanel == nullptr) {
        return;
    }

    const QSize coverSize =
        VLMS::adaptivePreviewImageSize(m_previewPanel, VLMS::bookCoverPreviewBounds());
    VLMS::applyPreviewLabelGeometry(m_coverPreview, coverSize);

    if (m_previewCoverPath.isEmpty()) {
        showCoverPlaceholder(m_coverPreview, coverSize);
        return;
    }

    const QPixmap pixmap(m_previewCoverPath);
    if (pixmap.isNull()) {
        showCoverPlaceholder(m_coverPreview, coverSize);
        return;
    }

    m_coverPreview->setText({});
    m_coverPreview->setPixmap(
        pixmap.scaled(coverSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

bool CatalogPage::eventFilter(QObject* watched, QEvent* event)
{
    if (VLMS::shouldSyncPreviewPanel(watched, m_previewPanel, event)) {
        renderCoverPreview();
    }
    return QWidget::eventFilter(watched, event);
}

void CatalogPage::updateCoverPreview(const VLMS::Repositories::BookRecord& book) {
    if (book.coverImagePath.empty()) {
        m_previewCoverPath.clear();
    } else {
        m_previewCoverPath = qs(m_repository.resolveCoverPath(book.coverImagePath));
    }
    renderCoverPreview();

    const QList<QPair<QString, QString>> rows = {
        {QStringLiteral("book.field.description"), VLMS::dashIfEmpty(book.description)},
        {QStringLiteral("book.field.title"), qs(book.title)},
        {QStringLiteral("book.field.author"), qs(book.authorName)},
        {QStringLiteral("book.field.publisher"), VLMS::dashIfEmpty(book.publisherName)},
        {QStringLiteral("book.field.category"), VLMS::dashIfEmpty(book.categoryLabel)},
        {QStringLiteral("book.field.isbn"), VLMS::dashIfEmpty(book.isbn)},
        {QStringLiteral("book.field.publicationDate"), VLMS::dashIfEmpty(book.publicationDate)},
        {QStringLiteral("book.field.place"), VLMS::dashIfEmpty(book.placeOfPublication)},
        {QStringLiteral("book.field.pages"), VLMS::dashIfEmpty(book.pages)},
        {QStringLiteral("book.field.dimensions"), VLMS::dashIfEmpty(book.dimensions)},
        {QStringLiteral("book.field.language"), qs(VLMS::Core::Strings::bookLanguageLabel(book.language))},
        {QStringLiteral("catalog.col.copies"), QString::number(book.totalCopies)},
        {QStringLiteral("catalog.col.available"), QString::number(book.availableCopies)},
    };

    for (int row = 0; row < rows.size() && row < m_detailRows.size(); ++row) {
        m_detailRows[row].label->setText(T(ss(rows.at(row).first)) + QStringLiteral(":"));
        VLMS::setDetailValueText(m_detailRows[row].value, rows.at(row).second);
    }
}

void CatalogPage::addBook() {
    BookEditorDialog dialog(m_repository, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    VLMS::Repositories::BookWrite write;
    write.book = dialog.bookInput();
    write.copies = dialog.copyInputs();
    if (dialog.coverChanged()) {
        write.coverSourcePath = ss(dialog.coverSourcePath());
    }
    if (const auto created = m_repository.saveNewBook(write); !created) {
        VLMS::showRepoError(this, created.error());
        return;
    }

    m_filters->refresh();
    refreshBooks();
}

void CatalogPage::editBook() {
    const qint64 bookId = selectedBookId();
    if (bookId <= 0) {
        VLMS::showInformation(
            this,
            T("catalog.editBook"),
            T("catalog.selectBookFirst"));
        return;
    }

    const auto book = m_repository.getBook(bookId);
    if (!book) {
        VLMS::showRepoError(this, book.error());
        return;
    }

    BookEditorDialog dialog(m_repository, book.value(), this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    VLMS::Repositories::BookWrite write;
    write.book = dialog.bookInput();
    write.copies = dialog.copyInputs();
    if (dialog.coverChanged()) {
        write.coverSourcePath = ss(dialog.coverSourcePath());
    }
    if (const auto saved = m_repository.saveExistingBook(bookId, write); !saved) {
        VLMS::showRepoError(this, saved.error());
        return;
    }

    m_filters->refresh();
    refreshBooks();
}

void CatalogPage::deleteBook() {
    if (!m_checks->checkedIds().isEmpty()) {
        QList<BulkRow> rows;
        for (const int row : m_checks->checkedRows()) {
            const QTableWidgetItem* item = m_booksTable->item(row, 0);
            if (item == nullptr) {
                continue;
            }
            BulkRow bulk;
            bulk.id = item->data(Qt::UserRole).toLongLong();
            bulk.row = row;
            bulk.label = item->text();
            rows.append(bulk);
        }
        BulkActionTexts texts;
        texts.title = T("catalog.deleteBook");
        texts.verb = T("bulk.verb.archive");
        texts.passive = T("bulk.passive.archive");
        texts.noun = T("bulk.noun.books");
        const int done = runBulkAction(
            this, texts, rows,
            [this](const BulkRow& bulk) { return m_repository.canArchiveBook(bulk.id); },
            [this](const BulkRow& bulk) { return m_repository.archiveBook(bulk.id); });
        if (done > 0) {
            m_filters->refresh();
            refreshBooks();
        }
        return;
    }

    const qint64 bookId = selectedBookId();
    if (bookId <= 0) {
        VLMS::showInformation(
            this,
            T("catalog.deleteBook"),
            T("catalog.selectBookFirst"));
        return;
    }

    // Asked before the confirmation, as the Members page asks: being offered
    // "delete this book?" only to be told afterwards that it was never possible
    // is the long way round to the same no.
    const auto open = m_repository.bookHasOpenLoans(bookId);
    if (!open) {
        VLMS::showRepoError(this, open.error());
        return;
    }
    if (open.value()) {
        VLMS::showInformation(this, T("catalog.deleteBook"),
                                    T("error.book.hasActiveLoans"));
        return;
    }

    if (!VLMS::askYesNo(this,
                              T("catalog.deleteBook"),
                              T("catalog.deleteConfirm"))) {
        return;
    }

    // Moves the title and its copies to the Archive; the confirmation text
    // (catalog.deleteConfirm) says so.
    if (const auto archived = m_repository.archiveBook(bookId); !archived) {
        VLMS::showRepoError(this, archived.error());
        return;
    }

    m_filters->refresh();
    refreshBooks();
}

void CatalogPage::showLoanHistory() {
    const qint64 bookId = selectedBookId();
    if (bookId <= 0) {
        return;
    }

    const auto book = m_repository.getBook(bookId);
    if (!book.has_value()) {
        return;
    }

    BookLoansDialog dialog(m_circulation, bookId, qs(book->title), this);
    dialog.exec();
    // A loan made in there changes the Available column behind it.
    refreshBooks();
    selectBookId(bookId);
}
