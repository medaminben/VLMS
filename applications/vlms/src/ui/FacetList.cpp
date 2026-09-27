#include "ui/FacetList.h"

#include <algorithm>

#include <QAbstractItemView>
#include <QListWidgetItem>
#include <QSignalBlocker>

namespace VLMS {

FacetList::FacetList(const QString& objectName, const bool multiSelect, QWidget* parent)
    : QListWidget(parent)
{
    setObjectName(objectName);
    setSelectionMode(multiSelect ? QAbstractItemView::MultiSelection
                                 : QAbstractItemView::SingleSelection);
    connect(this, &QListWidget::itemSelectionChanged, this, &FacetList::onSelectionChanged);
}

void FacetList::setEntries(const QString& allLabel, const QList<Entry>& entries)
{
    const QStringList previouslySelected = selectedCodes();

    const QSignalBlocker blocker(this);
    m_updating = true;

    clear();
    auto* allItem = new QListWidgetItem(allLabel);
    allItem->setData(Qt::UserRole, QString());
    addItem(allItem);

    bool restored = false;
    for (const Entry& entry : entries) {
        auto* item = new QListWidgetItem(entry.label);
        item->setData(Qt::UserRole, entry.code);
        addItem(item);
        if (previouslySelected.contains(entry.code)
            && (selectionMode() == QAbstractItemView::MultiSelection || !restored)) {
            item->setSelected(true);
            restored = true;
        }
    }
    if (!restored) {
        allItem->setSelected(true);
    }

    m_updating = false;
}

QStringList FacetList::selectedCodes() const
{
    QStringList codes;
    const auto selected = selectedItems();
    for (const QListWidgetItem* item : selected) {
        const QString code = item->data(Qt::UserRole).toString();
        if (!code.isEmpty()) {
            codes.append(code);
        }
    }
    return codes;
}

void FacetList::selectAll()
{
    if (count() == 0) {
        return;
    }
    const QSignalBlocker blocker(this);
    m_updating = true;
    clearSelection();
    item(0)->setSelected(true);
    m_updating = false;
}

void FacetList::fitRows(const int maxVisibleRows)
{
    if (count() == 0) {
        return;
    }
    // Measured with the theme's frame and padding: a list with no scrollbar
    // that is a few pixels short scrolls its first row out of sight on a pick.
    ensurePolished();
    const int visible = std::min(count(), std::max(maxVisibleRows, 1));
    int rows = 0;
    for (int row = 0; row < visible; ++row) {
        rows += std::max(sizeHintForRow(row), 20);
    }
    const QMargins margins = contentsMargins();
    setFixedHeight(rows + 2 * frameWidth() + margins.top() + margins.bottom());
    setVerticalScrollBarPolicy(count() > visible ? Qt::ScrollBarAsNeeded
                                                 : Qt::ScrollBarAlwaysOff);
}

void FacetList::onSelectionChanged()
{
    if (m_updating || count() == 0) {
        return;
    }

    QListWidgetItem* allItem = item(0);
    const QList<QListWidgetItem*> selected = selectedItems();

    m_updating = true;
    if (selected.isEmpty()) {
        allItem->setSelected(true);
    } else if (selectionMode() == QAbstractItemView::MultiSelection
               && selected.contains(allItem) && selected.size() > 1) {
        if (currentItem() == allItem) {
            for (int row = 1; row < count(); ++row) {
                item(row)->setSelected(false);
            }
        } else {
            allItem->setSelected(false);
        }
    }
    m_updating = false;

    emit selectionEdited();
}

}  // namespace VLMS
