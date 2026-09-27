#include <VLMS/Core/SqlText.h>

#include "Text.h"

#include <cctype>

namespace VLMS::SqlText {

namespace {

bool opensATrigger(std::string_view text)
{
    std::size_t i = 0;
    auto skipSpace = [&] {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
    };
    auto takeWord = [&]() -> std::string {
        skipSpace();
        std::string word;
        while (i < text.size()) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (!std::isalnum(c) && c != '_') {
                break;
            }
            word.push_back(static_cast<char>(std::toupper(c)));
            ++i;
        }
        return word;
    };

    if (takeWord() != "CREATE") {
        return false;
    }
    std::string next = takeWord();
    if (next == "OR") {
        if (takeWord() != "REPLACE") {
            return false;
        }
        next = takeWord();
    }
    if (next == "TEMP" || next == "TEMPORARY") {
        next = takeWord();
    }
    return next == "TRIGGER";
}

}  // namespace

std::string escapeLike(std::string_view value)
{
    std::string escaped(value);
    replaceAll(escaped, "\\", "\\\\");
    replaceAll(escaped, "%", "\\%");
    replaceAll(escaped, "_", "\\_");
    return escaped;
}

std::optional<std::string> nullableText(std::string_view value)
{
    std::string trimmed = trim(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    return trimmed;
}

std::vector<std::string> splitStatements(std::string_view script)
{
    std::vector<std::string> statements;
    std::string current;
    int blockDepth = 0;

    const auto flush = [&] {
        const std::string trimmed = trim(current);
        if (!trimmed.empty()) {
            statements.push_back(trimmed);
        }
        current.clear();
        blockDepth = 0;
    };

    const std::size_t size = script.size();
    std::size_t i = 0;
    while (i < size) {
        const char c = script[i];

        if (c == '-' && i + 1 < size && script[i + 1] == '-') {
            while (i < size && script[i] != '\n') {
                ++i;
            }
            continue;
        }

        if (c == '/' && i + 1 < size && script[i + 1] == '*') {
            i += 2;
            while (i + 1 < size && !(script[i] == '*' && script[i + 1] == '/')) {
                ++i;
            }
            i = std::min(i + 2, size);
            current += ' ';
            continue;
        }

        if (c == '\'' || c == '"') {
            const char quote = c;
            current += c;
            ++i;
            while (i < size) {
                const char inner = script[i];
                if (inner == quote) {
                    if (i + 1 < size && script[i + 1] == quote) {
                        current += quote;
                        current += quote;
                        i += 2;
                        continue;
                    }
                    current += quote;
                    ++i;
                    break;
                }
                current += inner;
                ++i;
            }
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t end = i;
            while (end < size) {
                const unsigned char ch = static_cast<unsigned char>(script[end]);
                if (!std::isalnum(ch) && script[end] != '_') {
                    break;
                }
                ++end;
            }
            const std::string word(script.substr(i, end - i));
            current += word;
            i = end;

            std::string keyword = word;
            for (char& ch : keyword) {
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            }
            if (blockDepth > 0) {
                if (keyword == "BEGIN" || keyword == "CASE") {
                    ++blockDepth;
                } else if (keyword == "END") {
                    --blockDepth;
                }
            } else if (keyword == "BEGIN" && opensATrigger(current)) {
                blockDepth = 1;
            }
            continue;
        }

        if (c == ';' && blockDepth == 0) {
            flush();
            ++i;
            continue;
        }

        current += c;
        ++i;
    }

    flush();
    return statements;
}

}  // namespace VLMS::SqlText
