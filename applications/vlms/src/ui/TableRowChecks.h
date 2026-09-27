#pragma once

#include <QList>
#include <QObject>
#include <QSet>

class CheckHeaderView;
class QTableWidget;
class QTableWidgetItem;
class QWidget;

/// Check boxes inside column 0 of a list table, plus a tri-state box at the
/// leading edge of the header. The id stays on Qt::UserRole of that same cell,
/// so no column index moves. Construct this before TableHeaderSort: the sort
/// object connects to whatever header is installed at that moment.
class TableRowChecks : public QObject {
    Q_OBJECT

    friend class CheckHeaderView;

public:
    explicit TableRowChecks(QTableWidget* table, QObject* parent = nullptr);

    /// Qt::UserRole on column 0, for every ticked row, top to bottom.
    [[nodiscard]] QList<qint64> checkedIds() const;
    [[nodiscard]] QList<int> checkedRows() const;

    /// Every column-0 item becomes checkable and unticked. Pages call this
    /// from the refresh that rebuilds the rows.
    void clear();

signals:
    void checkedCountChanged(int count);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void toggleAll();
    void noteCheckChange();
    void publishCount();
    [[nodiscard]] int countChecked() const;
    [[nodiscard]] bool everyRowChecked() const;
    void setRowChecked(QTableWidgetItem* item, bool checked);
    void rememberCheckedRows();
    /// Null once ~QTableWidget has finished and ~QWidget is still running.
    [[nodiscard]] QTableWidget* liveTable() const;

    QTableWidget* m_table = nullptr;
    /// Saved while the table is fully constructed. dynamic_cast through this
    /// fails during ~QWidget; a call through m_table does not.
    QWidget* m_tableWidget = nullptr;
    CheckHeaderView* m_header = nullptr;
    /// Rows whose column-0 check state was last seen as ticked. Used so a
    /// cell edit that does not change the tick can return without walking
    /// the table. Rebuilt whenever the ticks are recounted.
    QSet<int> m_checkedRows;
    bool m_adjusting = false;
    int m_publishedCount = 0;
};
