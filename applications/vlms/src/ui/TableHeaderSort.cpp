#include "ui/TableHeaderSort.h"
#include "ui/UiHelpers.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

namespace VLMS {

TableHeaderSort::TableHeaderSort(QTableWidget* table, QObject* parent)
    : QObject(parent == nullptr ? table : parent),
      m_table(table)
{
    if (m_table == nullptr) {
        return;
    }
    QHeaderView* header = m_table->horizontalHeader();
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    connect(header, &QHeaderView::sectionClicked, this, &TableHeaderSort::onSectionClicked);
}

void TableHeaderSort::setColumnKeys(const QStringList& keys)
{
    m_keys = keys;
}

void TableHeaderSort::reset()
{
    m_column = -1;
    m_ascending = true;
    if (m_table != nullptr) {
        m_table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    }
}

int TableHeaderSort::column() const { return m_column; }
bool TableHeaderSort::ascending() const { return m_ascending; }
bool TableHeaderSort::isActive() const { return m_column >= 0; }

QString TableHeaderSort::columnKey() const
{
    if (m_column < 0 || m_column >= m_keys.size()) {
        return {};
    }
    return m_keys.at(m_column);
}

void TableHeaderSort::onSectionClicked(const int column)
{
    if (m_column == column) {
        m_ascending = !m_ascending;
    } else {
        m_column = column;
        m_ascending = true;
    }
    m_table->horizontalHeader()->setSortIndicator(
        m_column, m_ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    emit sortChanged(m_column, m_ascending);
}

void enableWidgetTableSort(QTableWidget* table, const int idColumn, const int idRole)
{
    if (table == nullptr) {
        return;
    }
    auto* sort = new TableHeaderSort(table, table);
    QObject::connect(sort, &TableHeaderSort::sortChanged, table,
                     [table, idColumn, idRole](int column, bool ascending) {
                         qint64 selected = 0;
                         if (table->selectionMode() != QAbstractItemView::NoSelection) {
                             const int row = table->currentRow();
                             if (row >= 0 && table->item(row, idColumn) != nullptr) {
                                 selected = table->item(row, idColumn)->data(idRole).toLongLong();
                             }
                         }
                         table->sortItems(column,
                                          ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
                         if (selected <= 0) {
                             return;
                         }
                         for (int row = 0; row < table->rowCount(); ++row) {
                             const QTableWidgetItem* item = table->item(row, idColumn);
                             if (item != nullptr
                                 && item->data(idRole).toLongLong() == selected) {
                                 selectTableRow(table, row);
                                 return;
                             }
                         }
                     });
}

}  // namespace VLMS
