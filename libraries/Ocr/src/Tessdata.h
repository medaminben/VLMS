#pragma once

// Locating the traineddata, without Qt.
//
// This is the part that used QCoreApplication::applicationDirPath() and QDir
// before the OCR module was cut loose from Qt. Everything here is
// std::filesystem plus one platform call for "where is my own executable",
// which is the single thing the standard library still does not provide.

#include <filesystem>
#include <string>
#include <vector>

namespace VLMS::Ocr::Detail {

/// Directory containing the running executable, or an empty path when the
/// platform would not say. Callers treat an empty path as "skip that root"
/// rather than an error: a developer build still finds tessdata by other means.
[[nodiscard]] std::filesystem::path executableDirectory();

/// Recognition languages with a *.traineddata file directly in `directory`,
/// sorted and without duplicates. "osd" is filtered out: it holds orientation
/// and script-detection data, not a recognition model, so offering it as a
/// language would produce a run that can never return text.
///
/// Returns empty for a directory that does not exist or cannot be read; a
/// missing tessdata directory is an ordinary outcome here, not an exception.
[[nodiscard]] std::vector<std::string> languagesInDirectory(
    const std::filesystem::path& directory);

/// First directory in the search order that holds at least one recognition
/// language, as a UTF-8 string, or "" when there is none.
///
/// Order, unchanged from the Qt implementation: the application directory,
/// then <app>/ocr and <app>/../ocr, then the build-time VLMS_OCR_ROOT,
/// then -- on non-Windows only -- the distribution's own tessdata. Windows
/// customers deliberately never fall back to a machine-wide Tesseract install:
/// the app folder is self-contained, and silently picking up someone else's
/// tessdata would make a support call unanswerable.
[[nodiscard]] std::string resolveTessdataPrefix();

}  // namespace VLMS::Ocr::Detail
