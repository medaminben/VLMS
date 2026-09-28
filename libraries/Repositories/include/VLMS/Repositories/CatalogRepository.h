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

    [[nodiscard]] Core::Result<std::vector<BookRecord>> listBooks(const BookQuery& query) const;
    [[nodiscard]] Core::Result<int> rankOfBook(std::int64_t id, const BookQuery& query) const;
    [[nodiscard]] Core::Result<int> countBooks(const BookQuery& query) const;
    [[nodiscard]] Core::Result<BookRecord> getBook(std::int64_t id) const;

    [[nodiscard]] Core::Result<std::int64_t> createBook(const BookInput& input);
    [[nodiscard]] Core::Status updateBook(std::int64_t id, const BookInput& input);
    [[nodiscard]] Core::Status deleteBook(std::int64_t id);
    [[nodiscard]] Core::Status archiveBook(std::int64_t id);
    /// The same answer archiveBook gives before it writes: open loans, or no
    /// live row. Read-only, and it does not open a transaction.
    [[nodiscard]] Core::Status canArchiveBook(std::int64_t id) const;
    [[nodiscard]] Core::Status restoreBook(std::int64_t id);
    /// Destroys an archived title that has no copies at all. Takes its cover
    /// folder with it.
    [[nodiscard]] Core::Status purgeBook(std::int64_t id);
    /// The same answer purgeBook gives before it writes.
    [[nodiscard]] Core::Status canPurgeBook(std::int64_t id) const;
    /// True when any live copy of the book is out on an unreturned loan. The
    /// Catalogue asks before it confirms a delete; archiveBook asks again.
    [[nodiscard]] Core::Result<bool> bookHasOpenLoans(std::int64_t id) const;
    [[nodiscard]] Core::Status restoreCopy(std::int64_t copyId);
    [[nodiscard]] Core::Status purgeCopy(std::int64_t copyId);
    /// The same answer purgeCopy gives before it writes.
    [[nodiscard]] Core::Status canPurgeCopy(std::int64_t copyId) const;
    [[nodiscard]] static std::string copySourceForLanguage(const std::string& language);
    [[nodiscard]] Core::Result<std::vector<BookCopyRecord>> listCopyRows(
        const CopyQuery& query) const;
    [[nodiscard]] Core::Result<int> countCopyRows(const CopyQuery& query) const;
    [[nodiscard]] Core::Status setCoverImage(std::int64_t bookId,
                                                   const std::string& sourceFilePath);

    [[nodiscard]] Core::Result<std::int64_t> saveNewBook(const BookWrite& write);
    [[nodiscard]] Core::Status saveExistingBook(std::int64_t id, const BookWrite& write);

    [[nodiscard]] Core::Result<std::vector<BookCopyRecord>> listCopies(std::int64_t bookId) const;
    [[nodiscard]] Core::Status saveCopies(std::int64_t bookId,
                                                const std::vector<BookCopyInput>& copies);
    void suggestCopyIdentifiers(const std::string& language,
                                std::string* outSource,
                                std::string* outLocalId,
                                std::string* outGlobalCopyId) const;

    [[nodiscard]] Core::Result<std::vector<std::string>> listFreeLocalNumbers(
        const std::string& source,
        int limit) const;

    [[nodiscard]] Core::Result<std::vector<CategoryRecord>> listCategories() const;
    [[nodiscard]] Core::Result<std::vector<CategoryRecord>> listAllCategories() const;
    [[nodiscard]] Core::Result<std::vector<LanguageRecord>> listBookLanguages(ArchiveScope scope = ArchiveScope::Live) const;
    [[nodiscard]] Core::Result<std::int64_t> createCategory(const std::string& code,
                                                                  const std::string& label);
    [[nodiscard]] Core::Status updateCategory(std::int64_t id,
                                                    const std::string& code,
                                                    const std::string& label);
    [[nodiscard]] Core::Status deleteCategory(std::int64_t id);

    [[nodiscard]] Core::Result<std::vector<std::string>> listAuthorNames() const;
    [[nodiscard]] Core::Result<std::vector<std::string>> listPublisherNames() const;

    [[nodiscard]] std::string resolveCoverPath(const std::string& storedPath) const;

private:
    [[nodiscard]] Core::Result<std::int64_t> insertBookRow(const BookInput& input);
    [[nodiscard]] Core::Status applyBookFields(std::int64_t id, const BookInput& input);
    [[nodiscard]] Core::Status applyCoverImage(std::int64_t bookId,
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
