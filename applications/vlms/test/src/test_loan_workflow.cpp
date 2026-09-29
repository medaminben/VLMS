#include "WorkflowTestSupport.h"

#include <VLMS/Repositories/MemberTypes.h>

#include <QDate>

using namespace Test;

namespace {

std::string isoDaysFromToday(int days)
{
    return QDate::currentDate().addDays(days).toString(Qt::ISODate).toStdString();
}

}  // namespace

class LoanWorkflow : public WorkflowFixture {
protected:
    struct Seeded {
        qint64 memberId = 0;
        qint64 copyId = 0;
    };

    Seeded seedMemberAndCopy(int index)
    {
        MemberSeed member = uniqueMemberSeed(index);
        member.status = VLMS::Repositories::MemberStatus::kActive;
        Seeded seeded;
        seeded.memberId = seedMember(*db, member);
        seeded.copyId = copyIdsOf(*db, seedBook(*db, uniqueBookSeed(index))).front();
        return seeded;
    }

    qint64 seedOpenLoan()
    {
        const Seeded seeded = seedMemberAndCopy(1);
        return rawInsertLoan(*db, seeded.memberId, seeded.copyId, isoDaysFromToday(-5),
                             isoDaysFromToday(9));
    }

    VLMS::LoanForm loanForm(const Seeded& seeded) const
    {
        VLMS::LoanForm form;
        form.loan.memberId = seeded.memberId;
        form.loan.bookCopyId = seeded.copyId;
        form.loan.borrowedAt = isoDaysFromToday(0);
        form.loan.dueAt = isoDaysFromToday(14);
        return form;
    }

    std::string loanColumn(qint64 loanId, const std::string& column) const
    {
        return scalarText("SELECT " + column + " FROM loans WHERE id = " + std::to_string(loanId));
    }
};

TEST_F(LoanWorkflow, CheckoutCancelEmitsNothing)
{
    const auto status = loans->checkout(&parent, {});

    EXPECT_TRUE(status);
    EXPECT_EQ(dialogs.count(QStringLiteral("checkout")), 1);
    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(db->count("loans"), 0);
}

TEST_F(LoanWorkflow, CheckoutPassesThePrefillToTheDialog)
{
    VLMS::LoanCheckoutPrefill prefill;
    prefill.bookId = 7;
    prefill.memberId = 9;

    ASSERT_TRUE(loans->checkout(&parent, prefill));

    ASSERT_EQ(dialogs.checkoutPrefills.size(), 1U);
    EXPECT_EQ(dialogs.checkoutPrefills.front().bookId, 7);
    EXPECT_EQ(dialogs.checkoutPrefills.front().memberId, 9);
}

TEST_F(LoanWorkflow, CheckoutSuccessCreatesLoanAndEmitsLoansAndCopies)
{
    const Seeded seeded = seedMemberAndCopy(2);
    dialogs.nextLoan = loanForm(seeded);

    const auto status = loans->checkout(&parent, {});

    EXPECT_TRUE(status);
    EXPECT_TRUE(errors.calls.empty());
    ASSERT_EQ(db->count("loans"), 1);
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Loans | VLMS::Entity::Copies);
}

TEST_F(LoanWorkflow, CheckoutOfALentCopyShowsTheErrorOnceAndEmitsNothing)
{
    const Seeded seeded = seedMemberAndCopy(3);
    ASSERT_GT(rawInsertLoan(*db, seeded.memberId, seeded.copyId, isoDaysFromToday(-2),
                            isoDaysFromToday(12)),
              0);
    dialogs.nextLoan = loanForm(seeded);

    const auto status = loans->checkout(&parent, {});

    EXPECT_FALSE(status);
    EXPECT_EQ(errorKeys(), std::vector<std::string>{"error.loan.copyOnLoan"});
    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(db->count("loans"), 1);
}

TEST_F(LoanWorkflow, ReturnCancelEmitsNothing)
{
    const qint64 loanId = seedOpenLoan();

    const auto status = loans->returnLoan(&parent, loanId);

    EXPECT_TRUE(status);
    EXPECT_EQ(dialogs.count(QStringLiteral("returnLoan")), 1);
    EXPECT_EQ(changed->count(), 0);
    EXPECT_TRUE(loanColumn(loanId, "returned_at").empty());
}

TEST_F(LoanWorkflow, ReturnSuccessRecordsTheDateAndEmitsLoansAndCopies)
{
    const qint64 loanId = seedOpenLoan();
    dialogs.nextReturn = VLMS::ReturnForm{QString::fromStdString(isoDaysFromToday(0)),
                                          QStringLiteral("fine")};

    const auto status = loans->returnLoan(&parent, loanId);

    EXPECT_TRUE(status);
    EXPECT_TRUE(errors.calls.empty());
    EXPECT_EQ(loanColumn(loanId, "substr(returned_at, 1, 10)"), isoDaysFromToday(0));
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Loans | VLMS::Entity::Copies);
    EXPECT_EQ(changedIds(0), QList<qint64>({loanId}));
}

TEST_F(LoanWorkflow, ReturnOfAReturnedLoanInformsAndOpensNoForm)
{
    const qint64 loanId = seedOpenLoan();
    ASSERT_TRUE(circulation->returnLoan(loanId));

    const auto status = loans->returnLoan(&parent, loanId);

    EXPECT_TRUE(status);
    EXPECT_EQ(dialogs.count(QStringLiteral("returnLoan")), 0);
    EXPECT_EQ(dialogs.count(QStringLiteral("inform")), 1);
    EXPECT_EQ(changed->count(), 0);
}

TEST_F(LoanWorkflow, ExtendCancelEmitsNothing)
{
    const qint64 loanId = seedOpenLoan();
    const std::string dueBefore = loanColumn(loanId, "due_at");

    const auto status = loans->extend(&parent, loanId);

    EXPECT_TRUE(status);
    EXPECT_EQ(dialogs.count(QStringLiteral("extendLoan")), 1);
    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(loanColumn(loanId, "due_at"), dueBefore);
}

TEST_F(LoanWorkflow, ExtendSuccessMovesTheDueDateAndEmitsLoans)
{
    const qint64 loanId = seedOpenLoan();
    dialogs.nextExtend = VLMS::ExtendForm{QString::fromStdString(isoDaysFromToday(30))};

    const auto status = loans->extend(&parent, loanId);

    EXPECT_TRUE(status);
    EXPECT_TRUE(errors.calls.empty());
    EXPECT_EQ(loanColumn(loanId, "due_at"), isoDaysFromToday(30));
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Loans);
    EXPECT_EQ(changedIds(0), QList<qint64>({loanId}));
}

TEST_F(LoanWorkflow, ExtendToAnEarlierDateShowsTheErrorOnceAndKeepsTheDueDate)
{
    const qint64 loanId = seedOpenLoan();
    const std::string dueBefore = loanColumn(loanId, "due_at");
    dialogs.nextExtend = VLMS::ExtendForm{QString::fromStdString(isoDaysFromToday(1))};

    const auto status = loans->extend(&parent, loanId);

    EXPECT_FALSE(status);
    EXPECT_EQ(errorKeys(), std::vector<std::string>{"error.loan.extendNotAfter"});
    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(loanColumn(loanId, "due_at"), dueBefore);
}

TEST_F(LoanWorkflow, ArchiveSingleConfirmEmitsLoansChanged)
{
    const qint64 loanId = seedOpenLoan();
    ASSERT_TRUE(circulation->returnLoan(loanId));
    dialogs.askYesNoResult = true;

    ASSERT_TRUE(loans->archive(&parent, {loanId}, {QStringLiteral("loan")}));

    EXPECT_EQ(dialogs.count(QStringLiteral("askYesNo")), 1);
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Loans);
    EXPECT_FALSE(scalarIsNull("SELECT archived_at FROM loans WHERE id = " + std::to_string(loanId)));
}

TEST_F(LoanWorkflow, ArchiveSingleDeclinedEmitsNothing)
{
    const qint64 loanId = seedOpenLoan();
    ASSERT_TRUE(circulation->returnLoan(loanId));
    dialogs.askYesNoResult = false;

    ASSERT_TRUE(loans->archive(&parent, {loanId}, {QStringLiteral("loan")}));

    EXPECT_EQ(changed->count(), 0);
    EXPECT_TRUE(scalarIsNull("SELECT archived_at FROM loans WHERE id = " + std::to_string(loanId)));
}

TEST_F(LoanWorkflow, ArchiveManySkipsOpenLoansAndArchivesTheRest)
{
    const Seeded a = seedMemberAndCopy(4);
    const Seeded b = seedMemberAndCopy(5);
    const qint64 returned = rawInsertLoan(*db, a.memberId, a.copyId, isoDaysFromToday(-9),
                                          isoDaysFromToday(5));
    ASSERT_TRUE(circulation->returnLoan(returned));
    const qint64 open = rawInsertLoan(*db, b.memberId, b.copyId, isoDaysFromToday(-3),
                                      isoDaysFromToday(11));
    dialogs.confirmBulkResult = true;

    ASSERT_TRUE(loans->archive(&parent, {returned, open},
                               {QStringLiteral("returned"), QStringLiteral("open")}));

    ASSERT_EQ(dialogs.plans.size(), 1U);
    EXPECT_TRUE(dialogs.plans.front().text.contains(QStringLiteral("open")));
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedIds(0), QList<qint64>({returned}));
    EXPECT_FALSE(scalarIsNull("SELECT archived_at FROM loans WHERE id = " + std::to_string(returned)));
    EXPECT_TRUE(scalarIsNull("SELECT archived_at FROM loans WHERE id = " + std::to_string(open)));
}
