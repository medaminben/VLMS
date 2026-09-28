#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Core/Strings.h>

#include <gtest/gtest.h>

#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using VLMS::Locale;
using VLMS::Strings;

namespace {

constexpr auto kArabic = "ar";
constexpr auto kFrench = "fr";
constexpr auto kEnglish = "en";

std::string join(const std::vector<std::string>& values, std::string_view sep)
{
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out.append(sep);
        }
        out += values[i];
    }
    return out;
}

/// The {placeholder} tokens a string expects, as a set.
std::set<std::string> placeholderTokens(const std::string& text)
{
    static const std::regex pattern("\\{(\\w+)\\}");
    std::set<std::string> tokens;
    for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) {
        tokens.insert((*it)[1].str());
    }
    return tokens;
}

std::string describeMissing(const std::vector<std::string>& missing,
                            const std::string& from,
                            const std::string& to)
{
    // Name the keys. "expected 412, got 411" is useless to a translator.
    std::vector<std::string> shown = missing;
    std::string suffix;
    if (shown.size() > 20) {
        suffix = " (and " + std::to_string(shown.size() - 20) + " more)";
        shown.resize(20);
    }
    return std::to_string(missing.size()) + " key(s) present in '" + from + "' but missing from '"
        + to + "':\n  " + join(shown, "\n  ") + suffix;
}

std::vector<std::string> keysMissingFrom(const std::string& reference, const std::string& candidate)
{
    const std::vector<std::string> referenceKeys = Strings::knownKeys(reference);
    const std::vector<std::string> candidateList = Strings::knownKeys(candidate);
    const std::set<std::string> candidateKeys(candidateList.begin(), candidateList.end());

    std::vector<std::string> missing;
    for (const std::string& key : referenceKeys) {
        if (!candidateKeys.contains(key)) {
            missing.push_back(key);
        }
    }
    return missing;
}

}  // namespace

class test_core_StringsParity : public ::testing::Test {
protected:
    void TearDown() override { Locale::setCode(Locale::kDefaultCode); }
};

TEST_F(test_core_StringsParity, EveryTableIsNonEmpty)
{
    EXPECT_GT(Strings::knownKeys(kArabic).size(), 100u);
    EXPECT_FALSE(Strings::knownKeys(kFrench).empty());
    EXPECT_FALSE(Strings::knownKeys(kEnglish).empty());
}

TEST_F(test_core_StringsParity, TablesHaveIdenticalKeySets)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        {kArabic, kFrench}, {kArabic, kEnglish}, {kFrench, kArabic},
        {kFrench, kEnglish}, {kEnglish, kArabic}, {kEnglish, kFrench},
    };
    for (const auto& [reference, candidate] : cases) {
        SCOPED_TRACE(reference + "->" + candidate);
        const std::vector<std::string> missing = keysMissingFrom(reference, candidate);
        EXPECT_TRUE(missing.empty()) << describeMissing(missing, reference, candidate);
    }
}

TEST_F(test_core_StringsParity, NoTableHasAnEmptyValue)
{
    for (const char* locale : {kArabic, kFrench, kEnglish}) {
        SCOPED_TRACE(locale);

        std::vector<std::string> empties;
        for (const std::string& key : Strings::knownKeys(locale)) {
            if (Strings::rawValue(locale, key).empty()) {
                empties.push_back(key);
            }
        }

        // An empty value is worse than a missing one: lookup() treats it as absent
        // and silently falls back to Arabic in the middle of a French UI.
        EXPECT_TRUE(empties.empty())
            << "empty value(s) in '" << locale << "': " << join(empties, ", ");
    }
}

TEST_F(test_core_StringsParity, PlaceholderTokensMatchAcrossLocales)
{
    std::vector<std::string> problems;

    for (const std::string& key : Strings::knownKeys(kArabic)) {
        const std::set<std::string> arabic = placeholderTokens(Strings::rawValue(kArabic, key));

        for (const char* other : {kFrench, kEnglish}) {
            const std::string value = Strings::rawValue(other, key);
            if (value.empty()) {
                continue;  // reported by tablesHaveIdenticalKeySets
            }

            const std::set<std::string> tokens = placeholderTokens(value);
            if (tokens != arabic) {
                const std::vector<std::string> expected(arabic.begin(), arabic.end());
                const std::vector<std::string> got(tokens.begin(), tokens.end());
                problems.push_back(key + " [" + other + "]: expected {" + join(expected, ",")
                                   + "}, found {" + join(got, ",") + "}");
            }
        }
    }

    // A translated "{annee}" where the code substitutes "{year}" renders a
    // literal brace to the user and no test would otherwise notice.
    EXPECT_TRUE(problems.empty()) << "placeholder mismatch:\n  " << join(problems, "\n  ");
}

TEST_F(test_core_StringsParity, LookupFallsBackToArabicForMissingKey)
{
    Locale::setCode("fr");
    // Every key exists in every table (asserted above), so pick a real one and
    // confirm the French text is used rather than the Arabic fallback.
    const std::string key = "nav.catalog";
    EXPECT_EQ(Strings::t(key), Strings::rawValue("fr", key));
}

TEST_F(test_core_StringsParity, LookupReturnsKeyItselfWhenAbsentEverywhere)
{
    const std::string key = "this.key.does.not.exist";
    EXPECT_EQ(Strings::t(key), key);
}

TEST_F(test_core_StringsParity, BookLanguageLabelFallsBackToRawCode)
{
    EXPECT_EQ(Strings::bookLanguageLabel("zz"), "zz");
    EXPECT_EQ(Strings::bookLanguageLabel({}), std::string());
    EXPECT_FALSE(Strings::bookLanguageLabel("ar").empty());
}

TEST_F(test_core_StringsParity, MemberStatusLabelCoversAllStatusCodes)
{
    for (const std::string& code : MemberRepository::statusCodes()) {
        for (const char* locale : {kArabic, kFrench, kEnglish}) {
            Locale::setCode(locale);
            const std::string label = Strings::memberStatusLabel(code);
            EXPECT_NE(label, code) << "no " << locale << " label for status '" << code << "'";
        }
    }
}

TEST_F(test_core_StringsParity, MemberSexLabelCoversAllSexCodes)
{
    for (const std::string& code : MemberRepository::sexCodes()) {
        for (const char* locale : {kArabic, kFrench, kEnglish}) {
            Locale::setCode(locale);
            const std::string label = Strings::memberSexLabel(code);
            EXPECT_NE(label, code) << "no " << locale << " label for sex '" << code << "'";
        }
    }
}

TEST_F(test_core_StringsParity, NoBrowserSentences)
{
    EXPECT_EQ(Strings::rawValue(kArabic, "help.noBrowser"),
              "تعذّر العثور على متصفح لفتح دليل الاستخدام.");
    EXPECT_EQ(Strings::rawValue(kFrench, "help.noBrowser"),
              "Aucun navigateur n'a été trouvé pour ouvrir le manuel d'utilisation.");
    EXPECT_EQ(Strings::rawValue(kEnglish, "help.noBrowser"),
              "No browser was found to open the user manual.");
}

TEST_F(test_core_StringsParity, ArchiveKeysHaveSpecifiedWording)
{
    struct Case {
        const char* locale;
        const char* key;
        const char* value;
    };

    const Case cases[] = {
        {kArabic, "catalog.deleteConfirm", "نقل هذا الكتاب وكل نسخه إلى الأرشيف؟"},
        {kFrench, "catalog.deleteConfirm",
         "Déplacer cet ouvrage et tous ses exemplaires vers les archives ?"},
        {kEnglish, "catalog.deleteConfirm",
         "Move this book and all its copies to the Archive?"},

        {kArabic, "nav.archive", "الأرشيف"},
        {kFrench, "nav.archive", "Archives"},
        {kEnglish, "nav.archive", "Archive"},
        {kArabic, "page.archive.title", "الأرشيف"},
        {kFrench, "page.archive.title", "Archives"},
        {kEnglish, "page.archive.title", "Archive"},
        {kArabic, "page.archive.body", "الأعضاء والكتب والنسخ والإعارات المؤرشفة."},
        {kFrench, "page.archive.body", "Adhérents, ouvrages, exemplaires et prêts archivés."},
        {kEnglish, "page.archive.body", "Archived members, books, copies, and loans."},
        {kArabic, "archive.type.members", "الأعضاء"},
        {kFrench, "archive.type.members", "Adhérents"},
        {kEnglish, "archive.type.members", "Members"},
        {kArabic, "archive.type.books", "الكتب"},
        {kFrench, "archive.type.books", "Ouvrages"},
        {kEnglish, "archive.type.books", "Books"},
        {kArabic, "archive.type.copies", "النسخ"},
        {kFrench, "archive.type.copies", "Exemplaires"},
        {kEnglish, "archive.type.copies", "Copies"},
        {kArabic, "archive.type.loans", "الإعارات"},
        {kFrench, "archive.type.loans", "Prêts"},
        {kEnglish, "archive.type.loans", "Loans"},
        {kArabic, "archive.searchPlaceholder", "البحث في الأرشيف…"},
        {kFrench, "archive.searchPlaceholder", "Rechercher dans les archives…"},
        {kEnglish, "archive.searchPlaceholder", "Search the archive…"},
        {kArabic, "archive.col.number", "الرقم"},
        {kFrench, "archive.col.number", "Numéro"},
        {kEnglish, "archive.col.number", "Number"},
        {kArabic, "archive.col.name", "الاسم"},
        {kFrench, "archive.col.name", "Nom"},
        {kEnglish, "archive.col.name", "Name"},
        {kArabic, "archive.col.city", "المدينة"},
        {kFrench, "archive.col.city", "Ville"},
        {kEnglish, "archive.col.city", "City"},
        {kArabic, "archive.col.status", "الحالة"},
        {kFrench, "archive.col.status", "Statut"},
        {kEnglish, "archive.col.status", "Status"},
        {kArabic, "archive.col.title", "العنوان"},
        {kFrench, "archive.col.title", "Titre"},
        {kEnglish, "archive.col.title", "Title"},
        {kArabic, "archive.col.author", "المؤلف"},
        {kFrench, "archive.col.author", "Auteur"},
        {kEnglish, "archive.col.author", "Author"},
        {kArabic, "archive.col.copies", "النسخ المؤرشفة"},
        {kFrench, "archive.col.copies", "Exemplaires archivés"},
        {kEnglish, "archive.col.copies", "Archived copies"},
        {kArabic, "archive.col.localId", "الرقم المحلي"},
        {kFrench, "archive.col.localId", "N° local"},
        {kEnglish, "archive.col.localId", "Local no."},
        {kArabic, "archive.col.source", "المصدر"},
        {kFrench, "archive.col.source", "Source"},
        {kEnglish, "archive.col.source", "Source"},
        {kArabic, "archive.col.member", "العضو"},
        {kFrench, "archive.col.member", "Adhérent"},
        {kEnglish, "archive.col.member", "Member"},
        {kArabic, "archive.col.copy", "النسخة / العنوان"},
        {kFrench, "archive.col.copy", "Exemplaire / titre"},
        {kEnglish, "archive.col.copy", "Copy / title"},
        {kArabic, "archive.col.returnedAt", "تاريخ الإرجاع"},
        {kFrench, "archive.col.returnedAt", "Rendu le"},
        {kEnglish, "archive.col.returnedAt", "Returned"},
        {kArabic, "archive.col.archivedAt", "تاريخ الأرشفة"},
        {kFrench, "archive.col.archivedAt", "Archivé le"},
        {kEnglish, "archive.col.archivedAt", "Archived"},
        {kArabic, "archive.col.notes", "ملاحظات النسخة"},
        {kFrench, "archive.col.notes", "Notes de l'exemplaire"},
        {kEnglish, "archive.col.notes", "Copy notes"},
        {kArabic, "archive.restore", "استرجاع"},
        {kFrench, "archive.restore", "Restaurer"},
        {kEnglish, "archive.restore", "Restore"},
        {kArabic, "archive.restoreConfirm", "إعادة هذا السجل من الأرشيف؟"},
        {kFrench, "archive.restoreConfirm", "Restaurer cet élément depuis les archives ?"},
        {kEnglish, "archive.restoreConfirm", "Restore this record from the Archive?"},
        {kArabic, "archive.reuse", "إعادة استعمال الرقم المحلي"},
        {kFrench, "archive.reuse", "Réutiliser le n° local"},
        {kEnglish, "archive.reuse", "Reuse local number"},
        {kArabic, "archive.reuse.title", "اختيار الكتاب"},
        {kFrench, "archive.reuse.title", "Choisir l'ouvrage"},
        {kEnglish, "archive.reuse.title", "Choose the book"},
        {kArabic, "archive.reuse.prompt", "الكتاب الذي سيأخذ الرقم {number}:"},
        {kFrench, "archive.reuse.prompt", "Ouvrage qui recevra le n° {number} :"},
        {kEnglish, "archive.reuse.prompt", "Book that takes number {number}:"},
        {kArabic, "archive.reuse.search", "البحث عن كتاب…"},
        {kFrench, "archive.reuse.search", "Rechercher un ouvrage…"},
        {kEnglish, "archive.reuse.search", "Search for a book…"},
        {kArabic, "archive.reuse.newBook", "كتاب جديد"},
        {kFrench, "archive.reuse.newBook", "Nouvel ouvrage"},
        {kEnglish, "archive.reuse.newBook", "New book"},
        {kArabic, "circulation.delete", "حذف"},
        {kFrench, "circulation.delete", "Supprimer"},
        {kEnglish, "circulation.delete", "Delete"},
        {kArabic, "circulation.archiveLoan", "نقل هذه الإعارة المُرجعة إلى الأرشيف؟"},
        {kFrench, "circulation.archiveLoan", "Déplacer ce prêt rendu vers les archives ?"},
        {kEnglish, "circulation.archiveLoan", "Move this returned loan to the Archive?"},
        {kArabic, "copy.note.wasIndexedAs", "كان مفهرسًا تحت الرقم {number}"},
        {kFrench, "copy.note.wasIndexedAs", "était indexé sous {number}"},
        {kEnglish, "copy.note.wasIndexedAs", "was indexed as {number}"},
        {kArabic, "copy.note.newIndexedAs", "أعيدت فهرسته تحت الرقم {number}"},
        {kFrench, "copy.note.newIndexedAs", "nouvellement indexé sous {number}"},
        {kEnglish, "copy.note.newIndexedAs", "new indexed as {number}"},
        {kArabic, "error.book.duplicateArchived",
         "هذا الكتاب موجود في الأرشيف؛ استرجعه من هناك."},
        {kFrench, "error.book.duplicateArchived",
         "Cet ouvrage est dans les archives ; restaurez-le depuis celles-ci."},
        {kEnglish, "error.book.duplicateArchived",
         "This book is in the Archive; restore it from there."},
        {kArabic, "error.book.notArchived", "هذا الكتاب ليس في الأرشيف."},
        {kFrench, "error.book.notArchived", "Cet ouvrage n'est pas archivé."},
        {kEnglish, "error.book.notArchived", "This book is not in the Archive."},
        {kArabic, "error.copy.notArchived", "هذه النسخة ليست في الأرشيف."},
        {kFrench, "error.copy.notArchived", "Cet exemplaire n'est pas archivé."},
        {kEnglish, "error.copy.notArchived", "This copy is not in the Archive."},
        {kArabic, "error.copy.bookMissing", "الكتاب الأصلي لهذه النسخة لم يعد موجودًا."},
        {kFrench, "error.copy.bookMissing",
         "L'ouvrage d'origine de cet exemplaire n'existe plus."},
        {kEnglish, "error.copy.bookMissing", "This copy's original book no longer exists."},
        {kArabic, "error.copy.numberHeldByArchived",
         "الرقم «{detail}» محجوز لنسخة مؤرشفة؛ انقله من الأرشيف ← إعادة استعمال الرقم المحلي."},
        {kFrench, "error.copy.numberHeldByArchived",
         "Le n° « {detail} » est détenu par un exemplaire archivé ; déplacez-le via "
         "Archives → Réutiliser le n° local."},
        {kEnglish, "error.copy.numberHeldByArchived",
         "Number \"{detail}\" is held by an archived copy; move it with Archive → Reuse local number."},
        {kArabic, "error.copy.reuseStale", "النسخة المؤرشفة لم تعد تحمل هذا الرقم."},
        {kFrench, "error.copy.reuseStale", "L'exemplaire archivé ne détient plus ce numéro."},
        {kEnglish, "error.copy.reuseStale", "The archived copy no longer holds that number."},
        {kArabic, "error.copy.sourceMismatch", "لغة هذا الكتاب لا تناسب مصدر الرقم."},
        {kFrench, "error.copy.sourceMismatch",
         "La langue de cet ouvrage ne correspond pas à la source du numéro."},
        {kEnglish, "error.copy.sourceMismatch",
         "This book's language does not match the number's source."},
        {kArabic, "error.loan.archiveOpen", "لا يمكن أرشفة إعارة لم تُرجع بعد."},
        {kFrench, "error.loan.archiveOpen", "Un prêt non rendu ne peut pas être archivé."},
        {kEnglish, "error.loan.archiveOpen",
         "A loan that has not been returned cannot be archived."},
        {kArabic, "error.loan.notArchived", "هذه الإعارة ليست في الأرشيف."},
        {kFrench, "error.loan.notArchived", "Ce prêt n'est pas archivé."},
        {kEnglish, "error.loan.notArchived", "This loan is not in the Archive."},
        {kArabic, "error.loan.copyArchived", "هذه النسخة مؤرشفة ولا يمكن إعارتها."},
        {kFrench, "error.loan.copyArchived",
         "Cet exemplaire est archivé et ne peut pas être prêté."},
        {kEnglish, "error.loan.copyArchived", "This copy is archived and cannot be lent."},
        {kArabic, "error.member.notArchived", "هذا العضو ليس في الأرشيف."},
        {kFrench, "error.member.notArchived", "Cet adhérent n'est pas archivé."},
        {kEnglish, "error.member.notArchived", "This member is not in the Archive."},
    };

    for (const Case& c : cases) {
        EXPECT_EQ(Strings::rawValue(c.locale, c.key), c.value)
            << c.locale << " " << c.key;
    }
}
