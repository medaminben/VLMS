#include <VLMS/Core/Date.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace VLMS;

using VLMS::Date;

/**
 * The one-year rule on its own, with no database: which day a member's
 * year ends, and what a librarian's pick in the editor does to it.
 */
class test_core_MemberStatusRule : public ::testing::Test {
protected:
    const Date m_today{2026, 9, 23};
};

TEST_F(test_core_MemberStatusRule, StatusCodesAreActiveAndNotActiveOnly)
{
    const std::vector<std::string> expected = {Repositories::MemberStatus::kActive, Repositories::MemberStatus::kNonActive};
    EXPECT_EQ(Repositories::MemberRepository::statusCodes(), expected);
}

TEST_F(test_core_MemberStatusRule, AddYearsKeepsTheDayOfTheMonth)
{
    EXPECT_EQ(Date(2025, 9, 24).addYears(1), Date(2026, 9, 24));
}

TEST_F(test_core_MemberStatusRule, AddYearsRollsALeapDayToTheFirstOfMarch)
{
    // SQLite's date('2024-02-29', '+1 year') is 2025-03-01; the migration
    // uses SQLite and the repository uses Date, so they must agree.
    EXPECT_EQ(Date(2024, 2, 29).addYears(1), Date(2025, 3, 1));
}

TEST_F(test_core_MemberStatusRule, AddYearsOnAnInvalidDateStaysInvalid)
{
    EXPECT_FALSE(Date().addYears(1).isValid());
}

TEST_F(test_core_MemberStatusRule, ActiveOnTheLastDayNotActiveTheDayAfter)
{
    EXPECT_EQ(Repositories::MemberRepository::statusOn("2026-09-23", m_today), Repositories::MemberStatus::kActive);
    EXPECT_EQ(Repositories::MemberRepository::statusOn("2026-09-22", m_today), Repositories::MemberStatus::kNonActive);
}

TEST_F(test_core_MemberStatusRule, NoDateOrAnUnreadableDateIsNotActive)
{
    EXPECT_EQ(Repositories::MemberRepository::statusOn("", m_today), Repositories::MemberStatus::kNonActive);
    EXPECT_EQ(Repositories::MemberRepository::statusOn("next year", m_today), Repositories::MemberStatus::kNonActive);
}

TEST_F(test_core_MemberStatusRule, ANewMemberIsActiveForAYearLessADay)
{
    EXPECT_EQ(Repositories::MemberRepository::activeUntilFor("", Repositories::MemberStatus::kActive, m_today), "2027-09-22");
}

TEST_F(test_core_MemberStatusRule, ANewMemberRegisteredOnALeapDayEndsOnTheLastDayOfFebruary)
{
    EXPECT_EQ(Repositories::MemberRepository::activeUntilFor("", Repositories::MemberStatus::kActive, Date(2024, 2, 29)),
              "2025-02-28");
}

TEST_F(test_core_MemberStatusRule, ANewMemberSavedNotActiveEndedYesterday)
{
    EXPECT_EQ(Repositories::MemberRepository::activeUntilFor("", Repositories::MemberStatus::kNonActive, m_today),
              "2026-09-22");
}

TEST_F(test_core_MemberStatusRule, RenewingAnExpiredMemberGivesAYearFromToday)
{
    EXPECT_EQ(Repositories::MemberRepository::activeUntilFor("2026-01-09", Repositories::MemberStatus::kActive, m_today),
              "2027-09-22");
}

TEST_F(test_core_MemberStatusRule, EndingAMembershipEarlySetsYesterday)
{
    EXPECT_EQ(Repositories::MemberRepository::activeUntilFor("2027-01-01", Repositories::MemberStatus::kNonActive, m_today),
              "2026-09-22");
}

TEST_F(test_core_MemberStatusRule, KeepingTheStatusKeepsTheDate)
{
    EXPECT_EQ(Repositories::MemberRepository::activeUntilFor("2027-01-01", Repositories::MemberStatus::kActive, m_today),
              "2027-01-01");
    EXPECT_EQ(Repositories::MemberRepository::activeUntilFor("2026-01-09", Repositories::MemberStatus::kNonActive, m_today),
              "2026-01-09");
}

TEST_F(test_core_MemberStatusRule, AnEmptyChoiceMeansActive)
{
    EXPECT_EQ(Repositories::MemberRepository::activeUntilFor("", "", m_today), "2027-09-22");
}
