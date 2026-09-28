#include "ui/catalog/BookFacetFilters.h"

#include <VLMS/Core/Strings.h>

#include "QtBridge.h"
#include "ui/FacetList.h"
#include "ui/UiHelpers.h"

#include <QVBoxLayout>

namespace VLMS {
namespace {

constexpr int kLanguageStretch = 1;
constexpr int kCategoryStretch = 2;

QString withCount(const QString& label, const int count, const bool showCount)
{
    return showCount ? QStringLiteral("%1 (%2)").arg(label).arg(count) : label;
}

}  // namespace

BookFacetFilters::BookFacetFilters(Repositories::CatalogRepository& repository,
                                   const Repositories::ArchiveScope scope,
                                   QWidget* parent)
    : QWidget(parent),
      m_repository(repository),
      m_scope(scope)
{
    setObjectName(QStringLiteral("bookFilters"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    m_language = new FacetList(QStringLiteral("languageFilter"), true, this);
    m_category = new FacetList(QStringLiteral("categoryFilter"), true, this);
    m_cover = new FacetList(QStringLiteral("coverFilter"), false, this);
    for (FacetList* list : {m_language, m_category, m_cover}) {
        connect(list, &FacetList::selectionEdited, this, &BookFacetFilters::changed);
    }
    layout->addWidget(m_language, kLanguageStretch);
    layout->addWidget(m_category, kCategoryStretch);
    layout->addWidget(m_cover, 0);
}

void BookFacetFilters::refresh()
{
    const bool showCounts = m_scope == Repositories::ArchiveScope::Live;

    if (const auto languages = m_repository.listBookLanguages(m_scope); !languages) {
        showRepoError(this, languages.error());
    } else {
        QList<FacetList::Entry> entries;
        for (const Repositories::LanguageRecord& language : languages.value()) {
            entries.append({withCount(qs(Core::Strings::bookLanguageLabel(language.code)),
                                      language.bookCount, showCounts),
                            qs(language.code)});
        }
        m_language->setEntries(T("catalog.allLanguages"), entries);
    }

    if (const auto categories = m_repository.listAllCategories(); !categories) {
        showRepoError(this, categories.error());
    } else {
        QList<FacetList::Entry> entries;
        for (const Repositories::CategoryRecord& category : categories.value()) {
            const QString label = qs(category.label.empty() ? category.code : category.label);
            entries.append({withCount(label, category.bookCount, showCounts), qs(category.code)});
        }
        m_category->setEntries(T("catalog.allCategories"), entries);
    }

    m_cover->setEntries(T("catalog.allCovers"),
                        {{T("catalog.withCover"),
                          QString::number(static_cast<int>(Repositories::CoverFilter::WithCover))},
                         {T("catalog.withoutCover"),
                          QString::number(static_cast<int>(Repositories::CoverFilter::WithoutCover))}});
    m_cover->fitRows(m_cover->count());
}

QStringList BookFacetFilters::languages() const
{
    return m_language->selectedCodes();
}

QStringList BookFacetFilters::categoryCodes() const
{
    return m_category->selectedCodes();
}

Repositories::CoverFilter BookFacetFilters::coverFilter() const
{
    const QStringList codes = m_cover->selectedCodes();
    return codes.isEmpty() ? Repositories::CoverFilter::All : static_cast<Repositories::CoverFilter>(codes.first().toInt());
}

void BookFacetFilters::reset()
{
    for (FacetList* list : {m_language, m_category, m_cover}) {
        list->selectAll();
    }
}

}  // namespace VLMS
