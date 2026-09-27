#include <VLMS/Ocr/Ocr.h>

#include "Tessdata.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <mutex>
#include <utility>

#if defined(VLMS_HAS_TESSERACT)
#  include <leptonica/allheaders.h>
#  include <tesseract/baseapi.h>
#  include <tesseract/ocrclass.h>
#endif

namespace VLMS::Ocr {

// --- Installation -----------------------------------------------------------

std::string tessdataPrefix()
{
    return Detail::resolveTessdataPrefix();
}

std::vector<std::string> availableLanguages()
{
#if !defined(VLMS_HAS_TESSERACT)
    // No engine to run them, so there are no languages -- reporting the files
    // on disk would let a caller build a menu whose every entry fails.
    return {};
#else
    const std::string prefix = Detail::resolveTessdataPrefix();
    if (prefix.empty()) {
        return {};
    }
    return Detail::languagesInDirectory(std::filesystem::path(prefix));
#endif
}

bool isAvailable()
{
    return !availableLanguages().empty();
}

// --- Language selection -----------------------------------------------------

namespace {

/// The ten codes the catalogue's language combo offers, against Tesseract's
/// 639-2/T names.
///
/// Codes with no bundled model are listed anyway: this table says what the book
/// *is*, and availableLanguages() says what can be run. Keeping the two apart
/// is what makes dropping spa.traineddata into tessdata enough to make Spanish
/// books work, with no code change here.
constexpr std::array<std::pair<const char*, const char*>, 10> kLanguageMap{{
    {"ar", "ara"},
    {"fr", "fra"},
    {"en", "eng"},
    {"es", "spa"},
    {"de", "deu"},
    {"it", "ita"},
    {"tr", "tur"},
    {"pt", "por"},
    {"ru", "rus"},
    {"zh", "chi_sim"},
}};

std::string lowered(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

}  // namespace

std::string tesseractCodeForBookLanguage(const std::string& iso639_1)
{
    // books.language also holds free text, from the combo's "other" field.
    // Lower-casing the first two characters catches "AR" and "fr-FR" without
    // pretending to parse a language tag.
    const std::string key = lowered(iso639_1.substr(0, 2));
    for (const auto& [iso, tess] : kLanguageMap) {
        if (key == iso) {
            return tess;
        }
    }
    return {};
}

std::string bookLanguageForTesseractCode(const std::string& tesseractCode)
{
    const std::string key = lowered(tesseractCode);
    for (const auto& [iso, tess] : kLanguageMap) {
        if (key == tess) {
            return iso;
        }
    }
    return {};
}

std::string defaultLanguageForBook(const std::string& iso639_1, const std::string& fallback)
{
    const std::vector<std::string> installed = availableLanguages();
    if (installed.empty()) {
        return {};
    }

    const auto isInstalled = [&installed](const std::string& code) {
        return !code.empty()
            && std::find(installed.begin(), installed.end(), code) != installed.end();
    };

    const std::string preferred = tesseractCodeForBookLanguage(iso639_1);
    if (isInstalled(preferred)) {
        return preferred;
    }
    if (isInstalled(fallback)) {
        return fallback;
    }
    return installed.front();
}

// --- Running ----------------------------------------------------------------

#if !defined(VLMS_HAS_TESSERACT)

Result recognize(const Request&, const std::function<bool(int)>&)
{
    return {Status::Unavailable, {}};
}

void releaseCachedEngine() {}

#else

namespace {

std::string trimmed(std::string text)
{
    // ASCII whitespace only, which is all Tesseract pads a page with. UTF-8
    // continuation bytes are all >= 0x80, so this can never split a codepoint.
    const auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };

    const auto begin = std::find_if_not(text.begin(), text.end(), isSpace);
    const auto end = std::find_if_not(text.rbegin(), std::make_reverse_iterator(begin), isSpace);
    return std::string(begin, end.base());
}

/// pixDestroy takes a Pix** and nulls it, so it cannot be handed to a
/// unique_ptr deleter directly.
struct PixDeleter {
    void operator()(Pix* pix) const noexcept
    {
        if (pix != nullptr) {
            pixDestroy(&pix);
        }
    }
};
using PixPtr = std::unique_ptr<Pix, PixDeleter>;

/// One initialised engine, kept between runs.
///
/// Init() re-reads and re-parses the LSTM models every time it is called --
/// several hundred milliseconds for a language, and it used to happen on every
/// button press. Holding the engine costs tens of megabytes of resident memory,
/// which is why releaseCachedEngine() exists and why the dialog calls it on
/// close rather than leaving it charged to the app for the rest of the session.
struct EngineCache {
    std::mutex mutex;
    std::unique_ptr<tesseract::TessBaseAPI> api;
    std::string languages;
    std::string datapath;
};

EngineCache& engineCache()
{
    static EngineCache cache;
    return cache;
}

/// Hands out an engine for `languages`, reusing the cached one when it matches.
/// Returns null when Tesseract refused to initialise.
///
/// A caller that arrives while another run holds the cached engine simply gets
/// a fresh one: TessBaseAPI is not safe to use from two threads at once, and
/// blocking the second caller would freeze whichever UI is waiting on it -- the
/// exact failure this module exists to remove.
std::unique_ptr<tesseract::TessBaseAPI> acquireEngine(const std::string& datapath,
                                                      const std::string& languages)
{
    EngineCache& cache = engineCache();
    {
        const std::lock_guard<std::mutex> lock(cache.mutex);
        if (cache.api != nullptr && cache.languages == languages && cache.datapath == datapath) {
            return std::move(cache.api);
        }
    }

    auto api = std::make_unique<tesseract::TessBaseAPI>();
    if (api->Init(datapath.c_str(), languages.c_str()) != 0) {
        return nullptr;
    }
    return api;
}

void returnEngine(std::unique_ptr<tesseract::TessBaseAPI> api,
                  const std::string& datapath,
                  const std::string& languages)
{
    if (api == nullptr) {
        return;
    }

    // Drop the page: Clear() releases the image and the recognition results,
    // and resetting the adaptive classifier keeps one book's cover from
    // biasing the next one's.
    api->Clear();
    api->ClearAdaptiveClassifier();

    EngineCache& cache = engineCache();
    const std::lock_guard<std::mutex> lock(cache.mutex);
    if (cache.api != nullptr) {
        return;  // Someone else refilled the slot; let this one go.
    }
    cache.api = std::move(api);
    cache.languages = languages;
    cache.datapath = datapath;
}

/// Passed to Tesseract through tesseract::ETEXT_DESC::cancel_this.
struct MonitorContext {
    const std::function<bool(int)>* onProgress = nullptr;
    tesseract::ETEXT_DESC* monitor = nullptr;
    bool cancelled = false;
};

/// Tesseract calls this periodically during Recognize(); returning true stops
/// the run. It is the only place progress can be observed, so it doubles as the
/// progress tap -- tesseract::ETEXT_DESC::progress is updated by the engine just before.
bool monitorCallback(void* data, int /*words*/)
{
    auto* context = static_cast<MonitorContext*>(data);
    if (context == nullptr || context->onProgress == nullptr || !*context->onProgress) {
        return false;
    }

    const int percent = context->monitor != nullptr ? context->monitor->progress : 0;
    if ((*context->onProgress)(percent)) {
        return false;
    }

    context->cancelled = true;
    return true;
}

PixPtr loadAndScale(const std::string& imagePath, int maxDimensionPx)
{
    PixPtr pix(pixRead(imagePath.c_str()));
    if (pix == nullptr) {
        return nullptr;
    }
    if (maxDimensionPx <= 0) {
        return pix;
    }

    const int width = pixGetWidth(pix.get());
    const int height = pixGetHeight(pix.get());
    const int longEdge = std::max(width, height);
    if (longEdge <= maxDimensionPx) {
        return pix;
    }

    const float factor = static_cast<float>(maxDimensionPx) / static_cast<float>(longEdge);
    PixPtr scaled(pixScale(pix.get(), factor, factor));
    // A failed downscale is not a failed run: recognising the full-size image
    // is slower, not wrong.
    return scaled != nullptr ? std::move(scaled) : std::move(pix);
}

}  // namespace

Result recognize(const Request& request, const std::function<bool(int percent)>& onProgress)
{
    const std::string prefix = Detail::resolveTessdataPrefix();
    if (prefix.empty()) {
        return {Status::Unavailable, {}};
    }

    const std::string languages =
        request.languages.empty() ? defaultLanguageForBook({}) : request.languages;
    if (languages.empty()) {
        return {Status::Unavailable, {}};
    }

    if (request.imagePath.empty()) {
        return {Status::ImageMissing, {}};
    }

    // Decoding happens here rather than in the caller so that a 12 MP JPEG
    // costs the worker thread, not whichever thread wanted the text.
    PixPtr pix = loadAndScale(request.imagePath, request.maxDimensionPx);
    if (pix == nullptr) {
        return {Status::ImageMissing, {}};
    }

    // Cancellation between decode and Init: the image can take a while on its
    // own, and a caller who gave up should not then wait for the engine.
    if (onProgress && !onProgress(0)) {
        return {Status::Cancelled, {}};
    }

    std::unique_ptr<tesseract::TessBaseAPI> api = acquireEngine(prefix, languages);
    if (api == nullptr) {
        return {Status::StartFailed, {}};
    }

    api->SetPageSegMode(tesseract::PSM_AUTO);
    api->SetImage(pix.get());

    tesseract::ETEXT_DESC monitor;
    MonitorContext context;
    context.onProgress = &onProgress;
    context.monitor = &monitor;
    monitor.cancel = &monitorCallback;
    monitor.cancel_this = &context;

    const int recognizeResult = api->Recognize(&monitor);

    Result result;
    if (context.cancelled) {
        result = {Status::Cancelled, {}};
    } else if (recognizeResult != 0) {
        result = {Status::Failed, {}};
    } else {
        char* text = api->GetUTF8Text();
        if (text == nullptr) {
            result = {Status::Failed, {}};
        } else {
            std::string recognized = trimmed(text);
            delete[] text;
            result = recognized.empty() ? Result{Status::Empty, {}}
                                        : Result{Status::Ok, std::move(recognized)};
        }
    }

    // Before pix goes out of scope: the engine is still holding it.
    returnEngine(std::move(api), prefix, languages);
    return result;
}

void releaseCachedEngine()
{
    EngineCache& cache = engineCache();
    const std::lock_guard<std::mutex> lock(cache.mutex);
    cache.api.reset();
    cache.languages.clear();
    cache.datapath.clear();
}

#endif  // VLMS_HAS_TESSERACT

}  // namespace VLMS::Ocr
