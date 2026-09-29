#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QApplication>
#include <QDate>
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

class LoanWorkflow : public ::testing::Test {
protected:
    static void SetUpTestSuite() { VLMS::Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { VLMS::Core::Locale::setCode(VLMS::Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_circulation = std::make_unique<VLMS::Repositories::CirculationRepository>(m_db->session());
        m_harness = std::make_unique<LoanWorkflowHarness>(*m_circulation);
        m_changed = std::make_unique<QSignalSpy>(&m_harness->events, &VLMS::DataEvents::changed);
    }

    void TearDown() override
    {
        m_changed.reset();
        m_harness.reset();
        m_circulation.reset();
        m_db.reset();
    }

    qint64 seedOpenLoan()
    {
        MemberSeed member = uniqueMemberSeed(1);
        member.status = VLMS::Repositories::MemberStatus::kActive;
        const qint64 memberId = seedMember(*m_db, member);
        const qint64 bookId = seedBook(*m_db, uniqueBookSeed(1));
        const QDate today = QDate::currentDate();
        return rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(),
                             today.addDays(-5).toString(Qt::ISODate).toStdString(),
                             today.addDays(9).toString(Qt::ISODate).toStdString());
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<VLMS::Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<LoanWorkflowHarness> m_harness;
    std::unique_ptr<QSignalSpy> m_changed;
    QWidget m_parent;
};

TEST_F(LoanWorkflow, ReturnCancelEmitsNothing)
{
    const qint64 loanId = seedOpenLoan();
    ASSERT_GT(loanId, 0);

    rejectNextDialog();
    const auto status = m_harness->loans->returnLoan(&m_parent, loanId);

    EXPECT_TRUE(status);
    EXPECT_EQ(m_changed->count(), 0);
    EXPECT_TRUE(m_harness->errors.calls.empty());
    EXPECT_TRUE(m_db->scalar("SELECT returned_at FROM loans WHERE id = " + std::to_string(loanId))
                    .isNull());
}

TEST_F(LoanWorkflow, CheckoutCancelEmitsNothing)
{
    MemberSeed member = uniqueMemberSeed(2);
    member.status = VLMS::Repositories::MemberStatus::kActive;
    ASSERT_GT(seedMember(*m_db, member), 0);
    ASSERT_GT(seedBook(*m_db, uniqueBookSeed(2)), 0);

    rejectNextDialog();
    const auto status = m_harness->loans->checkout(&m_parent);

    EXPECT_TRUE(status);
    EXPECT_EQ(m_changed->count(), 0);
    EXPECT_TRUE(m_harness->errors.calls.empty());
    EXPECT_EQ(m_db->count("loans"), 0);
}

TEST_F(LoanWorkflow, ArchiveSingleConfirmEmitsLoansChanged)
{
    MemberSeed member = uniqueMemberSeed(3);
    member.status = VLMS::Repositories::MemberStatus::kActive;
    const qint64 memberId = seedMember(*m_db, member);
    const qint64 bookId = seedBook(*m_db, uniqueBookSeed(3));
    const QDate today = QDate::currentDate();
    const qint64 loanId =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(),
                      today.addDays(-20).toString(Qt::ISODate).toStdString(),
                      today.addDays(-6).toString(Qt::ISODate).toStdString(),
                      today.addDays(-5).toString(Qt::ISODate).toStdString());
    ASSERT_GT(loanId, 0);

    const ModalOutcome outcome = runAndAnswerModal(
        [this, loanId]() {
            ASSERT_TRUE(m_harness->loans->archive(&m_parent, {loanId}, {QStringLiteral("loan")}));
        },
        qs(VLMS::Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(VLMS::Core::Strings::t("circulation.archiveLoan")));
    ASSERT_EQ(m_changed->count(), 1);
    EXPECT_EQ(m_changed->at(0).at(0).value<VLMS::EntitySet>(), VLMS::Entity::Loans);
    EXPECT_EQ(m_changed->at(0).at(1).value<QList<qint64>>(), QList<qint64>({loanId}));
    EXPECT_FALSE(m_db->scalar("SELECT archived_at FROM loans WHERE id = " + std::to_string(loanId))
                     .isNull());
}
