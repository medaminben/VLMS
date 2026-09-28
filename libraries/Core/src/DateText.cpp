#include <VLMS/Core/DateText.h>

#include <VLMS/Core/Date.h>

#include <VLMS/Core/Text.h>

#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace VLMS::Core::DateText {

namespace {

int monthNumber(std::string word)
{
    for (char& ch : word) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    static const std::unordered_map<std::string, int> months = [] {
        const char* names[] = {"january",
                               "february",
                               "march",
                               "april",
                               "may",
                               "june",
                               "july",
                               "august",
                               "september",
                               "october",
                               "november",
                               "december"};
        std::unordered_map<std::string, int> table;
        for (int i = 0; i < 12; ++i) {
            table.emplace(names[i], i + 1);
            table.emplace(std::string(names[i], 3), i + 1);
        }
        table.emplace("sept", 9);
        return table;
    }();
    const auto it = months.find(word);
    return it == months.end() ? 0 : it->second;
}

bool isOrdinalSuffix(std::string word)
{
    for (char& ch : word) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return word == "st" || word == "nd" || word == "rd" || word == "th";
}

std::string pad2(int value)
{
    char buffer[3];
    std::snprintf(buffer, sizeof(buffer), "%02d", value);
    return buffer;
}

std::string pad4(int value)
{
    char buffer[5];
    std::snprintf(buffer, sizeof(buffer), "%04d", value);
    return buffer;
}

std::string normalizeNumericOnly(const std::string& text, const std::string& original)
{
    if (text.size() == 4 && text.find_first_not_of("0123456789") == std::string::npos) {
        return text;
    }

    int year = 0;
    int month = 0;
    if (text.size() >= 6 && text.size() <= 7 && text[4] == '-'
        && std::sscanf(text.c_str(), "%4d-%2d", &year, &month) == 2 && month >= 1 && month <= 12) {
        return pad4(year) + '-' + pad2(month);
    }
    if (text.size() >= 6 && text.size() <= 7 && text[4] == ' '
        && std::sscanf(text.c_str(), "%4d %2d", &year, &month) == 2 && month >= 1 && month <= 12) {
        return pad4(year) + '-' + pad2(month);
    }

    int day = 0;
    if (text.size() == 10 && std::sscanf(text.c_str(), "%4d-%2d-%2d", &year, &month, &day) == 3) {
        return Date(year, month, day).isValid() ? text : original;
    }

    return original;
}

}  // namespace

std::string normalizePublicationDate(std::string_view value)
{
    const std::string original(value);
    const std::string text = trim(value);
    if (text.empty()) {
        return original;
    }

    std::vector<int> numbers;
    int month = 0;
    bool sawUnknownWord = false;
    bool previousWasNumber = false;

    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);

        if (std::isdigit(c)) {
            std::size_t end = i;
            while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
                ++end;
            }
            numbers.push_back(std::stoi(text.substr(i, end - i)));
            previousWasNumber = true;
            i = end;
            continue;
        }

        if (std::isalpha(c)) {
            std::size_t end = i;
            while (end < text.size() && std::isalpha(static_cast<unsigned char>(text[end]))) {
                ++end;
            }
            const std::string word = text.substr(i, end - i);
            i = end;

            if (month == 0 && monthNumber(word) > 0) {
                month = monthNumber(word);
            } else if (!(previousWasNumber && isOrdinalSuffix(word))) {
                sawUnknownWord = true;
            }
            previousWasNumber = false;
            continue;
        }

        previousWasNumber = false;
        ++i;
    }

    if (month == 0 && !sawUnknownWord) {
        return normalizeNumericOnly(text, original);
    }
    if (month == 0 || sawUnknownWord) {
        return original;
    }

    std::vector<int> years;
    std::vector<int> days;
    for (const int number : numbers) {
        (number >= 1000 ? years : days).push_back(number);
    }
    if (years.size() != 1 || days.size() > 1) {
        return original;
    }

    const int year = years.front();
    if (days.empty()) {
        return pad4(year) + '-' + pad2(month);
    }

    const Date resolved(year, month, days.front());
    if (!resolved.isValid()) {
        return original;
    }
    return resolved.toIso();
}

}  // namespace VLMS::Core::DateText
