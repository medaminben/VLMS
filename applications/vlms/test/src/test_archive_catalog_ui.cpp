#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/archive/ReuseNumberFlow.h"
#include "ui/catalog/BookCopiesTable.h"
#include "ui/catalog/BookEditorDialog.h"
#include "ui/catalog/CatalogPage.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>

#include <QAbstractButton>
#include <QApplication>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

#include <gtest/gtest.h>

#include <memory>

using VLMS::Locale;
using namespace VLMS;
using namespace Test;

namespace {

QPushButton* buttonWithText(QWidget* root, const QString& text)
{
    for (QPushButton* button : root->findChildren<QPushButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

void answerNextBoxYes()
{
    auto* poll = new QTimer;
    poll->setInterval(10);
    QObject::connect(poll, &QTimer::timeout, [poll] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (box == nullptr || !box->isVisible()) {
            return;
        }
        poll->stop();
        poll->deleteLater();
        if (QAbstractButton* yes = box->button(QMessageBox::Yes)) {
            yes->click();
        } else {
            box->done(0);
        }
    });
    poll->start();
}

/// Rejects the next modal dialog of any kind.
void rejectNextDialog()
{
    auto* poll = new QTimer;
    poll->setInterval(10);
    QObject::connect(poll, &QTimer::timeout, [poll] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr || !dialog->isVisible()) {
            return;
        }
        poll->stop();
        poll->deleteLater();
        dialog->reject();
    });
    poll->start();
}

}  // namespace

class test_ui_ArchiveCatalog : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }
    static void TearDownTestSuite() { Locale::setCode(Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_catalog.reset();
        m_db.reset();
    }

    BookCopyRecord archivedCopy(std::int64_t copyId)
    {
        EXPECT_TRUE(m_db->exec("UPDATE book_copies SET archived_at = '2026-09-19 10:00:00' "
                               "WHERE id = " + std::to_string(copyId)));
        CopyQuery query;
        query.archive = ArchiveScope::Archived;
        // Bind the vector so the Result temporary outlives the range-for.
        const std::vector<BookCopyRecord> rows = m_catalog->listCopyRows(query).value();
        for (const BookCopyRecord& copy : rows) {
            if (copy.id == copyId) {
                return copy;
            }
        }
        return {};
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<CirculationRepository> m_circulation;
};

TEST_F(test_ui_ArchiveCatalog, CatalogDeleteArchivesTheSelectedBook)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(1));
    CatalogPage page(*m_catalog, *m_circulation);
    QPushButton* remove = buttonWithText(&page, QStringLiteral("Delete"));
    ASSERT_NE(remove, nullptr);

    answerNextBoxYes();
    remove->click();

    EXPECT_FALSE(m_catalog->getBook(bookId)->archivedAt.empty());
    EXPECT_EQ(m_catalog->countBooks({}).value(), 0);
    EXPECT_EQ(m_db->count("books"), 1);
}

TEST_F(test_ui_ArchiveCatalog, AReservedRowCarriesTheNumberAndCannotBeEdited)
{
    BookCopyRecord copy;
    copy.id = 42;
    copy.source = "arabic";
    copy.localId = "7";
    copy.globalCopyId = "AR-7";

    BookEditorDialog dialog(*m_catalog);
    dialog.reserveCopyNumber(copy);

    const auto inputs = dialog.copyInputs();
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_EQ(inputs.front().id, 0);
    EXPECT_EQ(inputs.front().source, "arabic");
    EXPECT_EQ(inputs.front().localId, "7");
    EXPECT_EQ(inputs.front().globalCopyId, "AR-7");

    auto* copies = dialog.findChild<BookCopiesTable*>();
    ASSERT_NE(copies, nullptr);
    auto* table = copies->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    int locked = 0;
    for (int column = 0; column < table->columnCount(); ++column) {
        const QTableWidgetItem* item = table->item(0, column);
        if (item != nullptr && (item->text() == "7" || item->text() == "AR-7")) {
            EXPECT_FALSE(item->flags() & Qt::ItemIsEditable) << column;
            ++locked;
        }
    }
    EXPECT_EQ(locked, 2);
}

TEST_F(test_ui_ArchiveCatalog, AReservedRowCannotBeRemoved)
{
    BookCopyRecord copy;
    copy.source = "arabic";
    copy.localId = "7";
    copy.globalCopyId = "AR-7";

    BookEditorDialog dialog(*m_catalog);
    dialog.reserveCopyNumber(copy);

    auto* copies = dialog.findChild<BookCopiesTable*>();
    ASSERT_NE(copies, nullptr);
    QPushButton* remove = buttonWithText(copies, QStringLiteral("Remove copy"));
    ASSERT_NE(remove, nullptr);

    // addReservedRow selects the reserved row. Remove must leave it — otherwise
    // Accept still releases the archived number with no live copy holding it.
    remove->click();

    const auto inputs = dialog.copyInputs();
    ASSERT_EQ(inputs.size(), 1u);
    EXPECT_EQ(inputs.front().localId, "7");
    EXPECT_EQ(inputs.front().globalCopyId, "AR-7");
}

TEST_F(test_ui_ArchiveCatalog, ChooserOffersANewBookAndOnlyBooksOfTheSameSource)
{
    const std::int64_t arabicBook = seedBook(*m_db, uniqueBookSeed(2));
    BookSeed french = uniqueBookSeed(3);
    french.language = "fr";
    seedBook(*m_db, french);

    BookCopyRecord copy;
    copy.source = "arabic";
    copy.localId = "7";
    ReuseBookChooser chooser(*m_catalog, copy);
    auto* books = chooser.findChild<QListWidget*>(QStringLiteral("reuseBooks"));
    ASSERT_NE(books, nullptr);
    ASSERT_EQ(books->count(), 2);
    EXPECT_EQ(books->item(0)->data(Qt::UserRole).toLongLong(), 0);
    EXPECT_EQ(books->item(1)->data(Qt::UserRole).toLongLong(), arabicBook);
    EXPECT_EQ(chooser.chosenBookId(), 0);
}

TEST_F(test_ui_ArchiveCatalog, CancellingReuseLeavesTheArchivedNumberInPlace)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(4));
    const BookCopyRecord copy = archivedCopy(copyIdsOf(*m_db, bookId).front());
    ASSERT_FALSE(copy.localId.empty());

    rejectNextDialog();
    EXPECT_FALSE(VLMS::runReuseNumberFlow(nullptr, *m_catalog, copy));
    EXPECT_EQ(m_db->scalar("SELECT local_id FROM book_copies WHERE id = " + std::to_string(copy.id))
                  .toString(),
              copy.localId);
}
