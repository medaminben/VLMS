#include "ui/TableRowChecks.h"

#include <QEvent>
#include <QHeaderView>
#include <QMouseEvent>
#include <QWidget>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionHeader>
#include <QStyleOptionViewItem>
#include <QTableWidget>
#include <QTableWidgetItem>

constexpr int kIndicatorMargin = 4;

class CheckHeaderView : public QHeaderView {
public:
    explicit CheckHeaderView(TableRowChecks* checks, QWidget* parent)
        : QHeaderView(Qt::Horizontal, parent)
        , m_checks(checks)
    {
    }

    void setTriState(const Qt::CheckState state)
    {
        if (m_state == state) {
            return;
        }
        m_state = state;
        viewport()->update();
    }

protected:
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override
    {
        if (logicalIndex != 0) {
            QHeaderView::paintSection(painter, rect, logicalIndex);
            return;
        }

        // The label is painted in what is left after the box, so the first
        // letters are not hidden under it and a click on them still sorts.
        const QRect box = indicatorRect(rect);
        const int inset = box.width() + kIndicatorMargin;
        QRect labelRect = rect;
        QRect strip = rect;
        if (layoutDirection() == Qt::RightToLeft) {
            labelRect.setRight(rect.right() - inset);
            strip.setLeft(labelRect.right() + 1);
        } else {
            labelRect.setLeft(rect.left() + inset);
            strip.setRight(labelRect.left() - 1);
        }

        // The stylesheet clips the painter to the label and does not put the
        // clip back. Anything drawn after that, including the checkbox, is
        // clipped away. Save around the label and draw the box after restore.
        painter->save();
        QStyleOptionHeader background;
        initStyleOption(&background);
        background.rect = rect;
        background.section = logicalIndex;
        background.text.clear();
        background.sortIndicator = QStyleOptionHeader::None;
        style()->drawControl(QStyle::CE_Header, &background, painter, this);

        QHeaderView::paintSection(painter, labelRect, logicalIndex);

        QStyleOptionHeader cover;
        initStyleOption(&cover);
        cover.rect = strip;
        cover.section = logicalIndex;
        cover.text.clear();
        cover.sortIndicator = QStyleOptionHeader::None;
        style()->drawControl(QStyle::CE_Header, &cover, painter, this);
        painter->restore();

        QStyleOptionButton option;
        option.initFrom(this);
        option.rect = box;
        option.state |= QStyle::State_Enabled;
        if (m_state == Qt::Checked) {
            option.state |= QStyle::State_On;
        } else if (m_state == Qt::PartiallyChecked) {
            option.state |= QStyle::State_NoChange;
        } else {
            option.state |= QStyle::State_Off;
        }
        style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, painter, this);
    }

    // The label is painted beside the box, so a section sized to its contents
    // has to count the box too, or the label loses its last letters to it.
    [[nodiscard]] QSize sectionSizeFromContents(int logicalIndex) const override
    {
        QSize size = QHeaderView::sectionSizeFromContents(logicalIndex);
        if (logicalIndex == 0) {
            size.rwidth() += style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, this)
                             + kIndicatorMargin * 2;
        }
        return size;
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        m_pressedInBox = indicatorRect(sectionZero()).contains(event->position().toPoint());
        if (m_pressedInBox) {
            event->accept();
            return;
        }
        QHeaderView::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        const bool inside = indicatorRect(sectionZero()).contains(event->position().toPoint());
        if (m_pressedInBox) {
            m_pressedInBox = false;
            if (inside) {
                m_checks->toggleAll();
            }
            event->accept();
            return;
        }
        QHeaderView::mouseReleaseEvent(event);
    }

private:
    [[nodiscard]] QRect sectionZero() const
    {
        return QRect(sectionViewportPosition(0), 0, sectionSize(0), height());
    }

    [[nodiscard]] QRect indicatorRect(const QRect& sectionRect) const
    {
        const int size = style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, this);
        const int y = sectionRect.top() + (sectionRect.height() - size) / 2;
        if (layoutDirection() == Qt::RightToLeft) {
            return QRect(sectionRect.right() - kIndicatorMargin - size + 1, y, size, size);
        }
        return QRect(sectionRect.left() + kIndicatorMargin, y, size, size);
    }

    TableRowChecks* m_checks = nullptr;
    Qt::CheckState m_state = Qt::Unchecked;
    bool m_pressedInBox = false;
};

namespace {

QRect cellIndicatorRect(const QTableWidget* table, const QModelIndex& index)
{
    QStyleOptionViewItem option;
    option.initFrom(table);
    option.rect = table->visualRect(index);
    option.features = QStyleOptionViewItem::HasCheckIndicator;
    option.checkState = Qt::Unchecked;
    return table->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, table);
}

}  // namespace

TableRowChecks::TableRowChecks(QTableWidget* table, QObject* parent)
    : QObject(parent == nullptr ? table : parent)
    , m_table(table)
    , m_tableWidget(table)
{
    if (m_table == nullptr) {
        return;
    }
    QHeaderView* previous = m_table->horizontalHeader();
    const int columns = m_table->columnCount();
    QList<int> widths;
    QList<QHeaderView::ResizeMode> modes;
    widths.reserve(columns);
    modes.reserve(columns);
    for (int column = 0; column < columns; ++column) {
        widths.append(previous->sectionSize(column));
        modes.append(previous->sectionResizeMode(column));
    }
    const bool stretchLast = previous->stretchLastSection();
    const bool cascading = previous->cascadingSectionResizes();
    const int minimum = previous->minimumSectionSize();
    const int defaultSize = previous->defaultSectionSize();

    auto* header = new CheckHeaderView(this, m_table);
    m_header = header;
    m_table->setHorizontalHeader(header);
    header->setStretchLastSection(stretchLast);
    header->setCascadingSectionResizes(cascading);
    header->setMinimumSectionSize(minimum);
    header->setDefaultSectionSize(defaultSize);
    for (int column = 0; column < columns; ++column) {
        header->setSectionResizeMode(column, modes.at(column));
        header->resizeSection(column, widths.at(column));
    }

    m_table->viewport()->installEventFilter(this);
    // setItem emits itemChanged for every cell. Catalogue fills every live book
    // before the window is shown, so walking the whole table here never returns
    // to MainWindow::show. Ignore a change that leaves the tick as it was.
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item == nullptr || item->column() != 0) {
            return;
        }
        const bool now = item->checkState() == Qt::Checked;
        const int row = item->row();
        const bool was = m_checkedRows.contains(row);
        if (was == now) {
            return;
        }
        if (now) {
            m_checkedRows.insert(row);
        } else {
            m_checkedRows.remove(row);
        }
        noteCheckChange();
    });
    // removeRow does not emit itemChanged, so a removed tick would leave the
    // header box showing a state the rows no longer have.
    connect(m_table->model(), &QAbstractItemModel::rowsRemoved, this,
            [this](const QModelIndex&, int, int) { noteCheckChange(); });
    connect(m_table->model(), &QAbstractItemModel::modelReset, this,
            [this] { noteCheckChange(); });
}

QTableWidget* TableRowChecks::liveTable() const
{
    // This object is a QObject, so it is destroyed from ~QObject, after
    // ~QWidget has already deleted the table. Viewport events and modelReset
    // both land here while the dynamic type is QWidget. dynamic_cast is
    // specified to fail; rowCount() and viewport() are not, and UBSan aborts
    // the sanitizer job on that call.
    return dynamic_cast<QTableWidget*>(m_tableWidget);
}

QList<qint64> TableRowChecks::checkedIds() const
{
    QList<qint64> ids;
    QTableWidget* table = liveTable();
    if (table == nullptr) {
        return ids;
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* item = table->item(row, 0);
        if (item != nullptr && item->checkState() == Qt::Checked) {
            ids.append(item->data(Qt::UserRole).toLongLong());
        }
    }
    return ids;
}

QList<int> TableRowChecks::checkedRows() const
{
    QList<int> rows;
    QTableWidget* table = liveTable();
    if (table == nullptr) {
        return rows;
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* item = table->item(row, 0);
        if (item != nullptr && item->checkState() == Qt::Checked) {
            rows.append(row);
        }
    }
    return rows;
}

void TableRowChecks::clear()
{
    QTableWidget* table = liveTable();
    if (table == nullptr) {
        return;
    }
    m_adjusting = true;
    for (int row = 0; row < table->rowCount(); ++row) {
        setRowChecked(table->item(row, 0), false);
    }
    m_adjusting = false;
    noteCheckChange();
}

bool TableRowChecks::eventFilter(QObject* watched, QEvent* event)
{
    QTableWidget* table = liveTable();
    if (table == nullptr || watched != table->viewport()) {
        return QObject::eventFilter(watched, event);
    }
    const auto type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease
        && type != QEvent::MouseButtonDblClick) {
        return QObject::eventFilter(watched, event);
    }

    auto* mouse = static_cast<QMouseEvent*>(event);
    const QPoint pos = mouse->position().toPoint();
    const QModelIndex index = table->indexAt(pos);
    if (!index.isValid() || index.column() != 0) {
        return QObject::eventFilter(watched, event);
    }
    QTableWidgetItem* item = table->item(index.row(), 0);
    if (item == nullptr || !(item->flags() & Qt::ItemIsUserCheckable)) {
        return QObject::eventFilter(watched, event);
    }
    if (!cellIndicatorRect(m_table, index).contains(pos)) {
        return QObject::eventFilter(watched, event);
    }

    if (type == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
        setRowChecked(item, item->checkState() != Qt::Checked);
    }
    return true;
}

void TableRowChecks::toggleAll()
{
    QTableWidget* table = liveTable();
    if (table == nullptr) {
        return;
    }
    const bool check = !everyRowChecked();
    m_adjusting = true;
    for (int row = 0; row < table->rowCount(); ++row) {
        setRowChecked(table->item(row, 0), check);
    }
    m_adjusting = false;
    noteCheckChange();
}

void TableRowChecks::noteCheckChange()
{
    QTableWidget* table = liveTable();
    if (m_adjusting || table == nullptr || m_header == nullptr) {
        return;
    }
    const int checked = countChecked();
    const int rows = table->rowCount();
    Qt::CheckState state = Qt::Unchecked;
    if (rows > 0 && checked == rows) {
        state = Qt::Checked;
    } else if (checked > 0) {
        state = Qt::PartiallyChecked;
    }
    m_header->setTriState(state);
    rememberCheckedRows();
    publishCount();
}

void TableRowChecks::rememberCheckedRows()
{
    m_checkedRows.clear();
    QTableWidget* table = liveTable();
    if (table == nullptr) {
        return;
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* item = table->item(row, 0);
        if (item != nullptr && item->checkState() == Qt::Checked) {
            m_checkedRows.insert(row);
        }
    }
}

void TableRowChecks::publishCount()
{
    const int checked = countChecked();
    if (checked == m_publishedCount) {
        return;
    }
    m_publishedCount = checked;
    emit checkedCountChanged(checked);
}

int TableRowChecks::countChecked() const
{
    return checkedRows().size();
}

bool TableRowChecks::everyRowChecked() const
{
    QTableWidget* table = liveTable();
    return table != nullptr && table->rowCount() > 0 && countChecked() == table->rowCount();
}

void TableRowChecks::setRowChecked(QTableWidgetItem* item, const bool checked)
{
    if (item == nullptr) {
        return;
    }
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
}
