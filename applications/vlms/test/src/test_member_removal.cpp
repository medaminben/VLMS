#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"

#include "ui/ListPage.h"
#include "ui/presentation/MemberListSpec.h"
#include "ui/workflows/Navigator.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>
#include <VLMS/Core/Strings.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QDate>
#include <QDir>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
#include <QTimer>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>

using namespace VLMS;
using namespace Test;

namespace {

ListPage::Config membersConfig()
{
    ListPage::Config cfg;
    cfg.titleKey = QStringLiteral("page.members.title");
    cfg.bodyKey = QStringLiteral("page.members.body");
    cfg.searchPlaceholderKey = QStringLiteral("members.searchPlaceholder");
    cfg.imageObjectName = QStringLiteral("memberPhoto");
    cfg.imageBounds = memberPhotoPreviewBounds();
    return cfg;
}

void clickDelete(ListPage* page)
{
    auto* button = page->findChild<QPushButton*>(QStringLiteral("listAction_archive"));
    ASSERT_NE(button, nullptr);
    button->click();
}

void selectFirstMember(ListPage* page)
{
    QTableView* table = page->table();
    ASSERT_NE(table, nullptr);
    ASSERT_GT(table->model()->rowCount(), 0);
    table->selectRow(0);
}

void tickMember(ListPage* page, qint64 memberId)
{
    QTableView* table = page->table();
    ASSERT_NE(table, nullptr);
    for (int row = 0; row < table->model()->rowCount(); ++row) {
        if (table->model()->index(row, 0).data(Qt::UserRole).toLongLong() == memberId) {
            ASSERT_TRUE(table->model()->setData(table->model()->index(row, 0),
                                                Qt::Checked, Qt::CheckStateRole));
            return;
        }
    }
    FAIL() << "member " << memberId << " is not on the page";
}

bool memberArchived(TestDatabase& db, qint64 memberId)
{
    return !db.scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(memberId))
                .isNull();
}

}  // namespace

class test_ui_MemberRemoval : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_page.reset();
        m_spec.reset();
        m_memberWorkflows.reset();
        m_circulation.reset();
        m_members.reset();
        m_db.reset();
    }

    void openPage()
    {
        m_memberWorkflows = std::make_unique<MemberWorkflowHarness>(*m_members, *m_circulation);
        m_spec = std::make_unique<MemberListSpec>(*m_members, *m_memberWorkflows->members);
        m_page = std::make_unique<ListPage>(m_spec.get(), m_memberWorkflows->events, membersConfig());
    }

    void seedMemberWithLoan(const QString& returnedAt)
    {
        MemberSeed member;
        member.membershipNumber = "M-2024-777";
        member.status = Repositories::MemberStatus::kActive;
        m_memberId = seedMember(*m_db, member);
        ASSERT_GT(m_memberId, 0);

        const qint64 bookId = seedBook(*m_db, BookSeed{});
        ASSERT_GT(bookId, 0);

        const QDate today = QDate::currentDate();
        ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copyIdsOf(*m_db, bookId).front(),
                                today.addDays(-30).toString(Qt::ISODate).toStdString(),
                                today.addDays(-16).toString(Qt::ISODate).toStdString(),
                                returnedAt.toStdString()),
                  0);

        openPage();
        selectFirstMember(m_page.get());
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    VLMS::FakeNavigator m_navigator;
    std::unique_ptr<MemberWorkflowHarness> m_memberWorkflows;
    std::unique_ptr<MemberListSpec> m_spec;
    std::unique_ptr<ListPage> m_page;
    qint64 m_memberId = 0;
};

TEST_F(test_ui_MemberRemoval, OpenLoansOfferGoToLoansAndEmitTheMembershipNumber)
{
    seedMemberWithLoan({});

    // Navigator records the jump; no page signal anymore.
    const int before = m_memberWorkflows->navigator.showMemberLoansCount;
    const QString screenshot = qEnvironmentVariable("VLMS_TEST_SHOT_DIR").isEmpty()
        ? QString()
        : QDir(qEnvironmentVariable("VLMS_TEST_SHOT_DIR")).filePath(QStringLiteral("blocked-by-loans.png"));

    const ModalOutcome outcome = runAndAnswerModal(
        [this]() { clickDelete(m_page.get()); }, qs(Core::Strings::t("members.goToLoans")), std::nullopt,
        screenshot);

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Core::Strings::t("members.deleteBlockedByLoans")));
    EXPECT_FALSE(outcome.hasCheckBox);
    EXPECT_EQ(outcome.buttonLabels.filter(qs(Core::Strings::t("members.goToLoans"))).size(), 1);
    EXPECT_EQ(m_memberWorkflows->navigator.showMemberLoansCount, before + 1);
    EXPECT_EQ(m_memberWorkflows->navigator.lastMembershipNumber,
              QString::fromStdString(m_members->getMember(m_memberId)->membershipNumber));
    EXPECT_EQ(m_db->count("members"), 1);
}

TEST_F(test_ui_MemberRemoval, ConfirmationIsAPlainYesOrNo)
{
    const QDate today = QDate::currentDate();
    seedMemberWithLoan(today.addDays(-20).toString(Qt::ISODate));

    const QString screenshot = qEnvironmentVariable("VLMS_TEST_SHOT_DIR").isEmpty()
        ? QString()
        : QDir(qEnvironmentVariable("VLMS_TEST_SHOT_DIR"))
              .filePath(QStringLiteral("confirm-delete.png"));

    const ModalOutcome outcome = runAndAnswerModal(
        [this]() { clickDelete(m_page.get()); }, qs(Core::Strings::t("common.no")), std::nullopt,
        screenshot);

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Core::Strings::t("members.deleteConfirm")));
    // Delete archives. Destroying a record is the Archive's business now, so
    // there is nothing here to opt out of.
    EXPECT_FALSE(outcome.hasCheckBox);
    EXPECT_EQ(m_db->count("members"), 1);
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId))
                    .isNull());
}

TEST_F(test_ui_MemberRemoval, ArchivingRemovesTheMemberFromTheList)
{
    const QDate today = QDate::currentDate();
    seedMemberWithLoan(today.addDays(-20).toString(Qt::ISODate));

    const ModalOutcome outcome = runAndAnswerModal(
        [this]() { clickDelete(m_page.get()); }, qs(Core::Strings::t("common.yes")), std::nullopt);
    EXPECT_TRUE(outcome.appeared);
    EXPECT_EQ(m_db->count("members"), 1);
    EXPECT_EQ(m_db->count("loans"), 1);
    EXPECT_FALSE(
        m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId))
            .isNull());

    QTableView* table = m_page->table();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->model()->rowCount(), 0);
}

TEST_F(test_ui_MemberRemoval, AMemberWithBorrowingHistoryIsArchivedWithoutASecondQuestion)
{
    const QDate today = QDate::currentDate();
    seedMemberWithLoan(today.addDays(-20).toString(Qt::ISODate));

    // One box, not two: history no longer changes what Delete can do, because
    // Delete no longer offers to destroy anything.
    const QList<ModalOutcome> outcomes = runAndAnswerModals(
        [this]() { clickDelete(m_page.get()); },
        {ModalAnswer{qs(Core::Strings::t("common.yes")), std::nullopt, {}},
         ModalAnswer{qs(Core::Strings::t("common.ok")), std::nullopt, {}}});

    ASSERT_TRUE(outcomes.at(0).appeared);
    EXPECT_FALSE(outcomes.at(1).appeared);
    EXPECT_EQ(m_db->count("members"), 1);
    EXPECT_EQ(m_db->count("loans"), 1);
    EXPECT_FALSE(
        m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId))
            .isNull());
}

TEST_F(test_ui_MemberRemoval, TwoTickedMembersAreArchivedAndTheHighlightedThirdStays)
{
    const qint64 first = seedMember(*m_db, uniqueMemberSeed(10));
    const qint64 second = seedMember(*m_db, uniqueMemberSeed(11));
    const qint64 highlighted = seedMember(*m_db, uniqueMemberSeed(12));
    openPage();
    QTableView* table = m_page->table();
    ASSERT_NE(table, nullptr);
    for (int row = 0; row < table->model()->rowCount(); ++row) {
        if (table->model()->index(row, 0).data(Qt::UserRole).toLongLong() == highlighted) {
            table->selectRow(row);
        }
    }
    tickMember(m_page.get(), first);
    tickMember(m_page.get(), second);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(m_page.get()); }, qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, QStringLiteral("Archive 2 members?"));
    EXPECT_TRUE(memberArchived(*m_db, first));
    EXPECT_TRUE(memberArchived(*m_db, second));
    EXPECT_FALSE(memberArchived(*m_db, highlighted));
}

TEST_F(test_ui_MemberRemoval, ATickedMemberWithABookOutStaysWhileTheOtherIsArchived)
{
    const qint64 blocked = seedMember(*m_db, uniqueMemberSeed(20));
    const qint64 free = seedMember(*m_db, uniqueMemberSeed(21));
    const qint64 bookId = seedBook(*m_db, uniqueBookSeed(20));
    ASSERT_GT(rawInsertLoan(*m_db, blocked, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                            "2026-09-15"),
              0);
    openPage();
    tickMember(m_page.get(), blocked);
    tickMember(m_page.get(), free);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(m_page.get()); }, qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_NE(outcome.text, qs(Core::Strings::t("members.deleteConfirm")));
    EXPECT_FALSE(memberArchived(*m_db, blocked));
    EXPECT_TRUE(memberArchived(*m_db, free));
}
