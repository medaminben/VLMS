#pragma once

#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Core/Result.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VLMS::Database {
class SqliteSession;
}  // namespace VLMS::Database

namespace VLMS::Repositories {

class BookCopyStore;
class CategoryStore;
class NamedEntityStore;

class CatalogRepository {
public:
    CatalogRepository(Database::SqliteSession& session, std::string resourcesDirectory);
    ~CatalogRepository();

    CatalogRepository(const CatalogRepository&) = delete;
    CatalogRepository& operator=(const CatalogRepository&) = delete;

    [[nodiscard]] VLMS::Result<std::vector<BookRecord>> listBooks(const BookQuery& query) const;
    [[nodiscard]] VLMS::Result<int> rankOfBook(std::int64_t id, const BookQuery& query) const;
    [[nodiscard]] VLMS::Result<int> countBooks(const BookQuery& query) const;
    [[nodiscard]] VLMS::Result<BookRecord> getBook(std::int64_t id) const;

    [[nodiscard]] VLMS::Result<std::int64_t> createBook(const BookInput& input);
    [[nodiscard]] VLMS::Status updateBook(std::int64_t id, const BookInput& input);
    [[nodiscard]] VLMS::Status deleteBook(std::int64_t id);
    [[nodiscard]] VLMS::Status archiveBook(std::int64_t id);
    /// The same answer archiveBook gives before it writes: open loans, or no
    /// live row. Read-only, and it does not open a transaction.
    [[nodiscard]] VLMS::Status canArchiveBook(std::int64_t id) const;
    [[nodiscard]] VLMS::Status restoreBook(std::int64_t id);
    /// Destroys an archived title that has no copies at all. Takes its cover
    /// folder with it.
    [[nodiscard]] VLMS::Status purgeBook(std::int64_t id);
    /// The same answer purgeBook gives before it writes.
    [[nodiscard]] VLMS::Status canPurgeBook(std::int64_t id) const;
    /// True when any live copy of the book is out on an unreturned loan. The
    /// Catalogue asks before it confirms a delete; archiveBook asks again.
    [[nodiscard]] VLMS::Result<bool> bookHasOpenLoans(std::int64_t id) const;
    [[nodiscard]] VLMS::Status restoreCopy(std::int64_t copyId);
    [[nodiscard]] VLMS::Status purgeCopy(std::int64_t copyId);
    /// The same answer purgeCopy gives before it writes.
    [[nodiscard]] VLMS::Status canPurgeCopy(std::int64_t copyId) const;
    [[nodiscard]] static std::string copySourceForLanguage(const std::string& language);
    [[nodiscard]] VLMS::Result<std::vector<BookCopyRecord>> listCopyRows(
        const CopyQuery& query) const;
    [[nodiscard]] VLMS::Result<int> countCopyRows(const CopyQuery& query) const;
    [[nodiscard]] VLMS::Status setCoverImage(std::int64_t bookId,
                                                   const std::string& sourceFilePath);

    [[nodiscard]] VLMS::Result<std::int64_t> saveNewBook(const BookWrite& write);
    [[nodiscard]] VLMS::Status saveExistingBook(std::int64_t id, const BookWrite& write);

    [[nodiscard]] VLMS::Result<std::vector<BookCopyRecord>> listCopies(std::int64_t bookId) const;
    [[nodiscard]] VLMS::Status saveCopies(std::int64_t bookId,
                                                const std::vector<BookCopyInput>& copies);
    void suggestCopyIdentifiers(const std::string& language,
                                std::string* outSource,
                                std::string* outLocalId,
                                std::string* outGlobalCopyId) const;

    [[nodiscard]] VLMS::Result<std::vector<std::string>> listFreeLocalNumbers(
        const std::string& source,
        int limit) const;

    [[nodiscard]] VLMS::Result<std::vector<CategoryRecord>> listCategories() const;
    [[nodiscard]] VLMS::Result<std::vector<CategoryRecord>> listAllCategories() const;
    [[nodiscard]] VLMS::Result<std::vector<LanguageRecord>> listBookLanguages(ArchiveScope scope = ArchiveScope::Live) const;
    [[nodiscard]] VLMS::Result<std::int64_t> createCategory(const std::string& code,
                                                                  const std::string& label);
    [[nodiscard]] VLMS::Status updateCategory(std::int64_t id,
                                                    const std::string& code,
                                                    const std::string& label);
    [[nodiscard]] VLMS::Status deleteCategory(std::int64_t id);

    [[nodiscard]] VLMS::Result<std::vector<std::string>> listAuthorNames() const;
    [[nodiscard]] VLMS::Result<std::vector<std::string>> listPublisherNames() const;

    [[nodiscard]] std::string resolveCoverPath(const std::string& storedPath) const;

private:
    [[nodiscard]] VLMS::Result<std::int64_t> insertBookRow(const BookInput& input);
    [[nodiscard]] VLMS::Status applyBookFields(std::int64_t id, const BookInput& input);
    [[nodiscard]] VLMS::Status applyCoverImage(std::int64_t bookId,
                                                     const std::string& sourceFilePath);
    [[nodiscard]] bool archivedBookHasKey(const BookInput& input,
                                          std::int64_t authorId,
                                          std::int64_t publisherId) const;

    Database::SqliteSession& m_session;
    std::string m_resourcesDirectory;
    std::unique_ptr<NamedEntityStore> m_names;
    std::unique_ptr<BookCopyStore> m_copies;
    std::unique_ptr<CategoryStore> m_categories;
};

}  // namespace VLMS::Repositories
