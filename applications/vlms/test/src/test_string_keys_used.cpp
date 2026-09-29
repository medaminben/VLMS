// Every translation key written in the source exists in all three tables.
// Complements test_core_Strings (tables agree with each other) by checking
// the tables against what the code actually asks for.

#include <VLMS/Core/Strings.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::set<std::string> keysOf(const char* locale)
{
    const auto keys = VLMS::Core::Strings::knownKeys(locale);
    return {keys.begin(), keys.end()};
}

std::string prefixOf(const std::string& key)
{
    return key.substr(0, key.find('.'));
}

}  // namespace

TEST(test_ui_StringKeysUsed, EveryKeyInTheSourceExistsInEveryLanguage)
{
    const std::set<std::string> ar = keysOf("ar");
    const std::set<std::string> fr = keysOf("fr");
    const std::set<std::string> en = keysOf("en");
    std::set<std::string> prefixes;
    for (const std::string& key : ar) {
        prefixes.insert(prefixOf(key));
    }

    // A quoted dotted identifier whose first segment is a translation prefix,
    // e.g. "catalog.addBook" or "error.loan.copyOnLoan". Dynamic keys built
    // from a trailing "prefix." never match, and file names are skipped.
    const std::regex literal(R"re("([a-z][A-Za-z0-9]*(?:\.[A-Za-z0-9_]+)+)")re");
    const std::regex fileName(R"re(\.(png|svg|db|qm|qrc|html|md|ico|txt|json)$)re");

    const fs::path root(VLMS_SOURCE_ROOT);
    int checked = 0;
    std::ostringstream missing;
    for (const char* dir : {"applications/vlms/src", "libraries/Core/src",
                            "libraries/Database/src", "libraries/Repositories/src"}) {
        for (const auto& entry : fs::recursive_directory_iterator(root / dir)) {
            const std::string ext = entry.path().extension().string();
            if (!entry.is_regular_file() || (ext != ".cpp" && ext != ".h")
                || entry.path().filename() == "Strings.cpp") {
                continue;
            }
            std::ifstream in(entry.path());
            const std::string text((std::istreambuf_iterator<char>(in)), {});
            for (std::sregex_iterator it(text.begin(), text.end(), literal), end; it != end; ++it) {
                const std::string key = (*it)[1];
                if (prefixes.count(prefixOf(key)) == 0 || std::regex_search(key, fileName)) {
                    continue;
                }
                ++checked;
                for (const auto& [code, table] :
                     {std::pair{"ar", &ar}, std::pair{"fr", &fr}, std::pair{"en", &en}}) {
                    if (table->count(key) == 0) {
                        missing << "\n  " << code << ": " << key << "  ("
                                << fs::relative(entry.path(), root).string() << ")";
                    }
                }
            }
        }
    }

    EXPECT_GT(checked, 300) << "the scan found too few keys; did the source layout move?";
    EXPECT_TRUE(missing.str().empty()) << "keys used but not translated:" << missing.str();
}
