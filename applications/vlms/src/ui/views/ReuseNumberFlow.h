#pragma once

#include <VLMS/Repositories/CatalogTypes.h>

#include <QDialog>

namespace VLMS {
class CatalogLookups;
}

class QLineEdit;
class QListWidget;

/// Archive -> Reuse local number, step 1: which book takes the number.
/// "New book" first, then live books whose language numbers copies in the
/// archived copy's source. ArchiveWorkflow runs the rest of the flow.
class ReuseBookChooser final : public QDialog {
    Q_OBJECT

public:
    ReuseBookChooser(VLMS::CatalogLookups& lookups,
                     const VLMS::Repositories::BookCopyRecord& archivedCopy,
                     QWidget* parent = nullptr);

    /// 0 for "New book", -1 when nothing is chosen.
    [[nodiscard]] qint64 chosenBookId() const;

private:
    void refreshBooks();

    VLMS::CatalogLookups& m_lookups;
    QString m_source;
    QLineEdit* m_search = nullptr;
    QListWidget* m_books = nullptr;
};
