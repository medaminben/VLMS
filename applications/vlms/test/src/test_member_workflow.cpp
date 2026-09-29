#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QDate>
#include <QSignalSpy>
#include <QWidget>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

class test_ui_MemberWorkflow : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members =
            std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_harness = std::make_unique<MemberWorkflowHarness>(*m_members, *m_circulation);
        m_changed = std::make_unique<QSignalSpy>(&m_harness->events, &DataEvents::changed);
    }

    void TearDown() override
    {
        m_changed.reset();
        m_harness.reset();
        m_circulation.reset();
        m_members.reset();
        m_db.reset();
    }

    qint64 seedMemberWithOpenLoan()
    {
        MemberSeed member = uniqueMemberSeed(1);
        member.status = Repositories::MemberStatus::kActive;
        const qint64 memberId = seedMember(*m_db, member);
        const qint64 bookId = seedBook(*m_db, uniqueBookSeed(1));
        const QDate today = QDate::currentDate();
        EXPECT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(),
                                today.addDays(-10).toString(Qt::ISODate).toStdString(),
                                today.addDays(4).toString(Qt::ISODate).toStdString()),
                  0);
        return memberId;
    }

    qint64 seedArchivableMember()
    {
        MemberSeed member = uniqueMemberSeed(2);
        member.status = Repositories::MemberStatus::kActive;
        return seedMember(*m_db, member);
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<MemberWorkflowHarness> m_harness;
    std::unique_ptr<QSignalSpy> m_changed;
    QWidget m_parent;
};

TEST_F(test_ui_MemberWorkflow, ArchiveBlockedByOpenLoansOffersGoToLoansAndDoesNotArchive)
{
    const qint64 memberId = seedMemberWithOpenLoan();
    ASSERT_GT(memberId, 0);

    const ModalOutcome outcome = runAndAnswerModal(
        [this, memberId]() {
            ASSERT_TRUE(m_harness->members->archive(&m_parent, {memberId}, {QStringLiteral("Amina")}));
        },
        qs(Core::Strings::t("members.goToLoans")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Core::Strings::t("members.deleteBlockedByLoans")));
    EXPECT_EQ(m_harness->navigator.showMemberLoansCount, 1);
    EXPECT_EQ(m_harness->navigator.lastMembershipNumber,
              QString::fromStdString(m_members->getMember(memberId)->membershipNumber));
    EXPECT_EQ(m_changed->count(), 0);
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(memberId))
                    .isNull());
}

TEST_F(test_ui_MemberWorkflow, ArchiveConfirmEmitsMembersChanged)
{
    const qint64 memberId = seedArchivableMember();
    ASSERT_GT(memberId, 0);

    const ModalOutcome outcome = runAndAnswerModal(
        [this, memberId]() {
            ASSERT_TRUE(m_harness->members->archive(&m_parent, {memberId}, {QStringLiteral("Amina")}));
        },
        qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Core::Strings::t("members.deleteConfirm")));
    ASSERT_EQ(m_changed->count(), 1);
    EXPECT_EQ(m_changed->at(0).at(0).value<EntitySet>(), Entity::Members);
    EXPECT_EQ(m_changed->at(0).at(1).value<QList<qint64>>(), QList<qint64>({memberId}));
    EXPECT_FALSE(
        m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(memberId)).isNull());
}
