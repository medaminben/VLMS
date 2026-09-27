#pragma once

// In-process Tesseract OCR, with no Qt in the interface or the implementation.
//
// The target this header belongs to (vlms_ocr) deliberately does not link
// Qt at all, so "no Qt in the OCR module" is a link error rather than a
// convention someone remembers -- the same reason Core links sqlite3, not Qt.
// That is also what keeps this module liftable into its own repository later:
// everything it exchanges with a caller is a std type.
//
// Recognition never runs on the caller's UI thread by accident. There are two
// entry points and both say which thread they use: recognize() blocks the
// thread that calls it, and Job::start() moves exactly that call onto a worker
// thread the caller polls.

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace VLMS::Ocr {

/// Why a run ended. Each value has a matching "ocr.<name>" key in Strings, so
/// a caller renders an outcome without knowing anything about Tesseract.
enum class Status {
    Ok,
    Unavailable,   ///< built without Tesseract, or no traineddata installed
    ImageMissing,  ///< no file at that path, or no decoder for its format
    StartFailed,   ///< Tesseract refused to initialise for those languages
    Cancelled,     ///< the caller asked to stop, and Tesseract stopped
    Empty,         ///< recognition ran and found no text
    Failed,
};

struct Result {
    Status status = Status::Failed;
    std::string text;  ///< UTF-8, trimmed. Non-empty only when status == Ok.
};

struct Request {
    std::string imagePath;

    /// One or more Tesseract codes joined with '+', e.g. "fra" or "ara+fra".
    /// Empty means "whatever defaultLanguageForBook() would pick for a book
    /// with no language", which is a fallback, not a recommendation: callers
    /// that know the book's language should say so.
    std::string languages;

    /// Cap on the longer edge, in pixels, applied before recognition.
    ///
    /// Tesseract wants roughly 300 DPI and gains nothing from a 12 MP phone
    /// photo of a book jacket -- it just spends the pixels. 2600 keeps a
    /// full page comfortably above 300 DPI. 0 disables scaling.
    int maxDimensionPx = 2600;
};

// --- Installation -----------------------------------------------------------

/// Directory holding the *.traineddata files, or "" when none was found.
[[nodiscard]] std::string tessdataPrefix();

/// Tesseract codes actually present in tessdataPrefix(), sorted: {"ara",
/// "eng", "fra"}. "osd" is excluded -- it is orientation data rather than a
/// recognition model, and listing it in a language menu would be a lie.
[[nodiscard]] std::vector<std::string> availableLanguages();

/// True when at least one recognition language is installed.
///
/// Deliberately not "all three". A machine that ended up with only
/// eng.traineddata can still read English books, and switching the whole
/// feature off because Arabic is missing helps nobody.
[[nodiscard]] bool isAvailable();

// --- Language selection -----------------------------------------------------

/// ISO 639-1 as stored in books.language -> Tesseract's 639-2/T code, or ""
/// when there is no known mapping (including for the free-text languages the
/// catalogue's "other" field allows).
///
/// The answer is a *candidate*: it still has to appear in availableLanguages()
/// before it can be run. Mapping and installation are separate questions on
/// purpose, so that dropping spa.traineddata into tessdata is all it takes to
/// make Spanish books work.
[[nodiscard]] std::string tesseractCodeForBookLanguage(const std::string& iso639_1);

/// The inverse: "ara" -> "ar", or "" when the code is not one this module
/// maps. Exists so a caller can label an installed language with the name the
/// catalogue already shows for it, instead of putting "ara" in front of a
/// librarian who has only ever seen "عربي".
[[nodiscard]] std::string bookLanguageForTesseractCode(const std::string& tesseractCode);

/// The language to preselect for a book, given what is installed: the book's
/// own language when its model is present, otherwise `fallback` when that is
/// present, otherwise the first installed language, otherwise "".
[[nodiscard]] std::string defaultLanguageForBook(const std::string& iso639_1,
                                                 const std::string& fallback = "eng");

// --- Running ----------------------------------------------------------------

/// Recognises on the calling thread, blocking until finished.
///
/// `onProgress` is called with 0-100 as Tesseract advances through the page,
/// from whichever thread is running this call. Returning false from it cancels
/// the run, which then reports Status::Cancelled. Pass {} to neither observe
/// nor cancel.
[[nodiscard]] Result recognize(const Request& request,
                               const std::function<bool(int percent)>& onProgress = {});

/// Drops the engine held between runs, freeing the tens of megabytes its
/// language models occupy.
///
/// Initialising Tesseract costs several hundred milliseconds per language, so
/// the engine is kept alive after a run rather than rebuilt on the next one.
/// That is the right trade while a librarian is working through a book and the
/// wrong one afterwards, so the cache is not self-managing: whoever opened the
/// workflow closes it. Safe to call when nothing is cached, and safe to call
/// between runs -- the next recognize() simply pays for Init() again.
///
/// Not safe to call while a Job is running.
void releaseCachedEngine();

/// One recognize() call running on its own std::thread, polled by its owner.
///
/// Polling rather than a completion callback is the deliberate choice: a
/// callback would fire on the worker thread, and every caller here is a UI
/// that may only touch its widgets from the main thread. Handing them a
/// callback would be handing them a way to get it wrong.
///
/// The destructor cancels and joins, so a Job may be dropped at any moment --
/// including while Tesseract is mid-page -- without leaking the thread or
/// letting it write into freed memory. That is what makes it safe for a dialog
/// to own one and simply close.
class Job {
public:
    /// Starts the worker immediately. Never returns null; a Job that could not
    /// run reports done() at once with the reason in take().
    [[nodiscard]] static std::unique_ptr<Job> start(Request request);

    ~Job();

    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;
    Job(Job&&) = delete;
    Job& operator=(Job&&) = delete;

    /// 0-100. Tesseract reports progress only during recognition, so this
    /// stays at 0 while the image is decoded and the engine initialises.
    [[nodiscard]] int progressPercent() const noexcept;

    /// True once the worker has stored its result. Safe to poll from the
    /// owning thread as often as you like.
    [[nodiscard]] bool done() const noexcept;

    /// Asks the worker to stop at Tesseract's next check, and returns without
    /// waiting. done() still has to become true before the result is readable.
    void cancel() noexcept;

    /// Waits for the worker to finish and returns its result. Call once.
    [[nodiscard]] Result take();

private:
    struct Impl;
    explicit Job(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

}  // namespace VLMS::Ocr
