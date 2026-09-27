#pragma once

#include <QObject>
#include <QStringList>

class QTableWidget;

namespace VLMS {

class TableHeaderSort : public QObject {
    Q_OBJECT

public:
    explicit TableHeaderSort(QTableWidget* table, QObject* parent = nullptr);
    void setColumnKeys(const QStringList& keys);
    /// Back to "no column": the caller's default order applies again. For a
    /// table whose columns change meaning, like the Archive's type switch.
    void reset();
    [[nodiscard]] int column() const;
    [[nodiscard]] bool ascending() const;
    [[nodiscard]] bool isActive() const;   // false until first click
    [[nodiscard]] QString columnKey() const; // empty if inactive / no keys

signals:
    void sortChanged(int column, bool ascending);

private slots:
    void onSectionClicked(int column);

private:
    QTableWidget* m_table = nullptr;
    QStringList m_keys;
    int m_column = -1;
    bool m_ascending = true;
};

void enableWidgetTableSort(QTableWidget* table,
                           int idColumn = 0,
                           int idRole = Qt::UserRole);

}  // namespace VLMS
