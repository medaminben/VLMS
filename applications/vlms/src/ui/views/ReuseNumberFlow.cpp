#include "ui/views/ReuseNumberFlow.h"

#include "ui/presentation/CatalogLookups.h"

#include "QtBridge.h"
#include "ui/views/UiHelpers.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>


using VLMS::T;
using VLMS::qs;
using VLMS::ss;

ReuseBookChooser::ReuseBookChooser(VLMS::CatalogLookups& lookups,
                                   const VLMS::Repositories::BookCopyRecord& archivedCopy,
                                   QWidget* parent)
    : QDialog(parent),
      m_lookups(lookups),
      m_source(qs(archivedCopy.source))
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

    for (const VLMS::Repositories::BookRecord& book :
         m_lookups.booksForCopySource(m_search->text(), m_source)) {
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
