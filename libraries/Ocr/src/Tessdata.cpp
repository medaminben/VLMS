#include "Tessdata.h"

#include <algorithm>
#include <system_error>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#elif defined(__linux__)
#  include <unistd.h>
#endif

namespace VLMS::Ocr::Detail {
namespace {

namespace fs = std::filesystem;

constexpr const char* kTraineddataExtension = ".traineddata";

// Orientation and script detection, not a recognition model. Init() accepts
// it and GetUTF8Text() then returns nothing, so it must never reach a language
// menu.
constexpr const char* kOrientationModel = "osd";

fs::path executablePath()
{
#if defined(_WIN32)
    // GetModuleFileNameW truncates instead of telling you the length it wanted,
    // so grow until the result fits with room to spare.
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written =
            ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size()) {
            buffer.resize(written);
            return fs::path(buffer);
        }
        if (buffer.size() > 32768) {  // Windows' own path ceiling; stop rather than spin.
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__linux__)
    std::string buffer(1024, '\0');
    for (;;) {
        const ssize_t written = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (written < 0) {
            return {};
        }
        if (static_cast<size_t>(written) < buffer.size()) {
            buffer.resize(static_cast<size_t>(written));
            return fs::path(buffer);
        }
        if (buffer.size() > 32768) {
            return {};
        }
        buffer.resize(buffer.size() * 2);
    }
#else
    return {};
#endif
}

// Every filesystem query here is the noexcept overload: a tessdata directory
// that is missing, unreadable, or on a disconnected network drive is a normal
// state for this code, and answering "no languages" is the correct response to
// all three.
bool isDirectory(const fs::path& path)
{
    std::error_code ec;
    return fs::is_directory(path, ec);
}

std::vector<std::string> candidateRoots()
{
    std::vector<std::string> roots;

    const fs::path appDir = executableDirectory();
    if (!appDir.empty()) {
        roots.push_back(appDir.string());
        roots.push_back((appDir / "ocr").string());
        roots.push_back((appDir.parent_path() / "ocr").string());
    }

#ifdef VLMS_OCR_ROOT
    roots.emplace_back(VLMS_OCR_ROOT);
#endif

    return roots;
}

}  // namespace

fs::path executableDirectory()
{
    const fs::path exe = executablePath();
    if (exe.empty()) {
        return {};
    }

    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(exe, ec);
    return (ec ? exe : resolved).parent_path();
}

std::vector<std::string> languagesInDirectory(const fs::path& directory)
{
    std::vector<std::string> languages;
    if (directory.empty() || !isDirectory(directory)) {
        return languages;
    }

    std::error_code ec;
    fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        return languages;
    }

    for (const fs::directory_entry& entry : it) {
        if (entry.path().extension() != kTraineddataExtension) {
            continue;
        }
        std::string code = entry.path().stem().string();
        if (code.empty() || code == kOrientationModel) {
            continue;
        }
        languages.push_back(std::move(code));
    }

    std::sort(languages.begin(), languages.end());
    languages.erase(std::unique(languages.begin(), languages.end()), languages.end());
    return languages;
}

std::string resolveTessdataPrefix()
{
    // Tesseract's Init() wants the directory that *contains* the .traineddata
    // files, so each root is probed three ways: as that directory itself, and
    // through the two layouts the bundles actually use.
    const auto prefixWithin = [](const fs::path& root) -> std::string {
        if (root.empty()) {
            return {};
        }
        const fs::path layouts[] = {root, root / "tessdata", root / "share" / "tessdata"};
        for (const fs::path& layout : layouts) {
            if (!languagesInDirectory(layout).empty()) {
                std::error_code ec;
                const fs::path resolved = fs::weakly_canonical(layout, ec);
                return (ec ? layout : resolved).string();
            }
        }
        return {};
    };

    for (const std::string& root : candidateRoots()) {
        std::string prefix = prefixWithin(fs::path(root));
        if (!prefix.empty()) {
            return prefix;
        }
    }

#ifndef _WIN32
    // Linux developers only. A Windows customer folder is self-contained by
    // design, and quietly borrowing a system-wide Tesseract there would mean
    // the app's behaviour depends on software nobody knows is installed.
    const char* systemDirs[] = {
        "/usr/share/tesseract-ocr/5/tessdata",
        "/usr/share/tesseract-ocr/4.00/tessdata",
        "/usr/share/tesseract-ocr/tessdata",
        "/usr/share/tessdata",
    };
    for (const char* dir : systemDirs) {
        if (!languagesInDirectory(fs::path(dir)).empty()) {
            return dir;
        }
    }
#endif

    return {};
}

}  // namespace VLMS::Ocr::Detail
