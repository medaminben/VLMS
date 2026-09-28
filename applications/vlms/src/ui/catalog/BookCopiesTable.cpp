#include "ui/catalog/BookCopiesTable.h"

#include "ui/catalog/FreeLocalNumberDelegate.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Core/Result.h>
#include <VLMS/Core/Strings.h>
#include "ui/BulkAction.h"
#include "ui/TableHeaderSort.h"
#include "ui/TableRowChecks.h"
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
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

enum CopyColumn {
    kCopyLocalId = 0,
    kCopyGlobalId,
    kCopySource,
    kCopyCentralId,
    kCopyClassification,
    kCopySubject,
    kCopyIndexCode,
    kCopyLocation,
    kCopyInventoryStatus,
    kCopyCompensation,
    kCopyNotes,
    kCopyStatus,
    kCopyColumnCount,
};

const QStringList kCopyColumnKeys = {
    QStringLiteral("book.copy.localId"),
    QStringLiteral("book.copy.globalId"),
    QStringLiteral("book.copy.source"),
    QStringLiteral("book.copy.centralId"),
    QStringLiteral("book.copy.classification"),
    QStringLiteral("book.copy.subject"),
    QStringLiteral("book.copy.indexCode"),
    QStringLiteral("book.copy.location"),
    QStringLiteral("book.copy.inventoryStatus"),
    QStringLiteral("book.copy.compensation"),
    QStringLiteral("book.copy.notes"),
    QStringLiteral("book.copy.status"),
};

constexpr int kMaxCopyColumnWidth = 220;
// The row tick, the drop-down arrow and the editor frame around the digits.
constexpr int kLocalNumberEditorChrome = 104;
constexpr int kCopyIdRole = Qt::UserRole + 1;
constexpr int kCopyOnLoanRole = Qt::UserRole + 2;
constexpr int kCopyReservedRole = Qt::UserRole + 3;
constexpr int kFreeNumberLimit = 100;

}  // namespace

BookCopiesTable::BookCopiesTable(VLMS::Repositories::CatalogRepository& repository, QWidget* parent)
    : QWidget(parent),
      m_repository(repository)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    m_table = new QTableWidget(0, kCopyColumnCount, this);
    m_table->setObjectName(QStringLiteral("copiesTable"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_checks = new TableRowChecks(m_table, this);
    VLMS::enableWidgetTableSort(m_table, kCopyLocalId, kCopyIdRole);
    m_table->setItemDelegateForColumn(kCopyLocalId, new FreeLocalNumberDelegate(m_table));
    layout->addWidget(m_table, 1);

    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    m_addButton = VLMS::makeSecondaryButton({});
    m_removeButton = VLMS::makeSecondaryButton({});
    connect(m_addButton, &QPushButton::clicked, this, &BookCopiesTable::addRequested);
    connect(m_removeButton, &QPushButton::clicked, this, &BookCopiesTable::removeSelectedRow);
    buttons->addWidget(m_addButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);
}

void BookCopiesTable::retranslateUi()
{
    QStringList headers;
    headers.reserve(kCopyColumnKeys.size());
    for (const QString& key : kCopyColumnKeys) {
        headers.append(T(ss(key)));
    }
    m_table->setHorizontalHeaderLabels(headers);
    m_addButton->setText(T("book.copy.add"));
    m_removeButton->setText(T("book.copy.remove"));
    // The headers are what most columns are sized by, and they only now exist.
    sizeCopyColumns();
}

void BookCopiesTable::loadCopies(const qint64 bookId)
{
    m_table->setRowCount(0);
    const auto copies = m_repository.listCopies(bookId);
    if (!copies) {
        VLMS::showRepoError(this, copies.error());
        return;
    }
    for (const VLMS::Repositories::BookCopyRecord& copy : copies.value()) {
        appendCopyRow(copy);
    }
    sizeCopyColumns();
    m_checks->clear();
}

void BookCopiesTable::addRow(const QString& language)
{
    VLMS::Repositories::BookCopyRecord copy;
    std::string source;
    std::string localId;
    std::string globalCopyId;
    m_repository.suggestCopyIdentifiers(ss(language), &source, &localId, &globalCopyId);
    copy.source = source;
    copy.localId = localId;
    copy.globalCopyId = globalCopyId;

    // The incremented number first -- it is what the cell opens on and what a
    // save uses if nobody touches the list. The gaps follow, capped: a
    // drop-down with 5,976 entries is not a choice, it is a haystack.
    QStringList offered{qs(localId)};
    if (const auto free = m_repository.listFreeLocalNumbers(source, kFreeNumberLimit)) {
        for (const std::string& number : free.value()) {
            offered.append(qs(number));
        }
    }

    appendCopyRow(copy);
    if (QTableWidgetItem* numberCell = m_table->item(m_table->rowCount() - 1, kCopyLocalId)) {
        numberCell->setData(FreeLocalNumberDelegate::kFreeNumbersRole, offered);
    }
    const int row = m_table->rowCount() - 1;
    sizeCopyColumns();
    VLMS::selectTableRow(m_table, row);
    m_table->scrollToBottom();
    m_table->editItem(m_table->item(row, kCopyLocalId));
}

void BookCopiesTable::addReservedRow(const QString& source,
                                     const QString& localId,
                                     const QString& globalCopyId)
{
    VLMS::Repositories::BookCopyRecord copy;
    copy.source = ss(source);
    copy.localId = ss(localId);
    copy.globalCopyId = ss(globalCopyId);
    appendCopyRow(copy);

    // The number is the reason this row exists: it must reach the save that
    // moves it exactly as the archived copy held it.
    const int row = m_table->rowCount() - 1;
    for (const int column : {kCopyLocalId, kCopyGlobalId}) {
        if (QTableWidgetItem* item = m_table->item(row, column)) {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        }
    }
    if (QWidget* combo = m_table->cellWidget(row, kCopySource)) {
        combo->setEnabled(false);
    }
    if (QTableWidgetItem* first = m_table->item(row, kCopyLocalId)) {
        first->setData(kCopyReservedRole, true);
    }
    sizeCopyColumns();
    VLMS::selectTableRow(m_table, row);
}

int BookCopiesTable::copyCount() const
{
    return m_table->rowCount();
}

std::vector<VLMS::Repositories::BookCopyInput> BookCopiesTable::copyInputs() const
{
    std::vector<VLMS::Repositories::BookCopyInput> copies;
    copies.reserve(static_cast<std::size_t>(m_table->rowCount()));

    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto cell = [this, row](int column) {
            const QTableWidgetItem* item = m_table->item(row, column);
            return item == nullptr ? std::string() : ss(item->text().trimmed());
        };

        VLMS::Repositories::BookCopyInput copy;
        const QTableWidgetItem* first = m_table->item(row, kCopyLocalId);
        copy.id = first == nullptr ? 0 : first->data(kCopyIdRole).toLongLong();
        copy.localId = cell(kCopyLocalId);
        copy.globalCopyId = cell(kCopyGlobalId);
        copy.centralId = cell(kCopyCentralId);
        copy.classification = cell(kCopyClassification);
        copy.subject = cell(kCopySubject);
        copy.indexCode = cell(kCopyIndexCode);
        copy.location = cell(kCopyLocation);
        copy.inventoryStatus = cell(kCopyInventoryStatus);
        copy.compensation = cell(kCopyCompensation);
        copy.notes = cell(kCopyNotes);

        if (auto* combo = qobject_cast<QComboBox*>(m_table->cellWidget(row, kCopySource))) {
            copy.source = ss(combo->currentData().toString());
        }

        copies.push_back(copy);
    }

    return copies;
}

bool BookCopiesTable::validate(QWidget* dialogParent)
{
    QSet<QString> seenGlobalIds;
    QSet<QString> seenSourceLocal;
    const std::vector<VLMS::Repositories::BookCopyInput> copies = copyInputs();

    for (int row = 0; row < static_cast<int>(copies.size()); ++row) {
        const VLMS::Repositories::BookCopyInput& copy = copies[static_cast<std::size_t>(row)];

        const auto complain = [this, dialogParent, row](const QString& key,
                                                        const QString& identifier) {
            emit requestCopiesTab();
            VLMS::selectTableRow(m_table, row);
            VLMS::showWarning(
                dialogParent,
                T("book.validation"),
                identifier.isEmpty()
                    ? T(ss(key))
                    : T(ss(key)).replace(QStringLiteral("{identifier}"), identifier));
            return false;
        };

        if (copy.localId.empty() || copy.globalCopyId.empty()) {
            return complain(QStringLiteral("book.copy.identifierRequired"), {});
        }
        if (seenGlobalIds.contains(qs(copy.globalCopyId))) {
            return complain(QStringLiteral("book.copy.duplicateIdentifier"), qs(copy.globalCopyId));
        }
        const QString sourceLocal = qs(copy.source) + QChar('/') + qs(copy.localId);
        if (seenSourceLocal.contains(sourceLocal)) {
            return complain(QStringLiteral("book.copy.duplicateIdentifier"), qs(copy.localId));
        }
        seenGlobalIds.insert(qs(copy.globalCopyId));
        seenSourceLocal.insert(sourceLocal);
    }

    return true;
}

void BookCopiesTable::appendCopyRow(const VLMS::Repositories::BookCopyRecord& copy)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    const auto setCell = [this, row](int column, const QString& text) {
        auto* item = new QTableWidgetItem(text);
        m_table->setItem(row, column, item);
        return item;
    };

    QTableWidgetItem* first = setCell(kCopyLocalId, qs(copy.localId));
    first->setData(kCopyIdRole, QVariant::fromValue(copy.id));
    first->setData(kCopyOnLoanRole, copy.onLoan);

    setCell(kCopyGlobalId, qs(copy.globalCopyId));
    setCell(kCopySource, qs(copy.source));
    setCell(kCopyCentralId, qs(copy.centralId));
    setCell(kCopyClassification, qs(copy.classification));
    setCell(kCopySubject, qs(copy.subject));
    setCell(kCopyIndexCode, qs(copy.indexCode));
    setCell(kCopyLocation, qs(copy.location));
    setCell(kCopyInventoryStatus, qs(copy.inventoryStatus));
    setCell(kCopyCompensation, qs(copy.compensation));
    setCell(kCopyNotes, qs(copy.notes));

    auto* sourceCombo = new QComboBox(m_table);
    sourceCombo->addItem(T("book.copy.source.arabic"),
                         QStringLiteral("arabic"));
    sourceCombo->addItem(T("book.copy.source.foreign"),
                         QStringLiteral("foreign"));
    const int sourceIndex = sourceCombo->findData(qs(copy.source));
    sourceCombo->setCurrentIndex(sourceIndex >= 0 ? sourceIndex : 1);
    m_table->setCellWidget(row, kCopySource, sourceCombo);

    auto* status = setCell(kCopyStatus,
                           T(copy.onLoan ? "book.copy.statusOnLoan"
                                         : "book.copy.statusAvailable"));
    status->setFlags(status->flags() & ~Qt::ItemIsEditable);
    m_checks->clear();
}

void BookCopiesTable::sizeCopyColumns()
{
    QHeaderView* header = m_table->horizontalHeader();
    // Measured with the stylesheet's bold header font and padding, not the
    // plain font an unpolished header has. And measured here, from the size
    // hints: the dialog is not shown yet, and a hidden header puts off
    // ResizeToContents, so sectionSize() handed back Qt's default widths and
    // Arabic headers such as الرقم المركزي were clipped at both ends.
    m_table->ensurePolished();
    header->ensurePolished();

    QList<int> widths;
    widths.reserve(m_table->columnCount());
    for (int column = 0; column < m_table->columnCount(); ++column) {
        m_table->resizeColumnToContents(column);
        int width = qMax(header->sectionSize(column), header->sectionSizeHint(column));
        // The size hints read the items, not the widgets placed over them:
        // the stock drop-down was cut to «عرب» and «Ara» without this.
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (QWidget* cell = m_table->cellWidget(row, column)) {
                cell->ensurePolished();
                width = qMax(width, cell->sizeHint().width());
            }
        }
        if (column == kCopyLocalId) {
            // The number is edited in a drop-down that opens over the cell, and
            // a short header ("N° local") left it too narrow for the digits.
            const int digits = m_table->fontMetrics().horizontalAdvance(QStringLiteral("0000000"));
            width = qMax(width, digits + kLocalNumberEditorChrome);
        }
        widths.append(qMin(width, kMaxCopyColumnWidth));
    }

    header->setSectionResizeMode(QHeaderView::Interactive);
    for (int column = 0; column < widths.size(); ++column) {
        m_table->setColumnWidth(column, widths.at(column));
    }
}

void BookCopiesTable::removeSelectedRow()
{
    if (!m_checks->checkedRows().isEmpty()) {
        QList<int> indexes = m_checks->checkedRows();
        std::sort(indexes.begin(), indexes.end(), std::greater<int>());
        QList<BulkRow> rows;
        for (const int row : indexes) {
            const QTableWidgetItem* item = m_table->item(row, kCopyLocalId);
            BulkRow bulk;
            bulk.row = row;
            bulk.id = item != nullptr ? item->data(kCopyIdRole).toLongLong() : 0;
            bulk.label = item != nullptr ? item->text() : QString::number(row);
            rows.append(bulk);
        }
        BulkActionTexts texts;
        texts.title = T("book.tab.copies");
        texts.verb = T("bulk.verb.remove");
        texts.passive = T("bulk.passive.remove");
        texts.noun = T("bulk.noun.copies");
        runBulkAction(
            this, texts, rows,
            [this](const BulkRow& bulk) {
                const QTableWidgetItem* item = m_table->item(bulk.row, kCopyLocalId);
                if (item != nullptr && item->data(kCopyOnLoanRole).toBool()) {
                    return VLMS::Status::fail(VLMS::ErrorKind::Validation,
                                                    "book.copy.cannotRemoveOnLoan");
                }
                if (item != nullptr && item->data(kCopyReservedRole).toBool()) {
                    return VLMS::Status::fail(VLMS::ErrorKind::Validation,
                                                    "book.copy.numberReserved");
                }
                return VLMS::Status::ok();
            },
            [this](const BulkRow& bulk) {
                m_table->removeRow(bulk.row);
                return VLMS::Status::ok();
            });
        return;
    }

    const int row = m_table->currentRow();
    if (row < 0) {
        VLMS::showInformation(
            this,
            T("book.tab.copies"),
            T("book.copy.selectFirst"));
        return;
    }

    const QTableWidgetItem* first = m_table->item(row, kCopyLocalId);
    if (first != nullptr && first->data(kCopyOnLoanRole).toBool()) {
        VLMS::showWarning(
            this,
            T("book.validation"),
            T("book.copy.cannotRemoveOnLoan"));
        return;
    }
    if (first != nullptr && first->data(kCopyReservedRole).toBool()) {
        // The reserved number must stay on a live row until save, or Accept
        // would release it with nothing holding it.
        return;
    }

    m_table->removeRow(row);
}
