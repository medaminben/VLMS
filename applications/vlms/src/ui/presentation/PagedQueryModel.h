#pragma once

#include "ui/presentation/ListSpec.h"
#include "ui/presentation/QueryState.h"
#include "ui/workflows/DataEvents.h"

#include <VLMS/Core/Result.h>

#include <QAbstractTableModel>
#include <QList>
#include <QSet>

namespace VLMS {

class PagedQueryModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    PagedQueryModel(ListSpec* spec, DataEvents& events, QObject* parent = nullptr);

    void setSpec(ListSpec* spec);  // Archive type switch
    void setQuery(const QueryState& state);
    void reload();
    [[nodiscard]] const QueryState& query() const;

    [[nodiscard]] QList<qint64> checkedIds() const;
    void tickAll();
    void clearTicks();
    /// Uses spec->rankOf to change page and select; returns whether found.
    bool revealId(qint64 id);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    void sort(int column, Qt::SortOrder order) override;

signals:
    void loaded(int total);
    void failed(Core::Error error);
    void selectionTargetChanged(int row);  // revealId helper for the view

private:
    void onDataChanged(EntitySet what, const QList<qint64>& ids);
    void applyLoad(Core::Status status);
    void pruneCheckedToListed();

    ListSpec* m_spec = nullptr;
    DataEvents& m_events;
    QueryState m_state;
    QSet<qint64> m_checked;
};

}  // namespace VLMS

Q_DECLARE_METATYPE(VLMS::Core::Error)
