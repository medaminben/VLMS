#pragma once

#include <string>
#include <string_view>

namespace VLMS::Core::DateText {

/**
 * Normalises a bibliographic publication date to reduced-precision ISO 8601.
 *
 * Three legal outputs: `YYYY`, `YYYY-MM`, `YYYY-MM-DD`. Reduced precision is
 * the point. Anything not understood is returned unchanged, never guessed at.
 */
[[nodiscard]] std::string normalizePublicationDate(std::string_view value);

}  // namespace VLMS::Core::DateText
