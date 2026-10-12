// Placeholder filling in Strings::t and t2. Table parity, blank values,
// fallback and the label helpers live in Repositories' test_strings_parity.

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>

#include <gtest/gtest.h>

#include <string>

using namespace VLMS;

class test_core_Strings : public ::testing::Test {
protected:
    void SetUp() override { Core::Locale::setCode("en"); }
    void TearDown() override { Core::Locale::setCode(Core::Locale::kDefaultCode); }
};

TEST_F(test_core_Strings, TFillsItsPlaceholder)
{
    const std::string text = Core::Strings::t("archive.reuse.prompt", "number", "42");
    EXPECT_NE(text.find("42"), std::string::npos) << text;
    EXPECT_EQ(text.find("{number}"), std::string::npos) << text;
}

TEST_F(test_core_Strings, TWithoutAParameterLeavesThePlaceholder)
{
    EXPECT_EQ(Core::Strings::t("archive.reuse.prompt"),
              Core::Strings::rawValue("en", "archive.reuse.prompt"));
}

TEST_F(test_core_Strings, T2FillsBothPlaceholders)
{
    const std::string text = Core::Strings::t2("bulk.confirm", "count", "3", "noun", "books");
    EXPECT_NE(text.find('3'), std::string::npos) << text;
    EXPECT_NE(text.find("books"), std::string::npos) << text;
    EXPECT_EQ(text.find("{count}"), std::string::npos) << text;
    EXPECT_EQ(text.find("{noun}"), std::string::npos) << text;
}

TEST_F(test_core_Strings, TheCurrentLanguageDecides)
{
    const std::string english = Core::Strings::t("nav.help");
    Core::Locale::setCode("fr");
    EXPECT_EQ(Core::Strings::t("nav.help"), Core::Strings::rawValue("fr", "nav.help"));
    EXPECT_NE(Core::Strings::t("nav.help"), english);
}
