#pragma once

#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Core/Result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace VLMS {
class SqliteSession;
}

namespace VLMS::Repositories {

class BookCopyStore {
public:
    explicit BookCopyStore(VLMS::SqliteSession& session);

    [[nodiscard]] VLMS::Result<std::vector<BookCopyRecord>> listCopies(
        std::int64_t bookId) const;
    [[nodiscard]] VLMS::Result<std::vector<BookCopyRecord>> listCopyRows(
        const CopyQuery& query) const;
    [[nodiscard]] VLMS::Result<int> countCopyRows(const CopyQuery& query) const;
    [[nodiscard]] VLMS::Status saveCopies(std::int64_t bookId,
                                                const std::vector<BookCopyInput>& copies);
    /// Same work as saveCopies, but the caller already holds the transaction.
    [[nodiscard]] VLMS::Status applyCopies(std::int64_t bookId,
                                                 const std::vector<BookCopyInput>& copies);
    [[nodiscard]] VLMS::Status addCopies(std::int64_t bookId,
                                               const std::string& language,
                                               int count);

    /// "arabic" for books in Arabic, "foreign" for every other language: the
    /// inventory a book's copies are numbered in.
    [[nodiscard]] static std::string sourceForLanguage(const std::string& language);

    /// Clears one copy's archive flag (and its book's, if archived). A copy
    /// that gave its number away gets the next free one and a note. The
    /// caller holds the transaction.
    [[nodiscard]] VLMS::Status restoreCopy(std::int64_t copyId);

    /// Archive -> Reuse: frees the archived copy's number for a new row of
    /// `incoming` saved in the same transaction. The caller holds it.
    [[nodiscard]] VLMS::Status releaseArchivedNumber(
        std::int64_t copyId,
        const std::vector<BookCopyInput>& incoming,
        const std::string& bookLanguage);

    /// Destroys an archived copy that no loan names. Its local number becomes
    /// free again by the deletion itself -- free means a number no row holds,
    /// so there is no pool to write to. The caller holds the transaction.
    [[nodiscard]] VLMS::Status purgeCopy(std::int64_t copyId);
    /// The same answer purgeCopy gives before it writes. The caller may
    /// already hold the transaction; this does not open one.
    [[nodiscard]] VLMS::Status canPurgeCopy(std::int64_t copyId) const;

    void suggestCopyIdentifiers(const std::string& language,
                                std::string* outSource,
                                std::string* outLocalId,
                                std::string* outGlobalCopyId) const;

    /// The numbers in 1 .. MAX(local_id) for `source` that no copy holds,
    /// ascending, at most `limit` of them.
    [[nodiscard]] VLMS::Result<std::vector<std::string>> freeLocalNumbers(
        const std::string& source,
        int limit) const;

private:
    VLMS::SqliteSession& m_session;
};

}  // namespace VLMS::Repositories
