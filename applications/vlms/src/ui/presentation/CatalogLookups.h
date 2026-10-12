#pragma once

#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/CatalogTypes.h>

#include <QString>
#include <QStringList>

#include <cstdint>
#include <vector>

namespace VLMS {

/// Read-only catalog helpers for form dialogs. Implementations talk to
/// CatalogRepository; dialogs must not hold the repository themselves.
class CatalogLookups {
public:
    virtual ~CatalogLookups() = default;

    virtual QStringList authorNames() const = 0;
    virtual QStringList publisherNames() const = 0;
    /// All categories (including unused), for the book editor combo.
    virtual std::vector<Repositories::CategoryRecord> categories() const = 0;
    virtual std::vector<Repositories::LanguageRecord> bookLanguages(
        Repositories::ArchiveScope scope = Repositories::ArchiveScope::Live) const = 0;
    virtual QStringList freeLocalNumbers(const QString& source, int limit) const = 0;
    virtual void suggestCopyIdentifiers(const QString& language,
                                        QString* outSource,
                                        QString* outLocalId,
                                        QString* outGlobalCopyId) const = 0;
    /// Live copies for a book (empty on error / unknown id).
    virtual std::vector<Repositories::BookCopyRecord> copiesForBook(std::int64_t bookId) const = 0;
    virtual QString resolveCoverPath(const QString& storedPath) const = 0;
    /// Live books whose language numbers copies in `source` (Archive -> Reuse
    /// number), first 200 matching `search`. Empty on error.
    virtual std::vector<Repositories::BookRecord> booksForCopySource(const QString& search,
                                                                     const QString& source) const = 0;
};

}  // namespace VLMS
