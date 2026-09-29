#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QSignalSpy>
#include <QWidget>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

class test_ui_ArchiveWorkflow : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members =
            std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_catalog =
            std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_harness =
            std::make_unique<ArchiveWorkflowHarness>(*m_members, *m_catalog, *m_circulation);
        m_changed = std::make_unique<QSignalSpy>(&m_harness->events, &DataEvents::changed);

        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_TRUE(m_members->archiveMember(m_memberId));
    }

    void TearDown() override
    {
        m_changed.reset();
        m_harness.reset();
        m_circulation.reset();
        m_catalog.reset();
        m_members.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<ArchiveWorkflowHarness> m_harness;
    std::unique_ptr<QSignalSpy> m_changed;
    QWidget m_parent;
    qint64 m_memberId = 0;
};

TEST_F(test_ui_ArchiveWorkflow, RestoreCancelEmitsNothing)
{
    const ModalOutcome outcome = runAndAnswerModal(
        [this]() {
            ASSERT_TRUE(m_harness->archive->restore(&m_parent, ArchiveType::Members, {m_memberId},
                                                    {QStringLiteral("Amina")}));
        },
        qs(Core::Strings::t("common.no")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(m_changed->count(), 0);
    EXPECT_FALSE(
        m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId)).isNull());
}

TEST_F(test_ui_ArchiveWorkflow, RestoreConfirmMemberEmitsMembersChanged)
{
    const ModalOutcome outcome = runAndAnswerModal(
        [this]() {
            ASSERT_TRUE(m_harness->archive->restore(&m_parent, ArchiveType::Members, {m_memberId},
                                                    {QStringLiteral("Amina")}));
        },
        qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Core::Strings::t("archive.restoreConfirm")));
    ASSERT_EQ(m_changed->count(), 1);
    EXPECT_EQ(m_changed->at(0).at(0).value<EntitySet>(), Entity::Members);
    EXPECT_EQ(m_changed->at(0).at(1).value<QList<qint64>>(), QList<qint64>({m_memberId}));
    EXPECT_TRUE(
        m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId)).isNull());
}

TEST_F(test_ui_ArchiveWorkflow, PurgeCancelEmitsNothing)
{
    const ModalOutcome outcome = runAndAnswerModal(
        [this]() {
            ASSERT_TRUE(m_harness->archive->purge(&m_parent, ArchiveType::Members, {m_memberId},
                                                  {QStringLiteral("Amina")}));
        },
        qs(Core::Strings::t("common.no")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(m_changed->count(), 0);
    EXPECT_EQ(m_db->count("members"), 1);
    EXPECT_FALSE(
        m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId)).isNull());
}

TEST_F(test_ui_ArchiveWorkflow, ShowLoansEarlyExitIsOkWithoutCrash)
{
    EXPECT_TRUE(m_harness->archive->showLoans(&m_parent, ArchiveType::Members, 0, QStringLiteral("x")));
    EXPECT_TRUE(m_harness->archive->showLoans(&m_parent, ArchiveType::Loans, 1, QStringLiteral("x")));
    EXPECT_EQ(m_changed->count(), 0);
    EXPECT_TRUE(m_harness->errors.calls.empty());
}
