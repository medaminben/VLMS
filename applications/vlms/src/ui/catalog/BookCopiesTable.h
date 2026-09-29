#pragma once

#include <VLMS/Repositories/CatalogTypes.h>

#include <QWidget>

namespace VLMS {
class CatalogLookups;
}

class QPushButton;
class QTableWidget;
class WidgetTableChecks;

/// Holdings table for the book editor. One row per physical copy.
class BookCopiesTable final : public QWidget {
    Q_OBJECT

public:
    explicit BookCopiesTable(VLMS::CatalogLookups& lookups, QWidget* parent = nullptr);

    void retranslateUi();
    void loadCopies(qint64 bookId);
    void addRow(const QString& language);
    /// A new copy row whose number comes from an archived copy (Archive ->
    /// Reuse). Number, global id, and source are locked for this session.
    void addReservedRow(const QString& source, const QString& localId, const QString& globalCopyId);
    [[nodiscard]] std::vector<VLMS::Repositories::BookCopyInput> copyInputs() const;
    [[nodiscard]] int copyCount() const;
    [[nodiscard]] bool validate(QWidget* dialogParent);

signals:
    void requestCopiesTab();
    void addRequested();

private:
    void appendCopyRow(const VLMS::Repositories::BookCopyRecord& copy);
    void sizeCopyColumns();
    void removeSelectedRow();

    VLMS::CatalogLookups& m_lookups;
    QTableWidget* m_table = nullptr;
    WidgetTableChecks* m_checks = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
};
