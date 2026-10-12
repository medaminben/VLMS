#include <VLMS/Core/Strings.h>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace VLMS;

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
        "licence.clause.data.head",
        "licence.clause.data.body",
        "licence.clause.warranty.head",
        "licence.clause.warranty.body",
        "licence.clause.liability.head",
        "licence.clause.liability.body",
        "licence.clause.support.head",
        "licence.clause.support.body",
        "licence.thirdParty.head",
        "licence.thirdParty.body",
        "licence.fullText.head",
        "licence.fullText.note",
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
            EXPECT_FALSE(Core::Strings::rawValue(locale, key).empty());
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
        EXPECT_NE(Core::Strings::rawValue(kArabic, key), Core::Strings::rawValue(kEnglish, key));
    }
}

TEST(test_core_LicenceStrings, TheLicenceNamesTheAuthorOnlyInTheContactBlock)
{
    const std::vector<std::string> bodyKeys = {
        "licence.intro",
        "licence.clause.data.body",
        "licence.clause.warranty.body",
        "licence.clause.liability.body",
        "licence.clause.support.body",
        "licence.thirdParty.body",
        "licence.fullText.note",
    };
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        for (const std::string& key : bodyKeys) {
            SCOPED_TRACE(std::string(locale) + " " + key);
            EXPECT_EQ(Core::Strings::rawValue(locale, key).find("Ben Hassine"), std::string::npos);
        }
    }
    EXPECT_NE(Core::Strings::rawValue(kEnglish, "licence.contact.author").find("Ben Hassine"),
              std::string::npos);
}

// The old gift wording said the application belonged to the Library. Under the
// MIT licence the author keeps the copyright and the Library gets the
// permissions; a clause saying otherwise contradicted the LICENSE file.
TEST(test_core_LicenceStrings, NoClauseClaimsTheApplicationForTheLibrary)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        EXPECT_TRUE(Core::Strings::rawValue(locale, "licence.clause.ownership.head").empty());
        EXPECT_TRUE(Core::Strings::rawValue(locale, "licence.clause.ownership.body").empty());
    }
}

TEST(test_core_LicenceStrings, TheIntroNamesTheMitLicenceInEveryLanguage)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);
        EXPECT_NE(Core::Strings::rawValue(locale, "licence.intro").find("MIT"), std::string::npos);
    }
}
