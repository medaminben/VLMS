#pragma once

#include "ui/presentation/CatalogLookups.h"

namespace VLMS::Repositories {
class CatalogRepository;
}

namespace VLMS {

class QtCatalogLookups final : public CatalogLookups {
public:
    explicit QtCatalogLookups(Repositories::CatalogRepository& catalog);

    QStringList authorNames() const override;
    QStringList publisherNames() const override;
    std::vector<Repositories::CategoryRecord> categories() const override;
    std::vector<Repositories::LanguageRecord> bookLanguages(
        Repositories::ArchiveScope scope = Repositories::ArchiveScope::Live) const override;
    QStringList freeLocalNumbers(const QString& source, int limit) const override;
    void suggestCopyIdentifiers(const QString& language,
                                QString* outSource,
                                QString* outLocalId,
                                QString* outGlobalCopyId) const override;
    std::vector<Repositories::BookCopyRecord> copiesForBook(std::int64_t bookId) const override;
    QString resolveCoverPath(const QString& storedPath) const override;
    std::vector<Repositories::BookRecord> booksForCopySource(const QString& search,
                                                             const QString& source) const override;

private:
    Repositories::CatalogRepository& m_catalog;
};

}  // namespace VLMS
