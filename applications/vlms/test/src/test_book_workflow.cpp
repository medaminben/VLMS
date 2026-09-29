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

#include <QSignalSpy>
#include <QWidget>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

class test_ui_BookWorkflow : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog =
            std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_harness = std::make_unique<BookWorkflowHarness>(*m_catalog, *m_circulation, *m_members);
        m_changed = std::make_unique<QSignalSpy>(&m_harness->events, &DataEvents::changed);
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
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<BookWorkflowHarness> m_harness;
    std::unique_ptr<QSignalSpy> m_changed;
    QWidget m_parent;
};

TEST_F(test_ui_BookWorkflow, CancelAddEmitsNothingAndShowsNoError)
{
    m_harness->dialogs.nextBook = std::nullopt;

    const auto status = m_harness->books->add(&m_parent);

    EXPECT_TRUE(status);
    EXPECT_EQ(m_changed->count(), 0);
    EXPECT_TRUE(m_harness->errors.calls.empty());
    EXPECT_EQ(m_harness->dialogs.calls, std::vector<QString>{QStringLiteral("addBook")});
}

TEST_F(test_ui_BookWorkflow, ArchiveSingleConfirmEmitsBooksChanged)
{
    const qint64 bookId = seedBook(*m_db, uniqueBookSeed(1));

    const ModalOutcome outcome = runAndAnswerModal(
        [this, bookId]() {
            ASSERT_TRUE(m_harness->books->archive(&m_parent, {bookId}, {QStringLiteral("title")}));
        },
        qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Core::Strings::t("catalog.deleteConfirm")));
    ASSERT_EQ(m_changed->count(), 1);
    EXPECT_EQ(m_changed->at(0).at(0).value<EntitySet>(), Entity::Books);
    EXPECT_EQ(m_changed->at(0).at(1).value<QList<qint64>>(), QList<qint64>({bookId}));
    EXPECT_TRUE(m_harness->errors.calls.empty());
    EXPECT_FALSE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId))
                     .isNull());
}

TEST_F(test_ui_BookWorkflow, ArchiveCancelEmitsNothing)
{
    const qint64 bookId = seedBook(*m_db, uniqueBookSeed(2));

    const ModalOutcome outcome = runAndAnswerModal(
        [this, bookId]() {
            ASSERT_TRUE(m_harness->books->archive(&m_parent, {bookId}, {QStringLiteral("title")}));
        },
        qs(Core::Strings::t("common.no")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(m_changed->count(), 0);
    EXPECT_TRUE(m_harness->errors.calls.empty());
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId))
                    .isNull());
}
