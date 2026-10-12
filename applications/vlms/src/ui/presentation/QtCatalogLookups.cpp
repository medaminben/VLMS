#include "ui/presentation/QtCatalogLookups.h"

#include "QtBridge.h"

#include <VLMS/Repositories/CatalogRepository.h>

namespace VLMS {

QtCatalogLookups::QtCatalogLookups(Repositories::CatalogRepository& catalog)
    : m_catalog(catalog)
{
}

QStringList QtCatalogLookups::authorNames() const
{
    const auto names = m_catalog.listAuthorNames();
    return names ? qsl(names.value()) : QStringList();
}

QStringList QtCatalogLookups::publisherNames() const
{
    const auto names = m_catalog.listPublisherNames();
    return names ? qsl(names.value()) : QStringList();
}

std::vector<Repositories::CategoryRecord> QtCatalogLookups::categories() const
{
    // Book editor combo needs every category, including ones with no books yet
    // (same as the previous direct listAllCategories call).
    const auto categories = m_catalog.listAllCategories();
    return categories ? categories.value() : std::vector<Repositories::CategoryRecord>{};
}

std::vector<Repositories::LanguageRecord> QtCatalogLookups::bookLanguages(
    Repositories::ArchiveScope scope) const
{
    const auto languages = m_catalog.listBookLanguages(scope);
    return languages ? languages.value() : std::vector<Repositories::LanguageRecord>{};
}

QStringList QtCatalogLookups::freeLocalNumbers(const QString& source, int limit) const
{
    const auto numbers = m_catalog.listFreeLocalNumbers(ss(source), limit);
    return numbers ? qsl(numbers.value()) : QStringList();
}

void QtCatalogLookups::suggestCopyIdentifiers(const QString& language,
                                              QString* outSource,
                                              QString* outLocalId,
                                              QString* outGlobalCopyId) const
{
    std::string source;
    std::string localId;
    std::string globalCopyId;
    m_catalog.suggestCopyIdentifiers(ss(language), &source, &localId, &globalCopyId);
    if (outSource != nullptr) {
        *outSource = qs(source);
    }
    if (outLocalId != nullptr) {
        *outLocalId = qs(localId);
    }
    if (outGlobalCopyId != nullptr) {
        *outGlobalCopyId = qs(globalCopyId);
    }
}

std::vector<Repositories::BookCopyRecord> QtCatalogLookups::copiesForBook(std::int64_t bookId) const
{
    const auto copies = m_catalog.listCopies(bookId);
    return copies ? copies.value() : std::vector<Repositories::BookCopyRecord>{};
}

QString QtCatalogLookups::resolveCoverPath(const QString& storedPath) const
{
    return qs(m_catalog.resolveCoverPath(ss(storedPath)));
}

std::vector<Repositories::BookRecord> QtCatalogLookups::booksForCopySource(const QString& search,
                                                                             const QString& source) const
{
    Repositories::BookQuery query;
    query.search = ss(search);
    query.limit = 200;
    const auto books = m_catalog.listBooks(query);
    std::vector<Repositories::BookRecord> matching;
    if (!books) {
        return matching;
    }
    const std::string wanted = ss(source);
    for (const Repositories::BookRecord& book : books.value()) {
        if (Repositories::CatalogRepository::copySourceForLanguage(book.language) == wanted) {
            matching.push_back(book);
        }
    }
    return matching;
}

}  // namespace VLMS
