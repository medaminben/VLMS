#include "ui/presentation/ReadSources.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MetricsRepository.h>

namespace VLMS {

Core::Result<std::vector<Repositories::LanguageRecord>> RepositoryBookFacets::bookLanguages(
    Repositories::ArchiveScope scope) const
{
    return m_catalog.listBookLanguages(scope);
}

Core::Result<std::vector<Repositories::CategoryRecord>> RepositoryBookFacets::categories() const
{
    return m_catalog.listAllCategories();
}

Core::Result<std::vector<std::string>> RepositoryMemberFacets::inscriptionYears(
    Repositories::ArchiveScope scope) const
{
    return m_members.listInscriptionYears(scope);
}

Core::Result<std::vector<std::string>> RepositoryMemberFacets::cities(
    Repositories::ArchiveScope scope) const
{
    return m_members.listCities(scope);
}

Core::Result<Repositories::LibraryMetrics> RepositoryMetrics::fetch() const
{
    return m_metrics.fetchMetrics();
}

}  // namespace VLMS
