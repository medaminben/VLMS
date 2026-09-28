#pragma once

#include <VLMS/Repositories/CatalogTypes.h>

#include <QDialog>

#include <string>

namespace VLMS::Repositories {
class CatalogRepository;
}  // namespace VLMS::Repositories
class QLineEdit;
class QListWidget;

/// Archive -> Reuse local number, step 1: which book takes the number.
/// "New book" first, then live books whose language numbers copies in the
/// archived copy's source.
class ReuseBookChooser final : public QDialog {
    Q_OBJECT

public:
    ReuseBookChooser(VLMS::Repositories::CatalogRepository& catalog,
                     const VLMS::Repositories::BookCopyRecord& archivedCopy,
                     QWidget* parent = nullptr);

    /// 0 for "New book", -1 when nothing is chosen.
    [[nodiscard]] qint64 chosenBookId() const;

private:
    void refreshBooks();

    VLMS::Repositories::CatalogRepository& m_catalog;
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
                        Repositories::CatalogRepository& catalog,
                        const Repositories::BookCopyRecord& archivedCopy);

}  // namespace VLMS
