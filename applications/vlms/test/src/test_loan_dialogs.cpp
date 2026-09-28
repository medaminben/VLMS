#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/circulation/LoanCheckoutDialog.h"
#include "ui/circulation/LoanExtendDialog.h"
#include "ui/circulation/LoanReturnDialog.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Repositories/LoanTypes.h>

#include <QDate>
#include <QDateEdit>

#include <gtest/gtest.h>

using VLMS::Clock;
using namespace VLMS::Test;

class test_ui_LoanDialogs : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        EXPECT_FALSE(Clock::isOverridden()) << "a test leaked a clock override";
        m_repository.reset();
        m_db.reset();
    }

    [[nodiscard]] LoanRecord loanDueOn(const QString& dueAt, const QString& borrowedAt = {}) const
    {
        LoanRecord loan;
        loan.id = 1;
        loan.memberName = "Amina Ben Salah";
        loan.membershipNumber = "M-0001";
        loan.bookTitle = "Al-Muqaddima";
        loan.copyCode = "C-0001";
        loan.borrowedAt = borrowedAt.isEmpty()
            ? QDate::currentDate().addDays(-3).toString(Qt::ISODate).toStdString()
            : borrowedAt.toStdString();
        loan.dueAt = dueAt.toStdString();
        return loan;
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CirculationRepository> m_repository;
};

TEST_F(test_ui_LoanDialogs, CheckoutDialogDefaultsToTodayAndFourteenDays)
{
    LoanCheckoutDialog dialog(*m_repository);
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 2);
    EXPECT_EQ(edits.at(0)->date(), QDate::currentDate());
    EXPECT_EQ(edits.at(1)->date(), QDate::currentDate().addDays(14));
}

TEST_F(test_ui_LoanDialogs, CheckoutDialogDueFollowsAChangedBorrowDate)
{
    LoanCheckoutDialog dialog(*m_repository);
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 2);
    const QDate backdated = QDate::currentDate().addDays(-5);
    edits.at(0)->setDate(backdated);
    EXPECT_EQ(edits.at(1)->date(), backdated.addDays(14));
}

TEST_F(test_ui_LoanDialogs, CheckoutDialogDefaultDueMatchesWhatCoreProducesForABlankInput)
{
    MemberSeed member = uniqueMemberSeed(1);
    member.status = MemberStatus::kActive;
    const qint64 memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    BookSeed book = uniqueBookSeed(1);
    const qint64 bookId = seedBook(*m_db, book);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);

    LoanCheckoutDialog dialog(*m_repository);
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 2);

    LoanInput blank;
    blank.memberId = memberId;
    blank.bookCopyId = copies.front();
    const auto created = m_repository->createLoan(blank);
    ASSERT_TRUE(created) << created.error().key;

    const auto stored = m_repository->getLoan(created.value());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(edits.at(0)->date().toString(Qt::ISODate).toStdString(), stored->borrowedAt);
    EXPECT_EQ(edits.at(1)->date().toString(Qt::ISODate).toStdString(), stored->dueAt);
}

TEST_F(test_ui_LoanDialogs, CheckoutDialogRefusesAFutureBorrowDate)
{
    LoanCheckoutDialog dialog(*m_repository);
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 2);
    EXPECT_EQ(edits.at(0)->maximumDate(), QDate::currentDate());
}

TEST_F(test_ui_LoanDialogs, ReturnDialogClampsTheReturnDateToToday)
{
    const LoanRecord loan = loanDueOn(QDate::currentDate().addDays(11).toString(Qt::ISODate));
    LoanReturnDialog dialog(loan);
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits.front()->maximumDate(), QDate::currentDate());
    EXPECT_EQ(edits.front()->date(), QDate::currentDate());
}

TEST_F(test_ui_LoanDialogs, ReturnDialogFloorsTheReturnDateAtTheBorrowDate)
{
    const QString borrowed = QDate::currentDate().addDays(-9).toString(Qt::ISODate);
    const LoanRecord loan =
        loanDueOn(QDate::currentDate().addDays(5).toString(Qt::ISODate), borrowed);
    LoanReturnDialog dialog(loan);
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits.front()->minimumDate(), QDate::fromString(borrowed, Qt::ISODate));
}

TEST_F(test_ui_LoanDialogs, ReturnDialogToleratesAnUnreadableBorrowDate)
{
    const LoanRecord loan =
        loanDueOn(QDate::currentDate().addDays(5).toString(Qt::ISODate), QStringLiteral("14/08/2020"));
    LoanReturnDialog dialog(loan);
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits.front()->maximumDate(), QDate::currentDate());
    EXPECT_LE(edits.front()->minimumDate(), QDate::currentDate());
}

TEST_F(test_ui_LoanDialogs, ExtendDialogMinimumIsCurrentDuePlusOneForACurrentLoan)
{
    const QDate currentDue = QDate::currentDate().addDays(10);
    LoanExtendDialog dialog(loanDueOn(currentDue.toString(Qt::ISODate)));
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits.front()->minimumDate(), currentDue.addDays(1));
}

TEST_F(test_ui_LoanDialogs, ExtendDialogMinimumIsTodayForAnOverdueLoan)
{
    const QDate currentDue = QDate::currentDate().addDays(-20);
    LoanExtendDialog dialog(loanDueOn(currentDue.toString(Qt::ISODate),
                                      QDate::currentDate().addDays(-34).toString(Qt::ISODate)));
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits.front()->minimumDate(), QDate::currentDate());
}

TEST_F(test_ui_LoanDialogs, ExtendDialogSuggestsFourteenDaysForACurrentLoan)
{
    const QDate currentDue = QDate::currentDate().addDays(10);
    LoanExtendDialog dialog(loanDueOn(currentDue.toString(Qt::ISODate)));
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits.front()->date(), currentDue.addDays(14));
}

TEST_F(test_ui_LoanDialogs, ExtendDialogSuggestsFourteenDaysForAnOverdueLoan)
{
    const QDate today = QDate::currentDate();
    LoanExtendDialog dialog(loanDueOn(today.addDays(-20).toString(Qt::ISODate),
                                      today.addDays(-34).toString(Qt::ISODate)));
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits.front()->date(), today.addDays(14));
}

TEST_F(test_ui_LoanDialogs, ExtendDialogToleratesAnUnreadableStoredDueDate)
{
    LoanExtendDialog dialog(loanDueOn(QStringLiteral("sometime next month")));
    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 1);
    EXPECT_EQ(edits.front()->minimumDate(), QDate::currentDate());
}

TEST_F(test_ui_LoanDialogs, ExtendDialogMinimumIsAcceptedByCore)
{
    MemberSeed member = uniqueMemberSeed(2);
    member.status = MemberStatus::kActive;
    const qint64 memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    BookSeed book = uniqueBookSeed(2);
    const qint64 bookId = seedBook(*m_db, book);
    ASSERT_GT(bookId, 0);

    const QDate today = QDate::currentDate();
    const QString currentDue = today.addDays(10).toString(Qt::ISODate);
    const qint64 loanId = rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(),
                                        today.addDays(-4).toString(Qt::ISODate).toStdString(),
                                        currentDue.toStdString());
    ASSERT_GT(loanId, 0);

    const auto stored = m_repository->getLoan(loanId);
    ASSERT_TRUE(stored.has_value());
    LoanExtendDialog dialog(*stored);

    const auto edits = dialog.findChildren<QDateEdit*>();
    ASSERT_EQ(edits.size(), 1);
    const std::string earliest = edits.front()->minimumDate().toString(Qt::ISODate).toStdString();
    EXPECT_TRUE(m_repository->extendLoan(loanId, earliest))
        << "core rejected the dialog's earliest date " << earliest;
    EXPECT_EQ(m_repository->getLoan(loanId)->dueAt, earliest);
}
