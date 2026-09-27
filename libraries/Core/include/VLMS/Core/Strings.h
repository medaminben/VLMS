#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace VLMS {

class Strings {
public:
    static std::string t(std::string_view key,
                         std::string_view param = {},
                         std::string_view value = {});

    static std::string t2(std::string_view key,
                          std::string_view p1,
                          std::string_view v1,
                          std::string_view p2,
                          std::string_view v2);

    static std::string bookLanguageLabel(std::string_view code);
    static std::string memberStatusLabel(std::string_view code);
    static std::string memberSexLabel(std::string_view code);
    static std::string memberAgeGroupLabel(std::string_view code);

    [[nodiscard]] static std::vector<std::string> knownKeys(std::string_view localeCode);
    [[nodiscard]] static std::string rawValue(std::string_view localeCode, std::string_view key);

private:
    static std::string lookup(std::string_view key);
};

}  // namespace VLMS
