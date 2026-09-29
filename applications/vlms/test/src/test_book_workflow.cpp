#include "WorkflowTestSupport.h"

#include <QDate>

using namespace Test;

class BookWorkflow : public WorkflowFixture {
protected:
    VLMS::BookForm bookForm(int index, const std::string& title) const
    {
        VLMS::BookForm form;
        form.book = uniqueBookSeed(index).toInput();
        form.book.title = title;
        return form;
    }

    std::string bookTitle(qint64 bookId) const
    {
        return scalarText("SELECT title FROM books WHERE id = " + std::to_string(bookId));
    }

    bool archived(qint64 bookId) const
    {
        return !scalarIsNull("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId));
    }
};

TEST_F(BookWorkflow, AddCancelEmitsNothingAndShowsNoError)
{
    const auto status = books->add(&parent);

    EXPECT_TRUE(status);
    EXPECT_EQ(changed->count(), 0);
    EXPECT_TRUE(errors.calls.empty());
    ASSERT_EQ(dialogs.bookRequests.size(), 1U);
    EXPECT_FALSE(dialogs.bookRequests.front().book.has_value());
}

TEST_F(BookWorkflow, AddSuccessSavesTheBookAndEmitsBooksAndCopies)
{
    dialogs.nextBook = bookForm(10, "Checklist Add Book");

    const auto status = books->add(&parent);

    EXPECT_TRUE(status);
    EXPECT_TRUE(errors.calls.empty());
    ASSERT_EQ(db->count("books"), 1);
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Books | VLMS::Entity::Copies);
    EXPECT_EQ(bookTitle(changedIds(0).front()), "Checklist Add Book");
}

TEST_F(BookWorkflow, AddWithoutATitleShowsTheErrorOnceAndSavesNothing)
{
    dialogs.nextBook = bookForm(11, "");

    const auto status = books->add(&parent);

    EXPECT_FALSE(status);
    EXPECT_EQ(errorKeys(), std::vector<std::string>{"error.book.titleRequired"});
    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(db->count("books"), 0);
}

TEST_F(BookWorkflow, EditOpensTheFullRecord)
{
    const qint64 bookId = seedBook(*db, uniqueBookSeed(12));

    ASSERT_TRUE(books->edit(&parent, bookId));

    ASSERT_EQ(dialogs.bookRequests.size(), 1U);
    ASSERT_TRUE(dialogs.bookRequests.front().book.has_value());
    EXPECT_EQ(dialogs.bookRequests.front().book->id, bookId);
    EXPECT_TRUE(static_cast<bool>(dialogs.bookRequests.front().manageCategories));
}

TEST_F(BookWorkflow, EditCancelEmitsNothing)
{
    const qint64 bookId = seedBook(*db, uniqueBookSeed(13));
    const std::string before = bookTitle(bookId);

    const auto status = books->edit(&parent, bookId);

    EXPECT_TRUE(status);
    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(bookTitle(bookId), before);
}

TEST_F(BookWorkflow, EditSuccessSavesAndEmitsTheBook)
{
    const qint64 bookId = seedBook(*db, uniqueBookSeed(14));
    VLMS::BookForm form = bookForm(14, "Renamed Title");
    const auto copies = catalog->listCopies(bookId);
    ASSERT_TRUE(copies);
    for (const auto& copy : copies.value()) {
        VLMS::Repositories::BookCopyInput input;
        input.id = copy.id;
        input.globalCopyId = copy.globalCopyId;
        input.source = copy.source;
        input.localId = copy.localId;
        input.centralId = copy.centralId;
        input.classification = copy.classification;
        input.subject = copy.subject;
        input.notes = copy.notes;
        input.inventoryStatus = copy.inventoryStatus;
        input.compensation = copy.compensation;
        input.location = copy.location;
        input.indexCode = copy.indexCode;
        form.copies.push_back(input);
    }
    dialogs.nextBook = form;

    const auto status = books->edit(&parent, bookId);

    EXPECT_TRUE(status) << ::testing::PrintToString(errorKeys());
    EXPECT_EQ(bookTitle(bookId), "Renamed Title");
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedIds(0), QList<qint64>({bookId}));
}

TEST_F(BookWorkflow, EditWithoutASelectionInforms)
{
    ASSERT_TRUE(books->edit(&parent, 0));

    EXPECT_EQ(dialogs.count(QStringLiteral("inform")), 1);
    EXPECT_EQ(dialogs.count(QStringLiteral("editBook")), 0);
}

TEST_F(BookWorkflow, ArchiveSingleConfirmEmitsBooksChanged)
{
    const qint64 bookId = seedBook(*db, uniqueBookSeed(1));
    dialogs.askYesNoResult = true;

    ASSERT_TRUE(books->archive(&parent, {bookId}, {QStringLiteral("title")}));

    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Books);
    EXPECT_EQ(changedIds(0), QList<qint64>({bookId}));
    EXPECT_TRUE(archived(bookId));
}

TEST_F(BookWorkflow, ArchiveSingleDeclinedEmitsNothing)
{
    const qint64 bookId = seedBook(*db, uniqueBookSeed(2));
    dialogs.askYesNoResult = false;

    ASSERT_TRUE(books->archive(&parent, {bookId}, {QStringLiteral("title")}));

    EXPECT_EQ(changed->count(), 0);
    EXPECT_FALSE(archived(bookId));
}

TEST_F(BookWorkflow, ArchiveSingleWithAnOpenLoanInformsAndKeepsTheBook)
{
    const qint64 bookId = seedBook(*db, uniqueBookSeed(3));
    const qint64 memberId = seedMember(*db, uniqueMemberSeed(3));
    const QDate today = QDate::currentDate();
    ASSERT_GT(rawInsertLoan(*db, memberId, copyIdsOf(*db, bookId).front(),
                            today.addDays(-1).toString(Qt::ISODate).toStdString(),
                            today.addDays(13).toString(Qt::ISODate).toStdString()),
              0);

    ASSERT_TRUE(books->archive(&parent, {bookId}, {QStringLiteral("title")}));

    EXPECT_EQ(dialogs.count(QStringLiteral("inform")), 1);
    EXPECT_EQ(dialogs.count(QStringLiteral("askYesNo")), 0);
    EXPECT_FALSE(archived(bookId));
}

TEST_F(BookWorkflow, ArchiveManyConfirmedThroughTheFactoryArchivesAll)
{
    const qint64 a = seedBook(*db, uniqueBookSeed(4));
    const qint64 b = seedBook(*db, uniqueBookSeed(5));
    dialogs.confirmBulkResult = true;

    ASSERT_TRUE(books->archive(&parent, {a, b}, {QStringLiteral("a"), QStringLiteral("b")}));

    EXPECT_EQ(dialogs.count(QStringLiteral("confirmBulk")), 1);
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedIds(0), QList<qint64>({a, b}));
    EXPECT_TRUE(archived(a));
    EXPECT_TRUE(archived(b));
}

TEST_F(BookWorkflow, ShowLoansOpensTheHistoryPinnedToTheBook)
{
    const qint64 bookId = seedBook(*db, uniqueBookSeed(6));

    ASSERT_TRUE(books->showLoans(&parent, bookId));

    ASSERT_EQ(dialogs.historyRequests.size(), 1U);
    EXPECT_EQ(dialogs.historyRequests.front().kind, VLMS::LoanHistoryKind::Book);
    EXPECT_EQ(dialogs.historyRequests.front().id, bookId);
    EXPECT_EQ(dialogs.historyRequests.front().loans, loans.get());
}

TEST_F(BookWorkflow, CategoriesClosedWithoutEditsEmitsNothing)
{
    ASSERT_TRUE(books->manageCategories(&parent));

    EXPECT_EQ(dialogs.count(QStringLiteral("manageCategories")), 1);
    EXPECT_EQ(changed->count(), 0);
}

TEST_F(BookWorkflow, CategoriesCreatedThroughTheEditorEmitCategoriesOnce)
{
    dialogs.categoryScript = [this](VLMS::CategoryEditor& editor) {
        EXPECT_TRUE(editor.create(&parent, QStringLiteral("HIS"), QStringLiteral("History")));
        EXPECT_TRUE(editor.create(&parent, QStringLiteral("GEO"), QStringLiteral("Geography")));
        EXPECT_EQ(editor.categories().size(), 2U);
    };

    ASSERT_TRUE(books->manageCategories(&parent));

    EXPECT_EQ(db->count("categories"), 2);
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Categories);
}

TEST_F(BookWorkflow, CategoryRenameAndDeleteGoThroughTheWorkflow)
{
    const qint64 keep = seedCategory(*db, "HIS", "History");
    const qint64 drop = seedCategory(*db, "OLD", "Old");
    dialogs.categoryScript = [&](VLMS::CategoryEditor& editor) {
        EXPECT_TRUE(editor.update(&parent, keep, QStringLiteral("HIS"), QStringLiteral("Histoire")));
        EXPECT_TRUE(editor.remove(&parent, drop));
    };

    ASSERT_TRUE(books->manageCategories(&parent));

    EXPECT_EQ(scalarText("SELECT label FROM categories WHERE id = " + std::to_string(keep)), "Histoire");
    EXPECT_EQ(db->count("categories"), 1);
    EXPECT_EQ(changed->count(), 1);
}

TEST_F(BookWorkflow, CategoryWriteFailureShowsTheErrorOnceAndEmitsNothing)
{
    dialogs.categoryScript = [this](VLMS::CategoryEditor& editor) {
        EXPECT_FALSE(editor.create(&parent, QStringLiteral(""), QStringLiteral("No code")));
    };

    ASSERT_TRUE(books->manageCategories(&parent));

    EXPECT_EQ(errorKeys(), std::vector<std::string>{"error.category.codeRequired"});
    EXPECT_EQ(changed->count(), 0);
}
