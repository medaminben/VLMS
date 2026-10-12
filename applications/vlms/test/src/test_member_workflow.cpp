#include "WorkflowTestSupport.h"

#include <QDate>

using namespace Test;

class MemberWorkflow : public WorkflowFixture {
protected:
    VLMS::MemberForm memberForm(int index) const
    {
        VLMS::MemberForm form;
        form.member = uniqueMemberSeed(index).toInput();
        return form;
    }

    std::string memberColumn(qint64 memberId, const std::string& column) const
    {
        return scalarText("SELECT " + column + " FROM members WHERE id = " + std::to_string(memberId));
    }

    bool archived(qint64 memberId) const
    {
        return !scalarIsNull("SELECT archived_at FROM members WHERE id = " + std::to_string(memberId));
    }

    void lendOneBookTo(qint64 memberId, int index)
    {
        const QDate today = QDate::currentDate();
        ASSERT_GT(rawInsertLoan(*db, memberId,
                                copyIdsOf(*db, seedBook(*db, uniqueBookSeed(index))).front(),
                                today.addDays(-2).toString(Qt::ISODate).toStdString(),
                                today.addDays(12).toString(Qt::ISODate).toStdString()),
                  0);
    }
};

TEST_F(MemberWorkflow, AddCancelEmitsNothing)
{
    const auto status = members->add(&parent);

    EXPECT_TRUE(status);
    ASSERT_EQ(dialogs.memberRequests.size(), 1U);
    EXPECT_FALSE(dialogs.memberRequests.front().has_value());
    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(db->count("members"), 0);
}

TEST_F(MemberWorkflow, AddSuccessSavesAndEmitsMembers)
{
    dialogs.nextMember = memberForm(1);

    const auto status = members->add(&parent);

    EXPECT_TRUE(status);
    EXPECT_TRUE(errors.calls.empty());
    ASSERT_EQ(db->count("members"), 1);
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Members);
}

TEST_F(MemberWorkflow, AddWithABadEmailShowsTheErrorOnceAndSavesNothing)
{
    VLMS::MemberForm form = memberForm(2);
    form.member.email = "not-an-email";
    dialogs.nextMember = form;

    const auto status = members->add(&parent);

    EXPECT_FALSE(status);
    EXPECT_EQ(errorKeys(), std::vector<std::string>{"error.member.invalidEmail"});
    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(db->count("members"), 0);
}

TEST_F(MemberWorkflow, EditOpensTheFullRecord)
{
    const qint64 memberId = seedMember(*db, uniqueMemberSeed(3));

    ASSERT_TRUE(members->edit(&parent, memberId));

    ASSERT_EQ(dialogs.memberRequests.size(), 1U);
    ASSERT_TRUE(dialogs.memberRequests.front().has_value());
    EXPECT_EQ(dialogs.memberRequests.front()->id, memberId);
}

TEST_F(MemberWorkflow, EditCancelEmitsNothing)
{
    const qint64 memberId = seedMember(*db, uniqueMemberSeed(4));

    ASSERT_TRUE(members->edit(&parent, memberId));

    EXPECT_EQ(changed->count(), 0);
}

TEST_F(MemberWorkflow, EditSuccessSavesAndEmitsTheMember)
{
    const qint64 memberId = seedMember(*db, uniqueMemberSeed(5));
    VLMS::MemberForm form = memberForm(5);
    form.member.membershipNumber = memberColumn(memberId, "membership_number");
    form.member.city = "Tunis";
    dialogs.nextMember = form;

    const auto status = members->edit(&parent, memberId);

    EXPECT_TRUE(status) << (errors.calls.empty() ? "" : errors.calls.front().key);
    EXPECT_EQ(memberColumn(memberId, "city"), "Tunis");
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedIds(0), QList<qint64>({memberId}));
}

TEST_F(MemberWorkflow, ArchiveBlockedByOpenLoansOffersGoToLoansAndDoesNotArchive)
{
    const qint64 memberId = seedMember(*db, uniqueMemberSeed(6));
    lendOneBookTo(memberId, 6);
    dialogs.warnWithActionResult = true;

    ASSERT_TRUE(members->archive(&parent, {memberId}, {QStringLiteral("Amina")}));

    EXPECT_EQ(dialogs.count(QStringLiteral("warnWithAction")), 1);
    EXPECT_EQ(navigator.showMemberLoansCount, 1);
    EXPECT_EQ(navigator.lastMembershipNumber.toStdString(), memberColumn(memberId, "membership_number"));
    EXPECT_FALSE(archived(memberId));
    EXPECT_EQ(changed->count(), 0);
}

TEST_F(MemberWorkflow, ArchiveBlockedAndDismissedDoesNotNavigate)
{
    const qint64 memberId = seedMember(*db, uniqueMemberSeed(7));
    lendOneBookTo(memberId, 7);
    dialogs.warnWithActionResult = false;

    ASSERT_TRUE(members->archive(&parent, {memberId}, {QStringLiteral("Amina")}));

    EXPECT_EQ(navigator.showMemberLoansCount, 0);
    EXPECT_FALSE(archived(memberId));
}

TEST_F(MemberWorkflow, ArchiveConfirmEmitsMembersChanged)
{
    const qint64 memberId = seedMember(*db, uniqueMemberSeed(8));
    dialogs.askYesNoResult = true;

    ASSERT_TRUE(members->archive(&parent, {memberId}, {QStringLiteral("Amina")}));

    EXPECT_TRUE(archived(memberId));
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Members);
}

TEST_F(MemberWorkflow, ArchiveManySkipsMembersWithLoans)
{
    const qint64 free = seedMember(*db, uniqueMemberSeed(9));
    const qint64 borrowing = seedMember(*db, uniqueMemberSeed(10));
    lendOneBookTo(borrowing, 10);
    dialogs.confirmBulkResult = true;

    ASSERT_TRUE(members->archive(&parent, {free, borrowing},
                                 {QStringLiteral("Free"), QStringLiteral("Borrowing")}));

    ASSERT_EQ(dialogs.plans.size(), 1U);
    EXPECT_TRUE(dialogs.plans.front().text.contains(QStringLiteral("Borrowing")));
    EXPECT_TRUE(archived(free));
    EXPECT_FALSE(archived(borrowing));
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedIds(0), QList<qint64>({free}));
}

TEST_F(MemberWorkflow, ShowLoansOpensTheHistoryPinnedToTheMember)
{
    const qint64 memberId = seedMember(*db, uniqueMemberSeed(11));

    ASSERT_TRUE(members->showLoans(&parent, memberId));

    ASSERT_EQ(dialogs.historyRequests.size(), 1U);
    EXPECT_EQ(dialogs.historyRequests.front().kind, VLMS::LoanHistoryKind::Member);
    EXPECT_EQ(dialogs.historyRequests.front().id, memberId);
}
