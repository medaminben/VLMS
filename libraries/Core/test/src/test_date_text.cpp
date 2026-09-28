#include <VLMS/Core/DateText.h>

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

using namespace VLMS;

using VLMS::DateText::normalizePublicationDate;

/**
 * The publication date normaliser.
 *
 * Every input below is a real shape from the production catalog -- 3067 rows,
 * 30 distinct shapes -- rather than an invented one. The counts in the
 * migration and realdb tests depend on this function's exact coverage, so the
 * boundary between "normalised" and "left alone" is written out here case by
 * case: it is a decision about a librarian's data, not an implementation
 * detail.
 */

TEST(test_core_DateText, AlreadyReducedIsoIsUnchanged)
{
    for (const char* input : {"2014", "1999-09", "2003-03-18", "2024-02-29"}) {
        SCOPED_TRACE(input);
        EXPECT_EQ(normalizePublicationDate(input), input);
    }
}

TEST(test_core_DateText, MonthNameFormsNormalise)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        // Month-day-year, the largest normalisable group at 545 rows.
        {"Nov 06, 1996", "1996-11-06"},
        {"February 6, 2007", "2007-02-06"},
        {"September 1 2012", "2012-09-01"},
        {"October , 18 2022", "2022-10-18"},

        // Day-month-year. The order does not have to be detected: with a month
        // name present, a four-digit number is the year and a short one is the day.
        {"20 May 2013", "2013-05-20"},
        {"30-Jun-2009", "2009-06-30"},
        {"8Jan2009", "2009-01-08"},
        {"23rd Dec 2010", "2010-12-23"},
        {"11 December 2013", "2013-12-11"},

        // Month and year only: reduced precision, because there is no day in the
        // source and inventing the 1st would be inventing data.
        {"February 2001", "2001-02"},
        {"May 1998", "1998-05"},
        {"2011 March", "2011-03"},
        {"2002 September", "2002-09"},

        {"nOVEMBER 1987", "1987-11"},
        {"  May 1995  ", "1995-05"},
    };
    for (const auto& [input, expected] : cases) {
        SCOPED_TRACE(input);
        EXPECT_EQ(normalizePublicationDate(input), expected);
    }
}

TEST(test_core_DateText, NumericYearMonthNormalises)
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        // '2000 03' is unambiguous only because the four-digit part can only be a
        // year -- which is exactly why '1/7/2021' below is not.
        {"2000 03", "2000-03"},
        {"2004 6", "2004-06"},
        {"1969 12", "1969-12"},
    };
    for (const auto& [input, expected] : cases) {
        SCOPED_TRACE(input);
        EXPECT_EQ(normalizePublicationDate(input), expected);
    }
}

TEST(test_core_DateText, AmbiguousInputIsLeftVerbatim)
{
    const std::vector<std::string> cases = {
        // MARC notation for an uncertain decade. Not corruption -- a cataloguer
        // wrote it deliberately, and it says something a date cannot.
        "201u",
        "202z",
        "195?",
        "xxxx",

        // A Hijri year. Rewriting it as Gregorian would be a translation, not a
        // normalisation.
        "1432\xD9\x87\xD8\xAC\xD8\xB1\xD9\x8A\xD8\xA7\xD9\x8B",

        // All-numeric with no four-digit-first shape: nothing in the value says
        // whether it means January or July, so it keeps its wording.
        "1/7/2021",
        "17/6/2008",
        "29/07/2021",
        "05.09.2018",
        "12-01-2009",

        // A month name and one number: day or year, no way to tell.
        "Jan-21",

        // Months in languages the table does not cover. Adding them is a data
        // question, and guessing wrong would rewrite a value silently.
        "Marzo 2009",
        "20 de mar\xC3\xA7o de 2021",

        "May June 1998",
        "March",
        "2001-13",
    };
    for (const std::string& input : cases) {
        SCOPED_TRACE(input);
        EXPECT_EQ(normalizePublicationDate(input), input);
    }
}

TEST(test_core_DateText, ImpossibleDatesAreLeftVerbatim)
{
    // The SQLite trap, from the other side. date('2024-02-30') is not NULL --
    // it is '2024-03-01' -- so a normaliser that leant on date() would quietly
    // move this book's publication forward by a day. Returning it unchanged
    // leaves a wrong value visible instead of a wrong value hidden.
    for (const char* input : {"2024-02-30", "1900-02-29", "February 30, 2024", "Jan 32, 2001"}) {
        SCOPED_TRACE(input);
        EXPECT_EQ(normalizePublicationDate(input), input);
    }
}

TEST(test_core_DateText, NormalisationIsIdempotent)
{
    for (const char* input : {"Nov 06, 1996", "February 2001", "2014", "1999-09", "201u",
                              "1/7/2021", "30-Jun-2009", "2000 03"}) {
        SCOPED_TRACE(input);

        // The migration writes on open and createBook writes on every save, so a
        // value passes through this function an unbounded number of times. If a
        // second pass moved it, the catalog would drift a little on every launch.
        const std::string once = normalizePublicationDate(input);
        EXPECT_EQ(normalizePublicationDate(once), once);
    }
}

TEST(test_core_DateText, BlankInputIsReturnedAsGiven)
{
    EXPECT_EQ(normalizePublicationDate({}), std::string());
    EXPECT_EQ(normalizePublicationDate(""), std::string());
    EXPECT_EQ(normalizePublicationDate("   "), std::string("   "));
}
