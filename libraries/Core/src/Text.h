#pragma once

#include <string>
#include <string_view>

namespace VLMS {

[[nodiscard]] inline std::string trim(std::string_view text)
{
    const auto begin = text.find_first_not_of(" \t\n\r");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\n\r");
    return std::string(text.substr(begin, end - begin + 1));
}

inline void replaceAll(std::string& text, std::string_view from, std::string_view to)
{
    if (from.empty()) {
        return;
    }
    std::string::size_type pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

}  // namespace VLMS
