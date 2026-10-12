#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VLMS::Database::SqlText {

[[nodiscard]] std::string escapeLike(std::string_view value);
/// The LIKE pattern for a search box term, trimmed and escaped. Plain text
/// matches anywhere ("%1%"); text in double quotes matches the whole value,
/// like a web search ("\"1\"" matches 1 but not 10).
[[nodiscard]] std::string likePattern(std::string_view search);
/// True when the trimmed term is wrapped in double quotes.
[[nodiscard]] bool isQuoted(std::string_view search);
/// The term without its surrounding quotes, trimmed.
[[nodiscard]] std::string unquoted(std::string_view search);

/// Trimmed text, or nullopt when blank -- binds as SQL NULL, never as ''.
[[nodiscard]] std::optional<std::string> nullableText(std::string_view value);

[[nodiscard]] std::vector<std::string> splitStatements(std::string_view script);

}  // namespace VLMS::Database::SqlText
