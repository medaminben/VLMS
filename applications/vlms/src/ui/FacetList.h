#pragma once

#include <QList>
#include <QListWidget>
#include <QString>
#include <QStringList>

namespace VLMS {

/**
 * A filter list whose first row is "All". Each other row carries a code in
 * Qt::UserRole; All carries the empty string.
 *
 * Multi-select lists keep All and concrete rows exclusive: picking a row drops
 * All, picking All drops the rows, and emptying the list selects All again.
 * A single-select list only ever falls back to All.
 */
class FacetList final : public QListWidget {
    Q_OBJECT

public:
    struct Entry {
        QString label;
        QString code;
    };

    FacetList(const QString& objectName, bool multiSelect, QWidget* parent = nullptr);

    /// Replaces the rows, keeping whatever was picked that still exists.
    void setEntries(const QString& allLabel, const QList<Entry>& entries);
    /// The picked codes; empty while All is selected.
    [[nodiscard]] QStringList selectedCodes() const;
    /// Selects All, without emitting selectionEdited.
    void selectAll();
    /// Fixes the height to `maxVisibleRows` rows (all of them when the list is
    /// shorter) and shows a scrollbar only when some rows are left out.
    void fitRows(int maxVisibleRows);

signals:
    /// The librarian changed the selection; not emitted by setEntries.
    void selectionEdited();

private:
    void onSelectionChanged();

    bool m_updating = false;
};

}  // namespace VLMS
