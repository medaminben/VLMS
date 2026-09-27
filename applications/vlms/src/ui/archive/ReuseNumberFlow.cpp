#include "ui/archive/ReuseNumberFlow.h"

#include <VLMS/Core/CatalogRepository.h>

#include "QtBridge.h"
#include "ui/UiHelpers.h"
#include "ui/catalog/BookEditorDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

using VLMS::T;
using VLMS::qs;
using VLMS::ss;

ReuseBookChooser::ReuseBookChooser(CatalogRepository& catalog,
                                   const BookCopyRecord& archivedCopy,
                                   QWidget* parent)
    : QDialog(parent),
      m_catalog(catalog),
      m_source(archivedCopy.source)
{
    setWindowTitle(T("archive.reuse.title"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(T("archive.reuse.prompt", "number", archivedCopy.localId), this));

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(T("archive.reuse.search"));
    layout->addWidget(m_search);

    m_books = new QListWidget(this);
    m_books->setObjectName(QStringLiteral("reuseBooks"));
    layout->addWidget(m_books, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    VLMS::localizeButtonBox(buttons);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_search, &QLineEdit::textChanged, this, &ReuseBookChooser::refreshBooks);
    connect(m_books, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    refreshBooks();
}

void ReuseBookChooser::refreshBooks()
{
    m_books->clear();
    auto* fresh = new QListWidgetItem(T("archive.reuse.newBook"), m_books);
    fresh->setData(Qt::UserRole, QVariant::fromValue<qint64>(0));

    BookQuery query;
    query.search = ss(m_search->text());
    query.limit = 200;
    const auto books = m_catalog.listBooks(query);
    if (!books) {
        VLMS::showRepoError(this, books.error());
        return;
    }
    for (const BookRecord& book : books.value()) {
        if (CatalogRepository::copySourceForLanguage(book.language) != m_source) {
            continue;
        }
        const QString author =
            book.authorName.empty() ? QString() : QStringLiteral(" — ") + qs(book.authorName);
        auto* item = new QListWidgetItem(qs(book.title) + author, m_books);
        item->setData(Qt::UserRole, QVariant::fromValue<qint64>(book.id));
    }
    m_books->setCurrentRow(0);
}

qint64 ReuseBookChooser::chosenBookId() const
{
    const QListWidgetItem* item = m_books->currentItem();
    return item == nullptr ? -1 : item->data(Qt::UserRole).toLongLong();
}

namespace VLMS {

bool runReuseNumberFlow(QWidget* parent,
                        CatalogRepository& catalog,
                        const BookCopyRecord& archivedCopy)
{
    ReuseBookChooser chooser(catalog, archivedCopy, parent);
    if (chooser.exec() != QDialog::Accepted || chooser.chosenBookId() < 0) {
        return false;
    }
    const qint64 bookId = chooser.chosenBookId();

    std::unique_ptr<BookEditorDialog> editor;
    if (bookId > 0) {
        const auto book = catalog.getBook(bookId);
        if (!book) {
            showRepoError(parent, book.error());
            return false;
        }
        editor = std::make_unique<BookEditorDialog>(catalog, book.value(), parent);
    } else {
        editor = std::make_unique<BookEditorDialog>(catalog, parent);
    }
    editor->reserveCopyNumber(archivedCopy);
    if (editor->exec() != QDialog::Accepted) {
        return false;
    }

    BookWrite write;
    write.book = editor->bookInput();
    write.copies = editor->copyInputs();
    if (editor->coverChanged()) {
        write.coverSourcePath = ss(editor->coverSourcePath());
    }
    const bool stillHoldsNumber = std::any_of(
        write.copies.begin(),
        write.copies.end(),
        [&archivedCopy](const BookCopyInput& copy) {
            return copy.localId == archivedCopy.localId
                && copy.globalCopyId == archivedCopy.globalCopyId;
        });
    if (stillHoldsNumber) {
        write.releaseFromCopyId = archivedCopy.id;
    }

    // The release lives inside the save's own transaction: a refused save or
    // an SQL error leaves the archived copy holding its number.
    if (bookId > 0) {
        if (const auto saved = catalog.saveExistingBook(bookId, write); !saved) {
            showRepoError(parent, saved.error());
            return false;
        }
    } else if (const auto created = catalog.saveNewBook(write); !created) {
        showRepoError(parent, created.error());
        return false;
    }
    return true;
}

}  // namespace VLMS
