#include <VLMS/Repositories/LoanPolicy.h>

#include <gtest/gtest.h>

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace VLMS;

namespace {

bool containsInsensitive(std::string_view text, std::string_view needle)
{
    if (needle.size() > text.size()) {
        return false;
    }
    auto lower = [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); };
    std::string haystack(text);
    std::string find(needle);
    for (char& ch : haystack) {
        ch = lower(static_cast<unsigned char>(ch));
    }
    for (char& ch : find) {
        ch = lower(static_cast<unsigned char>(ch));
    }
    return haystack.find(find) != std::string::npos;
}

}  // namespace

TEST(test_core_LoanPolicy, DefaultLoanDaysIsFourteen)
{
    EXPECT_EQ(Repositories::LoanPolicy::defaultLoanDays(), 14);
}

TEST(test_core_LoanPolicy, SuggestedDueDateIsFourteenDaysOut)
{
    EXPECT_EQ(Repositories::LoanPolicy::suggestedDueDate(Core::Date(2021, 6, 15)), Core::Date(2021, 6, 29));
}

TEST(test_core_LoanPolicy, SuggestedDueDateCrossesMonthAndYearBoundaries)
{
    const std::vector<std::pair<Core::Date, Core::Date>> cases = {
        {Core::Date(2021, 6, 15), Core::Date(2021, 6, 29)},
        {Core::Date(2021, 6, 20), Core::Date(2021, 7, 4)},
        {Core::Date(2021, 12, 26), Core::Date(2022, 1, 9)},
        {Core::Date(2024, 2, 20), Core::Date(2024, 3, 5)},
        {Core::Date(2023, 2, 20), Core::Date(2023, 3, 6)},
    };
    for (const auto& [borrowedOn, expected] : cases) {
        SCOPED_TRACE(borrowedOn.toIso());
        EXPECT_EQ(Repositories::LoanPolicy::suggestedDueDate(borrowedOn), expected);
    }
}

TEST(test_core_LoanPolicy, SuggestedDueDateOfAnInvalidDateIsInvalid)
{
    EXPECT_FALSE(Repositories::LoanPolicy::suggestedDueDate(Core::Date()).isValid());
    EXPECT_FALSE(Repositories::LoanPolicy::suggestedDueDate(Core::Date::fromIso("2024-02-30")).isValid());
}

TEST(test_core_LoanPolicy, MinimumExtensionIsTheDayAfterACurrentDueDate)
{
    const Core::Date today(2021, 6, 15);
    EXPECT_EQ(Repositories::LoanPolicy::minimumExtensionDate(Core::Date(2021, 6, 25), today), Core::Date(2021, 6, 26));
}

TEST(test_core_LoanPolicy, MinimumExtensionIsTodayForAnOverdueLoan)
{
    const Core::Date today(2021, 6, 15);
    EXPECT_EQ(Repositories::LoanPolicy::minimumExtensionDate(Core::Date(2021, 5, 26), today), today);
}

TEST(test_core_LoanPolicy, MinimumExtensionIsTodayWhenTheStoredDueDateIsUnreadable)
{
    const Core::Date today(2021, 6, 15);
    EXPECT_EQ(Repositories::LoanPolicy::minimumExtensionDate(Core::Date(), today), today);
}

TEST(test_core_LoanPolicy, MinimumExtensionIsTomorrowWhenTheLoanIsDueToday)
{
    const Core::Date today(2021, 6, 15);
    EXPECT_EQ(Repositories::LoanPolicy::minimumExtensionDate(today, today), Core::Date(2021, 6, 16));
}

TEST(test_core_LoanPolicy, MinimumExtensionIsNeverInThePast)
{
    const Core::Date today(2021, 6, 15);
    for (const Core::Date currentDue :
         {Core::Date(2019, 1, 1), Core::Date(2021, 6, 14), Core::Date(2021, 6, 15), Core::Date(2021, 6, 16),
          Core::Date(2025, 1, 1), Core::Date()}) {
        SCOPED_TRACE(currentDue.toIso());
        EXPECT_GE(Repositories::LoanPolicy::minimumExtensionDate(currentDue, today), today);
    }
}

TEST(test_core_LoanPolicy, ExtensionSuggestsFourteenDaysForACurrentLoan)
{
    const Core::Date today(2021, 6, 15);
    EXPECT_EQ(Repositories::LoanPolicy::suggestedExtensionDate(Core::Date(2021, 6, 25), today), Core::Date(2021, 7, 9));
}

TEST(test_core_LoanPolicy, ExtensionSuggestsFourteenDaysForAnOverdueLoan)
{
    const Core::Date today(2021, 6, 15);
    EXPECT_EQ(Repositories::LoanPolicy::suggestedExtensionDate(Core::Date(2021, 5, 26), today), Core::Date(2021, 6, 29));
}

TEST(test_core_LoanPolicy, ExtensionSuggestsFourteenDaysWhateverTheState)
{
    const Core::Date today(2021, 6, 15);
    for (const Core::Date currentDue :
         {Core::Date(2021, 5, 26), Core::Date(2021, 6, 14), Core::Date(2021, 6, 15), Core::Date(2021, 6, 16),
          Core::Date(2021, 7, 15), Core::Date()}) {
        SCOPED_TRACE(currentDue.toIso());
        const Core::Date effectiveFrom =
            (currentDue.isValid() && currentDue > today) ? currentDue : today;
        EXPECT_EQ(Repositories::LoanPolicy::suggestedExtensionDate(currentDue, today), effectiveFrom.addDays(14));
    }
}

TEST(test_core_LoanPolicy, TheSuggestionIsAlwaysAtLeastTheMinimum)
{
    const Core::Date today(2021, 6, 15);
    for (const Core::Date currentDue :
         {Core::Date(2021, 5, 26), Core::Date(2021, 6, 14), Core::Date(2021, 6, 15), Core::Date(2021, 6, 16),
          Core::Date(2021, 7, 15), Core::Date()}) {
        SCOPED_TRACE(currentDue.toIso());
        EXPECT_GE(Repositories::LoanPolicy::suggestedExtensionDate(currentDue, today),
                  Repositories::LoanPolicy::minimumExtensionDate(currentDue, today));
    }
}

TEST(test_core_LoanPolicy, ParseIsoDateAcceptsOnlyFullIsoDates)
{
    const std::vector<std::pair<std::string, Core::Date>> cases = {
        {"2021-06-15", Core::Date(2021, 6, 15)},
        {"  2021-06-15 ", Core::Date(2021, 6, 15)},
        {"2024-02-29", Core::Date(2024, 2, 29)},
        {"0001-01-01", Core::Date(1, 1, 1)},
        {"9999-12-31", Core::Date(9999, 12, 31)},
        {"", Core::Date()},
        {"   ", Core::Date()},
        {"14/08/2020", Core::Date()},
        {"28 August 2026", Core::Date()},
        {"2024-1-5", Core::Date()},
        {"2024-13-01", Core::Date()},
        {"2024-01-32", Core::Date()},
        {"2024-02-30", Core::Date()},
        {"1900-02-29", Core::Date()},
        {"2021-06-15T00:00:00", Core::Date()},
        {"sometime next month", Core::Date()},
    };
    for (const auto& [text, expected] : cases) {
        SCOPED_TRACE(text);
        const Core::Date actual = Repositories::LoanPolicy::parseIsoDate(text);
        if (expected.isValid()) {
            EXPECT_EQ(actual, expected);
        } else {
            EXPECT_FALSE(actual.isValid()) << actual.toIso();
        }
    }
}

TEST(test_core_LoanPolicy, ParseIsoDateRejectsTheShapesSqliteMisreads)
{
    for (const char* text :
         {"2014", " 2014 ", "1999-09", "2024-02-30", "1900-02-29", "0", "2451545"}) {
        SCOPED_TRACE(text);
        EXPECT_FALSE(Repositories::LoanPolicy::parseIsoDate(text).isValid()) << text;
    }
}

TEST(test_core_LoanPolicy, LoanDatesRejectAnUnparseableBorrowDateAsSuch)
{
    const auto result = Repositories::LoanPolicy::validateLoanDates("14/08/2026", "2026-08-28", Core::Date(2026, 8, 15));
    EXPECT_FALSE(result);
    EXPECT_TRUE(containsInsensitive(result.message, "Borrow")) << result.message;
}

TEST(test_core_LoanPolicy, LoanDatesRejectAnUnparseableDueDateWithoutMentioningOrdering)
{
    const auto result =
        Repositories::LoanPolicy::validateLoanDates("2026-08-14", "28 August 2026", Core::Date(2026, 8, 15));
    EXPECT_FALSE(result);
    EXPECT_FALSE(containsInsensitive(result.message, "before")) << result.message;
}

TEST(test_core_LoanPolicy, LoanDatesRejectAZeroDayLoan)
{
    const auto result = Repositories::LoanPolicy::validateLoanDates("2026-03-10", "2026-03-10", Core::Date(2026, 8, 15));
    EXPECT_FALSE(result);
    EXPECT_FALSE(result.message.empty());
}

TEST(test_core_LoanPolicy, LoanDatesRejectAFutureBorrowDate)
{
    const auto result = Repositories::LoanPolicy::validateLoanDates("2026-09-14", "2026-09-28", Core::Date(2026, 8, 15));
    EXPECT_FALSE(result);
    EXPECT_FALSE(result.message.empty());
}

TEST(test_core_LoanPolicy, LoanDatesAcceptAOneDayLoan)
{
    EXPECT_TRUE(Repositories::LoanPolicy::validateLoanDates("2026-08-14", "2026-08-15", Core::Date(2026, 8, 15)));
}

TEST(test_core_LoanPolicy, LoanDatesAcceptABackdatedCheckout)
{
    EXPECT_TRUE(Repositories::LoanPolicy::validateLoanDates("2026-07-01", "2026-07-15", Core::Date(2026, 8, 15)));
}

TEST(test_core_LoanPolicy, ReturnDateRejectsAnUnreadableStoredBorrowDate)
{
    const auto result = Repositories::LoanPolicy::validateReturnDate("2026-08-15", "14/08/2026", Core::Date(2026, 8, 15));
    EXPECT_FALSE(result);
    EXPECT_FALSE(result.message.empty());
}

TEST(test_core_LoanPolicy, ReturnDateAcceptsTheBorrowDateItself)
{
    EXPECT_TRUE(Repositories::LoanPolicy::validateReturnDate("2026-08-15", "2026-08-15", Core::Date(2026, 8, 15)));
}

TEST(test_core_LoanPolicy, ReturnDateRejectsTomorrow)
{
    EXPECT_FALSE(Repositories::LoanPolicy::validateReturnDate("2026-08-16", "2026-08-01", Core::Date(2026, 8, 15)));
}

TEST(test_core_LoanPolicy, ExtensionRejectsAnUnreadableStoredDueDate)
{
    const auto result =
        Repositories::LoanPolicy::validateExtension("2026-09-14", "sometime next month", Core::Date(2026, 8, 15));
    EXPECT_FALSE(result);
    EXPECT_FALSE(result.message.empty());
}

TEST(test_core_LoanPolicy, ExtensionRejectsGoingBackwardsFromAReadableDueDate)
{
    const Core::Date today(2026, 8, 15);
    EXPECT_FALSE(Repositories::LoanPolicy::validateExtension("2026-08-20", "2026-08-25", today));
    EXPECT_TRUE(Repositories::LoanPolicy::validateExtension("2026-08-26", "2026-08-25", today));
}

TEST(test_core_LoanPolicy, EveryRejectionCarriesAMessage)
{
    const Core::Date today(2026, 8, 15);
    const std::vector<Repositories::LoanPolicy::Validation> results = {
        Repositories::LoanPolicy::validateLoanDates("nope", "2026-08-28", today),
        Repositories::LoanPolicy::validateLoanDates("2026-08-14", "nope", today),
        Repositories::LoanPolicy::validateLoanDates("2026-08-14", "2026-08-14", today),
        Repositories::LoanPolicy::validateLoanDates("2026-08-14", "2026-08-13", today),
        Repositories::LoanPolicy::validateLoanDates("2026-08-16", "2026-08-30", today),
        Repositories::LoanPolicy::validateReturnDate("nope", "2026-08-01", today),
        Repositories::LoanPolicy::validateReturnDate("2026-08-15", "nope", today),
        Repositories::LoanPolicy::validateReturnDate("2026-07-31", "2026-08-01", today),
        Repositories::LoanPolicy::validateReturnDate("2026-08-16", "2026-08-01", today),
        Repositories::LoanPolicy::validateExtension("nope", "2026-08-25", today),
        Repositories::LoanPolicy::validateExtension("2026-09-14", "nope", today),
        Repositories::LoanPolicy::validateExtension("2026-08-25", "2026-08-25", today),
        Repositories::LoanPolicy::validateExtension("2026-08-10", "2026-08-01", today),
    };
    for (const auto& result : results) {
        EXPECT_FALSE(result);
        EXPECT_FALSE(result.message.empty());
        EXPECT_NE(result.message.find_first_not_of(" \t\n"), std::string::npos);
    }
}
