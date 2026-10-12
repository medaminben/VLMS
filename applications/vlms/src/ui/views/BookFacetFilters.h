#pragma once

#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/CatalogTypes.h>

#include "ui/presentation/ReadSources.h"

#include <QStringList>
#include <QWidget>

namespace VLMS {

class FacetList;

/**
 * The Catalogue's filter lists: language, category, cover.
 *
 * The Archive reuses it for archived titles and copies. `scope` decides which
 * books the language list is read from; the per-row counts are the live
 * catalogue's, so they are shown only for ArchiveScope::Live.
 */
class BookFacetFilters final : public QWidget {
    Q_OBJECT

public:
    BookFacetFilters(BookFacetSource& source, Repositories::ArchiveScope scope, QWidget* parent = nullptr);

    /// Re-reads the labels, languages and categories; keeps the picks.
    void refresh();
    [[nodiscard]] QStringList languages() const;
    [[nodiscard]] QStringList categoryCodes() const;
    [[nodiscard]] Repositories::CoverFilter coverFilter() const;
    /// Puts every list back on All, without emitting changed().
    void reset();
    /// Which books the language list is read from on the next refresh.
    void setScope(Repositories::ArchiveScope scope) { m_scope = scope; }

signals:
    void changed();

private:
    BookFacetSource& m_source;
    Repositories::ArchiveScope m_scope;
    FacetList* m_language = nullptr;
    FacetList* m_category = nullptr;
    FacetList* m_cover = nullptr;
};

}  // namespace VLMS
