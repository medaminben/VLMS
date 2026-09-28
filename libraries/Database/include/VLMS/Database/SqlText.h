#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VLMS::Database::SqlText {

[[nodiscard]] std::string escapeLike(std::string_view value);

/// Trimmed text, or nullopt when blank -- binds as SQL NULL, never as ''.
[[nodiscard]] std::optional<std::string> nullableText(std::string_view value);

[[nodiscard]] std::vector<std::string> splitStatements(std::string_view script);

}  // namespace VLMS::Database::SqlText
