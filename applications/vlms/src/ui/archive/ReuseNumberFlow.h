#pragma once

#include <VLMS/Core/CatalogTypes.h>

#include <QDialog>

#include <string>

class CatalogRepository;
class QLineEdit;
class QListWidget;

/// Archive -> Reuse local number, step 1: which book takes the number.
/// "New book" first, then live books whose language numbers copies in the
/// archived copy's source.
class ReuseBookChooser final : public QDialog {
    Q_OBJECT

public:
    ReuseBookChooser(CatalogRepository& catalog,
                     const BookCopyRecord& archivedCopy,
                     QWidget* parent = nullptr);

    /// 0 for "New book", -1 when nothing is chosen.
    [[nodiscard]] qint64 chosenBookId() const;

private:
    void refreshBooks();

    CatalogRepository& m_catalog;
    std::string m_source;
    QLineEdit* m_search = nullptr;
    QListWidget* m_books = nullptr;
};

namespace VLMS {

/// The whole Reuse flow: chooser, book editor with the number locked in, and a
/// save that releases the archived copy's number in the same transaction.
/// Returns true when a save committed; on cancel or failure the archived copy
/// still holds its number.
bool runReuseNumberFlow(QWidget* parent,
                        CatalogRepository& catalog,
                        const BookCopyRecord& archivedCopy);

}  // namespace VLMS
