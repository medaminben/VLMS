#include "ui/presentation/PagedQueryModel.h"

#include "QtBridge.h"

namespace VLMS {

PagedQueryModel::PagedQueryModel(ListSpec* spec, DataEvents& events, QObject* parent)
    : QAbstractTableModel(parent),
      m_spec(spec),
      m_events(events)
{
    connect(&m_events, &DataEvents::changed, this, &PagedQueryModel::onDataChanged);
}

void PagedQueryModel::setSpec(ListSpec* spec)
{
    if (m_spec == spec) {
        return;
    }
    m_spec = spec;
    m_checked.clear();
    m_cellOverrides.clear();
    reload();
}

void PagedQueryModel::setQuery(const QueryState& state)
{
    m_state = state;
    reload();
}

void PagedQueryModel::reload()
{
    if (m_spec == nullptr) {
        return;
    }
    applyLoad(m_spec->load(m_state));
}

void PagedQueryModel::notifySpecRetranslated()
{
    if (m_spec == nullptr) {
        return;
    }
    const int rows = m_spec->rowCount();
    const int cols = columnCount();
    if (rows > 0 && cols > 0) {
        emit dataChanged(index(0, 0), index(rows - 1, cols - 1));
    }
    if (cols > 0) {
        emit headerDataChanged(Qt::Horizontal, 0, cols - 1);
    }
}

const QueryState& PagedQueryModel::query() const
{
    return m_state;
}

QList<qint64> PagedQueryModel::checkedIds() const
{
    QList<qint64> ids;
    ids.reserve(m_checked.size());
    for (qint64 id : m_checked) {
        ids.append(id);
    }
    return ids;
}

void PagedQueryModel::tickAll()
{
    if (m_spec == nullptr) {
        return;
    }
    const int rows = m_spec->rowCount();
    for (int row = 0; row < rows; ++row) {
        m_checked.insert(m_spec->idAt(row));
    }
    if (rows > 0) {
        emit dataChanged(index(0, 0), index(rows - 1, 0), {Qt::CheckStateRole});
    }
}

void PagedQueryModel::clearTicks()
{
    if (m_checked.isEmpty()) {
        return;
    }
    m_checked.clear();
    if (m_spec != nullptr && m_spec->rowCount() > 0) {
        emit dataChanged(index(0, 0), index(m_spec->rowCount() - 1, 0), {Qt::CheckStateRole});
    }
}

void PagedQueryModel::toggleTicks()
{
    if (m_spec == nullptr) {
        return;
    }
    const int rows = m_spec->rowCount();
    for (int row = 0; row < rows; ++row) {
        if (!m_checked.contains(m_spec->idAt(row))) {
            tickAll();
            return;
        }
    }
    clearTicks();
}

bool PagedQueryModel::revealId(qint64 id)
{
    if (m_spec == nullptr) {
        return false;
    }
    const Core::Result<int> rank = m_spec->rankOf(id, m_state);
    if (!rank) {
        return false;
    }
    const int rankValue = rank.value();
    if (m_state.pageSize <= 0) {
        m_state.page = 1;
    } else {
        m_state.page = (rankValue / m_state.pageSize) + 1;
    }
    reload();
    const int rowInPage =
        m_state.pageSize <= 0 ? rankValue : (rankValue % m_state.pageSize);
    emit selectionTargetChanged(rowInPage);
    return true;
}

int PagedQueryModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || m_spec == nullptr) {
        return 0;
    }
    return m_spec->rowCount();
}

int PagedQueryModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid() || m_spec == nullptr) {
        return 0;
    }
    return m_spec->columns().size();
}

QString PagedQueryModel::overrideKey(const qint64 id, const int column, const int role)
{
    return QStringLiteral("%1/%2/%3").arg(id).arg(column).arg(role);
}

QVariant PagedQueryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || m_spec == nullptr) {
        return {};
    }
    if (role == Qt::CheckStateRole && index.column() == 0) {
        const qint64 id = m_spec->idAt(index.row());
        return m_checked.contains(id) ? Qt::Checked : Qt::Unchecked;
    }
    const qint64 id = m_spec->idAt(index.row());
    if (id > 0) {
        const auto it = m_cellOverrides.constFind(overrideKey(id, index.column(), role));
        if (it != m_cellOverrides.cend()) {
            return *it;
        }
    }
    return m_spec->cell(index.row(), index.column(), role);
}

bool PagedQueryModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || m_spec == nullptr) {
        return false;
    }
    const qint64 id = m_spec->idAt(index.row());
    if (id <= 0) {
        return false;
    }
    if (role == Qt::CheckStateRole && index.column() == 0) {
        const bool checked = value.toInt() == static_cast<int>(Qt::Checked);
        if (checked) {
            m_checked.insert(id);
        } else {
            m_checked.remove(id);
        }
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }
    // Ephemeral cell overrides (LocalNumberDelegate pick writes kSelectedRole).
    if (role == Qt::CheckStateRole) {
        return false;
    }
    m_cellOverrides.insert(overrideKey(id, index.column(), role), value);
    emit dataChanged(index, index, {role});
    return true;
}

QVariant PagedQueryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || m_spec == nullptr) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    const QList<ColumnSpec> cols = m_spec->columns();
    if (section < 0 || section >= cols.size()) {
        return {};
    }
    return T(ss(cols.at(section).titleKey));
}

Qt::ItemFlags PagedQueryModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == 0) {
        f |= Qt::ItemIsUserCheckable;
    }
    return f;
}

void PagedQueryModel::sort(int column, Qt::SortOrder order)
{
    if (m_spec == nullptr) {
        return;
    }
    const QList<ColumnSpec> cols = m_spec->columns();
    if (column < 0 || column >= cols.size()) {
        return;
    }
    const QString key = cols.at(column).sortKey;
    if (key.isEmpty()) {
        return;
    }
    m_state.sortKey = key;
    m_state.ascending = (order == Qt::AscendingOrder);
    reload();
}

void PagedQueryModel::onDataChanged(EntitySet what, const QList<qint64>& /*ids*/)
{
    if (m_spec == nullptr) {
        return;
    }
    if ((what & m_spec->dependsOn()) != EntitySet()) {
        reload();
    }
}

void PagedQueryModel::applyLoad(Core::Status status)
{
    if (!status) {
        emit failed(status.error());
        return;
    }
    beginResetModel();
    m_cellOverrides.clear();
    pruneCheckedToListed();
    endResetModel();
    emit loaded(m_spec != nullptr ? m_spec->totalCount() : 0);
}

void PagedQueryModel::pruneCheckedToListed()
{
    if (m_spec == nullptr || m_checked.isEmpty()) {
        return;
    }
    QSet<qint64> listed;
    const int rows = m_spec->rowCount();
    for (int row = 0; row < rows; ++row) {
        listed.insert(m_spec->idAt(row));
    }
    m_checked.intersect(listed);
}

}  // namespace VLMS
