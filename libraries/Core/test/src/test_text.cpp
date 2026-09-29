#include <VLMS/Core/Text.h>

#include <gtest/gtest.h>

#include <string>

using namespace VLMS;

TEST(test_core_Text, TrimRemovesSpacesTabsAndNewlinesAtBothEnds)
{
    EXPECT_EQ(Core::trim("  a b \t\r\n"), "a b");
    EXPECT_EQ(Core::trim("\n\tx"), "x");
    EXPECT_EQ(Core::trim("x"), "x");
}

TEST(test_core_Text, TrimOfBlankTextIsEmpty)
{
    EXPECT_EQ(Core::trim(""), "");
    EXPECT_EQ(Core::trim(" \t\r\n "), "");
}

TEST(test_core_Text, TrimKeepsArabicText)
{
    EXPECT_EQ(Core::trim("  مكتبة  "), "مكتبة");
}

TEST(test_core_Text, ReplaceAllReplacesEveryOccurrence)
{
    std::string text = "{n} of {n}";
    Core::replaceAll(text, "{n}", "3");
    EXPECT_EQ(text, "3 of 3");
}

TEST(test_core_Text, ReplaceAllDoesNotRescanTheReplacement)
{
    std::string text = "aa";
    Core::replaceAll(text, "a", "aa");
    EXPECT_EQ(text, "aaaa");
}

TEST(test_core_Text, ReplaceAllWithAnEmptyPatternChangesNothing)
{
    std::string text = "abc";
    Core::replaceAll(text, "", "x");
    EXPECT_EQ(text, "abc");
}

TEST(test_core_Text, ReplaceAllCanDelete)
{
    std::string text = "a-b-c";
    Core::replaceAll(text, "-", "");
    EXPECT_EQ(text, "abc");
}
