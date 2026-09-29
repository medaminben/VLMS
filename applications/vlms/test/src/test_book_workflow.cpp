#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>

#include <QApplication>
#include <QDialog>
#include <QSignalSpy>
#include <QTimer>
#include <QWidget>

#include <gtest/gtest.h>

#include <memory>

using namespace Test;

namespace {

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

class BookWorkflow : public ::testing::Test {
protected:
    static void SetUpTestSuite() { VLMS::Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { VLMS::Core::Locale::setCode(VLMS::Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog =
            std::make_unique<VLMS::Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<VLMS::Repositories::CirculationRepository>(m_db->session());
        m_members = std::make_unique<VLMS::Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_harness = std::make_unique<BookWorkflowHarness>(*m_catalog, *m_circulation, *m_members);
        m_changed = std::make_unique<QSignalSpy>(&m_harness->events, &VLMS::DataEvents::changed);
    }

    void TearDown() override
    {
        m_changed.reset();
        m_harness.reset();
        m_members.reset();
        m_circulation.reset();
        m_catalog.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<VLMS::Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<VLMS::Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<VLMS::Repositories::MemberRepository> m_members;
    std::unique_ptr<BookWorkflowHarness> m_harness;
    std::unique_ptr<QSignalSpy> m_changed;
    QWidget m_parent;
};

TEST_F(BookWorkflow, AddCancelEmitsNothingAndShowsNoError)
{
    m_harness->dialogs.nextBook = std::nullopt;

    const auto status = m_harness->books->add(&m_parent);

    EXPECT_TRUE(status);
    EXPECT_EQ(m_changed->count(), 0);
    EXPECT_TRUE(m_harness->errors.calls.empty());
    EXPECT_EQ(m_harness->dialogs.calls, std::vector<QString>{QStringLiteral("addBook")});
}

TEST_F(BookWorkflow, AddSuccessEmitsBooksChanged)
{
    VLMS::BookForm form;
    form.book = uniqueBookSeed(10).toInput();
    form.book.title = "Checklist Add Book";
    m_harness->dialogs.nextBook = form;

    const auto status = m_harness->books->add(&m_parent);

    EXPECT_TRUE(status);
    ASSERT_EQ(m_changed->count(), 1);
    EXPECT_EQ(m_changed->at(0).at(0).value<VLMS::EntitySet>(), VLMS::Entity::Books | VLMS::Entity::Copies);
    EXPECT_TRUE(m_harness->errors.calls.empty());
    EXPECT_EQ(m_db->count("books"), 1);
}

TEST_F(BookWorkflow, EditCancelEmitsNothing)
{
    const qint64 bookId = seedBook(*m_db, uniqueBookSeed(11));
    ASSERT_GT(bookId, 0);

    rejectNextDialog();
    const auto status = m_harness->books->edit(&m_parent, bookId);

    EXPECT_TRUE(status);
    EXPECT_EQ(m_changed->count(), 0);
    EXPECT_TRUE(m_harness->errors.calls.empty());
}

TEST_F(BookWorkflow, CategoriesCancelStillEmitsCategoriesChanged)
{
    m_harness->dialogs.nextCategories = std::nullopt;

    const auto status = m_harness->books->manageCategories(&m_parent);

    EXPECT_TRUE(status);
    ASSERT_EQ(m_changed->count(), 1);
    EXPECT_EQ(m_changed->at(0).at(0).value<VLMS::EntitySet>(), VLMS::Entity::Categories);
    EXPECT_EQ(m_harness->dialogs.calls, std::vector<QString>{QStringLiteral("manageCategories")});
    EXPECT_TRUE(m_harness->errors.calls.empty());
}

TEST_F(BookWorkflow, ArchiveSingleConfirmEmitsBooksChanged)
{
    const qint64 bookId = seedBook(*m_db, uniqueBookSeed(1));

    const ModalOutcome outcome = runAndAnswerModal(
        [this, bookId]() {
            ASSERT_TRUE(m_harness->books->archive(&m_parent, {bookId}, {QStringLiteral("title")}));
        },
        qs(VLMS::Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(VLMS::Core::Strings::t("catalog.deleteConfirm")));
    ASSERT_EQ(m_changed->count(), 1);
    EXPECT_EQ(m_changed->at(0).at(0).value<VLMS::EntitySet>(), VLMS::Entity::Books);
    EXPECT_EQ(m_changed->at(0).at(1).value<QList<qint64>>(), QList<qint64>({bookId}));
    EXPECT_TRUE(m_harness->errors.calls.empty());
    EXPECT_FALSE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId))
                     .isNull());
}

TEST_F(BookWorkflow, ArchiveCancelEmitsNothing)
{
    const qint64 bookId = seedBook(*m_db, uniqueBookSeed(2));

    const ModalOutcome outcome = runAndAnswerModal(
        [this, bookId]() {
            ASSERT_TRUE(m_harness->books->archive(&m_parent, {bookId}, {QStringLiteral("title")}));
        },
        qs(VLMS::Core::Strings::t("common.no")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(m_changed->count(), 0);
    EXPECT_TRUE(m_harness->errors.calls.empty());
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId))
                    .isNull());
}
