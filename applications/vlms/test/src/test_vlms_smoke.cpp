// End-to-end passes through the real QtDialogFactory: each test answers the
// real windows the librarian would see, then checks the database and the
// change events. Kept small; the workflow suite covers the edge cases.

#include "Librarian.h"
#include "WorkflowTestSupport.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>

#include <deque>

using namespace Test;

namespace {

std::string isoDaysFromToday(int days)
{
    return QDate::currentDate().addDays(days).toString(Qt::ISODate).toStdString();
}

}  // namespace

class Smoke : public ::testing::Test {
protected:
    static void SetUpTestSuite() { VLMS::Core::Locale::setCode("en"); }
    static void TearDownTestSuite()
    {
        VLMS::Core::Locale::setCode(VLMS::Core::Locale::kDefaultCode);
    }

    void SetUp() override
    {
        db = std::make_unique<::Test::TestDatabase>();
        ASSERT_TRUE(db->isValid()) << db->lastError();
        memberRepo = std::make_unique<VLMS::Repositories::MemberRepository>(
            db->session(), db->resourcesDirectory());
        catalog = std::make_unique<VLMS::Repositories::CatalogRepository>(
            db->session(), db->resourcesDirectory());
        circulation = std::make_unique<VLMS::Repositories::CirculationRepository>(db->session());
        app = std::make_unique<UiWorkflows>(*circulation, catalog.get(), memberRepo.get());
        changed = std::make_unique<QSignalSpy>(&app->events, &VLMS::DataEvents::changed);
    }

    void TearDown() override
    {
        EXPECT_TRUE(app->errors.calls.empty()) << app->errors.calls.front().key;
        changed.reset();
        app.reset();
        circulation.reset();
        catalog.reset();
        memberRepo.reset();
        db.reset();
    }

    void run(std::deque<Librarian::Step> steps, const std::function<void()>& action)
    {
        Librarian librarian(std::move(steps));
        action();
        EXPECT_TRUE(librarian.done()) << "windows seen: " << librarian.seen.join(", ").toStdString();
        EXPECT_TRUE(librarian.unexpected.isEmpty())
            << "unexpected windows: " << librarian.unexpected.join(", ").toStdString();
    }

    std::string scalarText(const std::string& sql) const { return db->scalar(sql).toString(); }

    std::unique_ptr<::Test::TestDatabase> db;
    std::unique_ptr<VLMS::Repositories::MemberRepository> memberRepo;
    std::unique_ptr<VLMS::Repositories::CatalogRepository> catalog;
    std::unique_ptr<VLMS::Repositories::CirculationRepository> circulation;
    std::unique_ptr<UiWorkflows> app;
    std::unique_ptr<QSignalSpy> changed;
    QWidget parent;
};

TEST_F(Smoke, AddABookInTheRealEditor)
{
    run({titleThenOk(QStringLiteral("Smoke Added Book"))},
        [this] { EXPECT_TRUE(app->books->add(&parent)); });

    EXPECT_EQ(scalarText("SELECT title FROM books"), "Smoke Added Book");
    EXPECT_EQ(changed->count(), 1);
}

TEST_F(Smoke, EditABookInTheRealEditor)
{
    const qint64 bookId = seedBook(*db, uniqueBookSeed(1));

    run({titleThenOk(QStringLiteral("Smoke Edited Book"))},
        [&] { EXPECT_TRUE(app->books->edit(&parent, bookId)); });

    EXPECT_EQ(scalarText("SELECT title FROM books WHERE id = " + std::to_string(bookId)),
              "Smoke Edited Book");
    EXPECT_EQ(changed->count(), 1);
}

TEST_F(Smoke, ArchiveABookFromARealQuestion)
{
    const qint64 bookId = seedBook(*db, uniqueBookSeed(2));

    run({pressYes}, [&] { EXPECT_TRUE(app->books->archive(&parent, {bookId}, {QStringLiteral("b")})); });

    EXPECT_FALSE(db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId)).isNull());
}

TEST_F(Smoke, OpenAndCloseTheCategoryManager)
{
    run({closeWindow}, [this] { EXPECT_TRUE(app->books->manageCategories(&parent)); });

    EXPECT_EQ(changed->count(), 0);
}

TEST_F(Smoke, SaveAMemberUnchangedInTheRealEditor)
{
    const qint64 memberId = seedMember(*db, uniqueMemberSeed(3));

    run({pressOk}, [&] { EXPECT_TRUE(app->members->edit(&parent, memberId)); });

    EXPECT_EQ(changed->count(), 1);
}

TEST_F(Smoke, ArchiveRestoreAndPurgeAMember)
{
    const qint64 memberId = seedMember(*db, uniqueMemberSeed(4));
    const QList<QString> label{QStringLiteral("Amina")};

    run({pressYes}, [&] { EXPECT_TRUE(app->members->archive(&parent, {memberId}, label)); });
    run({pressYes}, [&] {
        EXPECT_TRUE(app->archive->restore(&parent, VLMS::ArchiveType::Members, {memberId}, label));
    });
    ASSERT_TRUE(db->scalar("SELECT archived_at FROM members").isNull());
    run({pressYes}, [&] { EXPECT_TRUE(app->members->archive(&parent, {memberId}, label)); });
    run({pressYes}, [&] {
        EXPECT_TRUE(app->archive->purge(&parent, VLMS::ArchiveType::Members, {memberId}, label));
    });

    EXPECT_EQ(db->count("members"), 0);
    EXPECT_EQ(changed->count(), 4);
}

TEST_F(Smoke, CheckoutExtendAndReturnThroughTheRealLoanDialogs)
{
    MemberSeed member = uniqueMemberSeed(5);
    member.status = VLMS::Repositories::MemberStatus::kActive;
    const qint64 memberId = seedMember(*db, member);
    const qint64 bookId = seedBook(*db, uniqueBookSeed(5));
    VLMS::LoanCheckoutPrefill prefill;
    prefill.memberId = memberId;
    prefill.bookId = bookId;

    run({pressOk}, [&] { EXPECT_TRUE(app->loans->checkout(&parent, prefill)); });
    ASSERT_EQ(db->count("loans"), 1);
    const qint64 loanId = db->scalar("SELECT id FROM loans").toInt();
    const std::string due = scalarText("SELECT due_at FROM loans");

    run({pressOk}, [&] { EXPECT_TRUE(app->loans->extend(&parent, loanId)); });
    EXPECT_GT(scalarText("SELECT due_at FROM loans"), due);

    run({pressOk}, [&] { EXPECT_TRUE(app->loans->returnLoan(&parent, loanId)); });
    EXPECT_EQ(scalarText("SELECT substr(returned_at, 1, 10) FROM loans"), isoDaysFromToday(0));
    EXPECT_EQ(changed->count(), 3);
}

TEST_F(Smoke, ReuseAnArchivedNumberOnANewBook)
{
    const qint64 oldBook = seedBook(*db, uniqueBookSeed(6));
    const qint64 copyId = copyIdsOf(*db, oldBook).front();
    const std::string localId =
        scalarText("SELECT local_id FROM book_copies WHERE id = " + std::to_string(copyId));
    ASSERT_TRUE(catalog->archiveBook(oldBook));

    run({pressOk, titleThenOk(QStringLiteral("Smoke Reused Number"))},
        [&] { EXPECT_TRUE(app->archive->reuseNumber(&parent, copyId)); });

    EXPECT_NE(scalarText("SELECT local_id FROM book_copies WHERE id = " + std::to_string(copyId)),
              localId);
    EXPECT_EQ(scalarText("SELECT COUNT(*) FROM book_copies WHERE archived_at IS NULL AND local_id = '"
                         + localId + "'"),
              "1");
}
