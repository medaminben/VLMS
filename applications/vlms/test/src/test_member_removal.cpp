#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"

#include "ui/members/MembersPage.h"

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
#include <QTableWidget>
#include <QTest>
#include <QTimer>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>

using VLMS::Locale;
using VLMS::Strings;
using namespace VLMS;
using namespace Test;

namespace {

void clickDelete(MembersPage* page)
{
    clickButtonWithText(page, qs(Strings::t("members.delete")));
}

void selectFirstMember(MembersPage* page)
{
    auto* table = page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_GT(table->rowCount(), 0);
    table->selectRow(0);
}

}  // namespace

class test_ui_MemberRemoval : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }

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
        m_circulation.reset();
        m_members.reset();
        m_db.reset();
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

        m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
        selectFirstMember(m_page.get());
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<MembersPage> m_page;
    qint64 m_memberId = 0;
};

TEST_F(test_ui_MemberRemoval, OpenLoansOfferGoToLoansAndEmitTheMembershipNumber)
{
    seedMemberWithLoan({});

    QSignalSpy spy(m_page.get(), &MembersPage::memberLoansRequested);
    const QString screenshot = qEnvironmentVariable("VLMS_TEST_SHOT_DIR").isEmpty()
        ? QString()
        : QDir(qEnvironmentVariable("VLMS_TEST_SHOT_DIR")).filePath(QStringLiteral("blocked-by-loans.png"));

    const ModalOutcome outcome = runAndAnswerModal(
        [this]() { clickDelete(m_page.get()); }, qs(Strings::t("members.goToLoans")), std::nullopt,
        screenshot);

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Strings::t("members.deleteBlockedByLoans")));
    EXPECT_FALSE(outcome.hasCheckBox);
    EXPECT_EQ(outcome.buttonLabels.filter(qs(Strings::t("members.goToLoans"))).size(), 1);
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.front().front().toString(),
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
        [this]() { clickDelete(m_page.get()); }, qs(Strings::t("common.no")), std::nullopt,
        screenshot);

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Strings::t("members.deleteConfirm")));
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
        [this]() { clickDelete(m_page.get()); }, qs(Strings::t("common.yes")), std::nullopt);
    EXPECT_TRUE(outcome.appeared);
    EXPECT_EQ(m_db->count("members"), 1);
    EXPECT_EQ(m_db->count("loans"), 1);
    EXPECT_FALSE(
        m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId))
            .isNull());

    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 0);
}

TEST_F(test_ui_MemberRemoval, AMemberWithBorrowingHistoryIsArchivedWithoutASecondQuestion)
{
    const QDate today = QDate::currentDate();
    seedMemberWithLoan(today.addDays(-20).toString(Qt::ISODate));

    // One box, not two: history no longer changes what Delete can do, because
    // Delete no longer offers to destroy anything.
    const QList<ModalOutcome> outcomes = runAndAnswerModals(
        [this]() { clickDelete(m_page.get()); },
        {ModalAnswer{qs(Strings::t("common.yes")), std::nullopt, {}},
         ModalAnswer{qs(Strings::t("common.ok")), std::nullopt, {}}});

    ASSERT_TRUE(outcomes.at(0).appeared);
    EXPECT_FALSE(outcomes.at(1).appeared);
    EXPECT_EQ(m_db->count("members"), 1);
    EXPECT_EQ(m_db->count("loans"), 1);
    EXPECT_FALSE(
        m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId))
            .isNull());
}

namespace {

void tickMember(QTableWidget* table, qint64 memberId)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem* item = table->item(row, 0);
        if (item != nullptr && item->data(Qt::UserRole).toLongLong() == memberId) {
            item->setCheckState(Qt::Checked);
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

TEST_F(test_ui_MemberRemoval, TwoTickedMembersAreArchivedAndTheHighlightedThirdStays)
{
    const qint64 first = seedMember(*m_db, uniqueMemberSeed(10));
    const qint64 second = seedMember(*m_db, uniqueMemberSeed(11));
    const qint64 highlighted = seedMember(*m_db, uniqueMemberSeed(12));
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->data(Qt::UserRole).toLongLong() == highlighted) {
            table->selectRow(row);
        }
    }
    tickMember(table, first);
    tickMember(table, second);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(m_page.get()); }, qs(Strings::t("common.yes")));

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
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    tickMember(table, blocked);
    tickMember(table, free);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(m_page.get()); }, qs(Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_NE(outcome.text, qs(Strings::t("members.deleteConfirm")));
    EXPECT_FALSE(memberArchived(*m_db, blocked));
    EXPECT_TRUE(memberArchived(*m_db, free));
}
