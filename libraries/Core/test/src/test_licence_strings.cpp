#include <VLMS/Core/Strings.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using VLMS::Strings;

namespace {

constexpr auto kArabic = "ar";
constexpr auto kFrench = "fr";
constexpr auto kEnglish = "en";

/// Every key the licence dialog composes. Listed here rather than derived,
/// because the parity test only proves the three tables agree with each
/// other -- it would stay green if all three lost the licence together.
const std::vector<std::string>& licenceKeys()
{
    static const std::vector<std::string> keys = {
        "licence.title",
        "licence.intro",
        "licence.clause.ownership.head",
        "licence.clause.ownership.body",
        "licence.clause.data.head",
        "licence.clause.data.body",
        "licence.clause.warranty.head",
        "licence.clause.warranty.body",
        "licence.clause.liability.head",
        "licence.clause.liability.body",
        "licence.clause.support.head",
        "licence.clause.support.body",
        "licence.contact.head",
        "licence.contact.author",
        "licence.tooltip",
    };
    return keys;
}

}  // namespace

TEST(test_core_LicenceStrings, EveryClauseIsWrittenInEveryLanguage)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        for (const std::string& key : licenceKeys()) {
            SCOPED_TRACE(std::string(locale) + " " + key);
            EXPECT_FALSE(Strings::rawValue(locale, key).empty());
        }
    }
}

// An untranslated clause left as English in the Arabic table is the realistic
// failure. Arabic script against Latin makes that a reliable check. French and
// English are deliberately not compared: "Licence" is legitimately identical
// in both.
TEST(test_core_LicenceStrings, TheArabicIsNotTheEnglish)
{
    for (const std::string& key : licenceKeys()) {
        SCOPED_TRACE(key);
        EXPECT_NE(Strings::rawValue(kArabic, key), Strings::rawValue(kEnglish, key));
    }
}

TEST(test_core_LicenceStrings, TheLicenceNamesTheAuthorOnlyInTheContactBlock)
{
    const std::vector<std::string> bodyKeys = {
        "licence.intro",
        "licence.clause.ownership.body",
        "licence.clause.data.body",
        "licence.clause.warranty.body",
        "licence.clause.liability.body",
        "licence.clause.support.body",
    };
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        for (const std::string& key : bodyKeys) {
            SCOPED_TRACE(std::string(locale) + " " + key);
            EXPECT_EQ(Strings::rawValue(locale, key).find("Ben Hassine"), std::string::npos);
        }
    }
    EXPECT_NE(Strings::rawValue(kEnglish, "licence.contact.author").find("Ben Hassine"),
              std::string::npos);
}
