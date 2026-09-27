// Tests for the Qt-free OCR module.
//
// Two halves. The language mapping and the Job lifecycle are pure logic and
// run everywhere, including on a machine with no Tesseract at all -- that is
// the point of them: the paths a customer hits when the engine is missing are
// exactly the paths nobody exercises by hand. The recognition tests need a
// real engine and skip themselves without one.
//
// The fixture image is checked in rather than rendered here on purpose. Drawing
// text with QPainter would make the assertions depend on whichever fonts the
// build machine happens to have, and a test that fails because a font changed
// tells you nothing about OCR.

#include <VLMS/Ocr/Ocr.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

using namespace VLMS;

namespace {

std::string fixtureImage()
{
    const char* dir = std::getenv("VLMS_TEST_OCR_DATA_DIR");
#if defined(VLMS_TEST_OCR_DATA_DIR)
    if (dir == nullptr || dir[0] == '\0') {
        dir = VLMS_TEST_OCR_DATA_DIR;
    }
#endif
    const std::filesystem::path base = (dir != nullptr) ? dir : std::filesystem::path{};
    return (base / "ocr_sample_eng.png").string();
}

bool hasEnglish()
{
    const std::vector<std::string> installed = Ocr::availableLanguages();
    return std::find(installed.begin(), installed.end(), "eng") != installed.end();
}

Ocr::Request englishRequest()
{
    Ocr::Request request;
    request.imagePath = fixtureImage();
    request.languages = "eng";
    return request;
}

bool containsInsensitive(std::string_view text, std::string_view needle)
{
    if (needle.size() > text.size()) {
        return false;
    }
    auto lower = [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); };
    std::string haystack(text);
    std::string find(needle);
    for (char& ch : haystack) {
        ch = lower(static_cast<unsigned char>(ch));
    }
    for (char& ch : find) {
        ch = lower(static_cast<unsigned char>(ch));
    }
    return haystack.find(find) != std::string::npos;
}

std::string trimmed(const std::string& text)
{
    const auto begin = text.find_first_not_of(" \t\n\r");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\n\r");
    return text.substr(begin, end - begin + 1);
}

}  // namespace

class test_ocr_Ocr : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        ASSERT_TRUE(std::filesystem::exists(fixtureImage()))
            << "missing fixture: " << fixtureImage();
    }

    static void TearDownTestSuite()
    {
        // Not tidiness -- correctness, and the reason releaseCachedEngine() is part
        // of the public interface instead of the cache managing itself.
        //
        // A TessBaseAPI left alive until process exit is destroyed by static
        // teardown, in an order not defined against Tesseract's own global
        // dictionary cache. When that cache goes first the engine cannot return its
        // dawgs to it, and the run ends with "ObjectCache: WARNING! LEAK!" and ~3.7
        // MB reported by LeakSanitizer. BookEditorDialog avoids this by releasing
        // in its destructor; a test that recognises anything has to do the same.
        Ocr::releaseCachedEngine();
    }
};

TEST_F(test_ocr_Ocr, MapsCatalogLanguagesToTesseractCodes)
{
    // Every code the catalogue's language combo offers, including the seven
    // with no bundled model: the mapping describes the book, not the install.
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("ar"), std::string("ara"));
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("fr"), std::string("fra"));
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("en"), std::string("eng"));
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("es"), std::string("spa"));
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("de"), std::string("deu"));
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("it"), std::string("ita"));
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("tr"), std::string("tur"));
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("pt"), std::string("por"));
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("ru"), std::string("rus"));
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("zh"), std::string("chi_sim"));

    // books.language is free text underneath, so casing and region suffixes
    // reach this function in practice.
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("AR"), std::string("ara"));
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("fr-FR"), std::string("fra"));
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("En"), std::string("eng"));
}

TEST_F(test_ocr_Ocr, MapsUnknownAndFreeTextLanguagesToNothing)
{
    // The combo's "other" field accepts anything a cataloguer types. None of it
    // may be guessed into a language: running Arabic models over a Berber title
    // and pasting the output into the description is worse than doing nothing.
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage(""), std::string());
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("zz"), std::string());
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("Tamazight"), std::string());
    EXPECT_EQ(Ocr::tesseractCodeForBookLanguage("x"), std::string());
}

TEST_F(test_ocr_Ocr, MapsTesseractCodesBackToCatalogLanguages)
{
    EXPECT_EQ(Ocr::bookLanguageForTesseractCode("ara"), std::string("ar"));
    EXPECT_EQ(Ocr::bookLanguageForTesseractCode("fra"), std::string("fr"));
    EXPECT_EQ(Ocr::bookLanguageForTesseractCode("eng"), std::string("en"));
    EXPECT_EQ(Ocr::bookLanguageForTesseractCode("chi_sim"), std::string("zh"));

    // An installed model this module does not know about has no catalogue
    // name; the UI labels it with its raw code rather than hiding it.
    EXPECT_EQ(Ocr::bookLanguageForTesseractCode("osd"), std::string());
    EXPECT_EQ(Ocr::bookLanguageForTesseractCode("klingon"), std::string());
    EXPECT_EQ(Ocr::bookLanguageForTesseractCode(""), std::string());
}

TEST_F(test_ocr_Ocr, AvailableLanguagesAreSortedAndExcludeOrientationData)
{
    const std::vector<std::string> installed = Ocr::availableLanguages();

    EXPECT_TRUE(std::is_sorted(installed.begin(), installed.end()));
    EXPECT_EQ(std::adjacent_find(installed.begin(), installed.end()), installed.end());

    // osd.traineddata ships with most Tesseract packages and initialises
    // happily, then recognises nothing. It must never reach a language menu.
    EXPECT_EQ(std::find(installed.begin(), installed.end(), "osd"), installed.end());

    for (const std::string& code : installed) {
        EXPECT_FALSE(code.empty()) << "an empty language code would build 'ara++fra'";
    }
}

TEST_F(test_ocr_Ocr, IsAvailableAgreesWithTheLanguageList)
{
    EXPECT_EQ(Ocr::isAvailable(), !Ocr::availableLanguages().empty());

    // Availability is "at least one language", not "all three". A build that
    // shipped only English still reads English books.
    if (Ocr::isAvailable()) {
        EXPECT_FALSE(Ocr::tessdataPrefix().empty());
    }
}

TEST_F(test_ocr_Ocr, DefaultLanguagePrefersTheBooksOwnLanguage)
{
    if (!Ocr::isAvailable()) {
        GTEST_SKIP() << "no OCR languages installed";
    }

    for (const std::string& installed : Ocr::availableLanguages()) {
        const std::string iso = Ocr::bookLanguageForTesseractCode(installed);
        if (iso.empty()) {
            continue;
        }
        EXPECT_EQ(Ocr::defaultLanguageForBook(iso), installed);
    }
}

TEST_F(test_ocr_Ocr, DefaultLanguageFallsBackWhenTheBooksModelIsMissing)
{
    if (!Ocr::isAvailable()) {
        EXPECT_EQ(Ocr::defaultLanguageForBook("ar"), std::string());
        GTEST_SKIP() << "no OCR languages installed";
    }

    const std::vector<std::string> installed = Ocr::availableLanguages();
    const auto isInstalled = [&installed](const std::string& code) {
        return std::find(installed.begin(), installed.end(), code) != installed.end();
    };

    // A Klingon book still has to produce a runnable language, or the button
    // would be enabled with nothing behind it.
    const std::string chosen = Ocr::defaultLanguageForBook("zz");
    EXPECT_TRUE(isInstalled(chosen)) << "fallback picked a language that is not installed";

    // And the fallback itself is honoured when it exists.
    if (isInstalled("eng")) {
        EXPECT_EQ(Ocr::defaultLanguageForBook("zz"), std::string("eng"));
    }
    if (installed.size() > 1) {
        const std::string other = installed.front() == "eng" ? installed.back() : installed.front();
        EXPECT_EQ(Ocr::defaultLanguageForBook("zz", other), other);
    }
}

TEST_F(test_ocr_Ocr, MissingImageIsReportedRatherThanThrown)
{
    Ocr::Request request;
    request.imagePath = "/nonexistent/definitely/not/here.png";
    request.languages = "eng";

    const Ocr::Result result = Ocr::recognize(request);
    EXPECT_TRUE(result.text.empty());
    // Without an engine the module cannot get as far as opening the file, and
    // says so rather than claiming the image is at fault.
    EXPECT_TRUE(result.status == (Ocr::isAvailable() ? Ocr::Status::ImageMissing
                                                     : Ocr::Status::Unavailable));

    Ocr::Request empty;
    empty.languages = "eng";
    EXPECT_TRUE(Ocr::recognize(empty).text.empty());
}

TEST_F(test_ocr_Ocr, AJobCancelledBeforeItRunsReportsCancelled)
{
    if (!Ocr::isAvailable()) {
        GTEST_SKIP() << "no OCR languages installed";
    }

    std::unique_ptr<Ocr::Job> job = Ocr::Job::start(englishRequest());
    ASSERT_NE(job, nullptr);
    job->cancel();

    const Ocr::Result result = job->take();
    EXPECT_EQ(result.status, Ocr::Status::Cancelled);
    EXPECT_TRUE(result.text.empty());
}

TEST_F(test_ocr_Ocr, DroppingARunningJobDoesNotHang)
{
    // The reason ~Job cancels before it joins. A librarian who closes the book
    // editor mid-run must not be waiting on Tesseract to finish the page, and
    // the thread must be gone before the dialog's widgets are.
    const auto started = std::chrono::steady_clock::now();
    {
        std::unique_ptr<Ocr::Job> job = Ocr::Job::start(englishRequest());
        ASSERT_NE(job, nullptr);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    EXPECT_LT(elapsed, 10000) << "dropping a job took " << elapsed << " ms";
}

TEST_F(test_ocr_Ocr, RecognisesTheFixtureImage)
{
    if (!hasEnglish()) {
        GTEST_SKIP() << "eng.traineddata is not installed";
    }

    const Ocr::Result result = Ocr::recognize(englishRequest());
    EXPECT_EQ(result.status, Ocr::Status::Ok);

    const std::string& text = result.text;
    EXPECT_TRUE(containsInsensitive(text, "Catalogue")) << "read back: '" << text << "'";
    EXPECT_TRUE(containsInsensitive(text, "Alexandria")) << "read back: '" << text << "'";

    // Trimmed on the way out, so the description field does not gain a blank
    // line every time the librarian uses this.
    EXPECT_EQ(text, trimmed(text));
}

TEST_F(test_ocr_Ocr, ReportsProgressWhileRecognising)
{
    if (!hasEnglish()) {
        GTEST_SKIP() << "eng.traineddata is not installed";
    }

    int calls = 0;
    int highest = -1;
    bool ordered = true;
    const Ocr::Result result = Ocr::recognize(englishRequest(), [&](int percent) {
        ++calls;
        ordered = ordered && percent >= highest;
        highest = std::max(highest, percent);
        return true;
    });

    EXPECT_EQ(result.status, Ocr::Status::Ok);
    EXPECT_GT(calls, 0) << "the progress callback was never invoked";
    EXPECT_TRUE(ordered) << "progress went backwards";
    EXPECT_TRUE(highest >= 0 && highest <= 100);
}

TEST_F(test_ocr_Ocr, ACancelledRunReturnsNoText)
{
    if (!hasEnglish()) {
        GTEST_SKIP() << "eng.traineddata is not installed";
    }

    // Cancelling from inside the progress callback is the path Job uses, and
    // the one that has to stop Tesseract mid-page rather than at a page break.
    const Ocr::Result result = Ocr::recognize(englishRequest(), [](int) { return false; });
    EXPECT_EQ(result.status, Ocr::Status::Cancelled);
    EXPECT_TRUE(result.text.empty());
}

TEST_F(test_ocr_Ocr, ReleasingTheCachedEngineLeavesLaterRunsWorking)
{
    if (!hasEnglish()) {
        GTEST_SKIP() << "eng.traineddata is not installed";
    }

    const Ocr::Result first = Ocr::recognize(englishRequest());
    EXPECT_EQ(first.status, Ocr::Status::Ok);

    Ocr::releaseCachedEngine();
    Ocr::releaseCachedEngine();  // Idempotent: the dialog may close twice over.

    const Ocr::Result second = Ocr::recognize(englishRequest());
    EXPECT_EQ(second.status, Ocr::Status::Ok);
    EXPECT_EQ(second.text, first.text);

    Ocr::releaseCachedEngine();
}

TEST_F(test_ocr_Ocr, DownscalingDoesNotChangeWhatIsRead)
{
    if (!hasEnglish()) {
        GTEST_SKIP() << "eng.traineddata is not installed";
    }

    Ocr::Request full = englishRequest();
    full.maxDimensionPx = 0;  // No scaling at all.
    Ocr::Request capped = englishRequest();
    capped.maxDimensionPx = 2600;  // The default.

    const Ocr::Result unscaled = Ocr::recognize(full);
    const Ocr::Result scaled = Ocr::recognize(capped);

    EXPECT_EQ(unscaled.status, Ocr::Status::Ok);
    EXPECT_EQ(scaled.status, Ocr::Status::Ok);
    // The fixture is well under the cap, so the two runs must agree exactly --
    // this pins that the cap is a ceiling and not an unconditional resize.
    EXPECT_EQ(scaled.text, unscaled.text);
}
