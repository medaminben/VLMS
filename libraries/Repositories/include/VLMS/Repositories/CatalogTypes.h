#pragma once

#include <cstdint>
#include <VLMS/Repositories/ArchiveTypes.h>
#include <string>
#include <vector>

namespace VLMS::Repositories {

struct BookRecord {
    std::int64_t id = 0;
    std::string title;
    std::string authorName;
    std::string publisherName;
    std::int64_t categoryId = 0;
    std::string categoryCode;
    std::string categoryLabel;
    std::string isbn;
    std::string publicationDate;
    std::string placeOfPublication;
    std::string pages;
    std::string dimensions;
    std::string language;
    std::string description;
    std::string coverImagePath;
    std::string subject;
    int totalCopies = 0;
    int availableCopies = 0;
    /// Local accession numbers of this book's copies, ascending numerically.
    /// Empty when the book has no copies in the queried scope.
    std::vector<std::string> localIds;
    /// The subset of `localIds` whose copy is out on an unreturned loan, same
    /// order. A subset rather than a flag per number: the numbers are unique
    /// within a book, so membership answers the question without an index to
    /// keep aligned with `localIds`.
    std::vector<std::string> localIdsOnLoan;
    /// The copy number a numeric search matched on, so the list can lead with
    /// it. Empty when the search was not a number, or did not match a copy.
    std::string matchedLocalId;
    std::string archivedAt;
};

struct BookInput {
    std::string title;
    std::string authorName;
    std::string publisherName;
    std::int64_t categoryId = 0;
    std::string isbn;
    std::string publicationDate;
    std::string placeOfPublication;
    std::string pages;
    std::string dimensions;
    std::string language;
    std::string description;
    int initialCopyCount = 1;
};

struct BookCopyRecord {
    std::int64_t id = 0;
    std::int64_t bookId = 0;
    std::string globalCopyId;
    std::string source;
    std::string localId;
    std::string centralId;
    std::string classification;
    std::string subject;
    std::string notes;
    std::string inventoryStatus;
    std::string compensation;
    std::string location;
    std::string indexCode;
    int sourceRow = 0;
    bool onLoan = false;
    std::string bookTitle;   // filled by the Archive copy list only
    std::string coverImagePath;  // the title's cover; Archive copy list only
    std::string archivedAt;
    /// Every loan row naming this copy, archived ones included. Filled by the
    /// Archive copy list only; the gate on permanent removal.
    int loanCount = 0;
};

struct BookCopyInput {
    std::int64_t id = 0;
    std::string globalCopyId;
    std::string source;
    std::string localId;
    std::string centralId;
    std::string classification;
    std::string subject;
    std::string notes;
    std::string inventoryStatus;
    std::string compensation;
    std::string location;
    std::string indexCode;
};

struct CategoryRecord {
    std::int64_t id = 0;
    std::string code;
    std::string label;
    int bookCount = 0;
};

struct LanguageRecord {
    std::string code;
    int bookCount = 0;
};

enum class CoverFilter {
    All,
    WithCover,
    WithoutCover,
};

namespace BookSort {
inline constexpr auto kTitle = "title";
inline constexpr auto kAuthor = "author";
inline constexpr auto kCategory = "category";
inline constexpr auto kLocalNumber = "localNumber";
inline constexpr auto kCopies = "copies";
inline constexpr auto kAvailable = "available";
inline constexpr auto kArchivedAt = "archivedAt";
}  // namespace BookSort

struct BookQuery {
    std::string search;
    std::vector<std::string> categoryCodes;
    std::vector<std::string> languages;
    CoverFilter coverFilter = CoverFilter::All;
    ArchiveScope archive = ArchiveScope::Live;
    int limit = 200;
    int offset = 0;
    std::string sortColumn;
    bool sortAscending = true;
};

namespace CopySort {
inline constexpr auto kLocalId = "localId";
inline constexpr auto kSource = "source";
inline constexpr auto kTitle = "title";
inline constexpr auto kArchivedAt = "archivedAt";
inline constexpr auto kLoans = "loans";
}  // namespace CopySort

struct CopyQuery {
    std::string search;
    /// Filters on the title a copy belongs to, as the Catalogue applies them.
    std::vector<std::string> categoryCodes;
    std::vector<std::string> languages;
    CoverFilter coverFilter = CoverFilter::All;
    ArchiveScope archive = ArchiveScope::Live;
    int limit = 200;
    int offset = 0;
    std::string sortColumn;
    bool sortAscending = true;
};

struct BookWrite {
    BookInput book;
    std::vector<BookCopyInput> copies;
    std::string coverSourcePath;
    /// Archive -> Reuse local number: the archived copy whose number one of
    /// `copies` takes. Released in the same transaction as the save; 0 = none.
    std::int64_t releaseFromCopyId = 0;
};

}  // namespace VLMS::Repositories
