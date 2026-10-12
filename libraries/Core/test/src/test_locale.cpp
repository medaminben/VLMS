#include <VLMS/Core/Locale.h>

#include <gtest/gtest.h>

using namespace VLMS;

class test_core_Locale : public ::testing::Test {
protected:
    void TearDown() override { Core::Locale::setCode(Core::Locale::kDefaultCode); }
};

TEST_F(test_core_Locale, ArabicIsTheDefaultAndRightToLeft)
{
    Core::Locale::setCode(Core::Locale::kDefaultCode);
    EXPECT_EQ(Core::Locale::code(), "ar");
    EXPECT_TRUE(Core::Locale::isRtl());
}

TEST_F(test_core_Locale, FrenchAndEnglishAreLeftToRight)
{
    Core::Locale::setCode("fr");
    EXPECT_EQ(Core::Locale::code(), "fr");
    EXPECT_FALSE(Core::Locale::isRtl());

    Core::Locale::setCode("en");
    EXPECT_EQ(Core::Locale::code(), "en");
    EXPECT_FALSE(Core::Locale::isRtl());
}

TEST_F(test_core_Locale, AnUnknownCodeFallsBackToArabic)
{
    Core::Locale::setCode("en");
    Core::Locale::setCode("de");
    EXPECT_EQ(Core::Locale::code(), "ar");

    Core::Locale::setCode("");
    EXPECT_EQ(Core::Locale::code(), "ar");
}

TEST_F(test_core_Locale, CodesAreCaseSensitive)
{
    Core::Locale::setCode("EN");
    EXPECT_EQ(Core::Locale::code(), "ar");
}
