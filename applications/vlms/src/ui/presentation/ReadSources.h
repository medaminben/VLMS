#pragma once

#include <VLMS/Core/Result.h>
#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Repositories/MetricsTypes.h>

#include <string>
#include <vector>

namespace VLMS {

namespace Repositories {
class CatalogRepository;
class MemberRepository;
class MetricsRepository;
}  // namespace Repositories

/// Values for the book facet lists. Views read through this, never through
/// CatalogRepository, and show the error when a read fails.
class BookFacetSource {
public:
    virtual ~BookFacetSource() = default;
    [[nodiscard]] virtual Core::Result<std::vector<Repositories::LanguageRecord>> bookLanguages(
        Repositories::ArchiveScope scope) const = 0;
    [[nodiscard]] virtual Core::Result<std::vector<Repositories::CategoryRecord>> categories() const = 0;
};

/// Values for the member facet lists.
class MemberFacetSource {
public:
    virtual ~MemberFacetSource() = default;
    [[nodiscard]] virtual Core::Result<std::vector<std::string>> inscriptionYears(
        Repositories::ArchiveScope scope) const = 0;
    [[nodiscard]] virtual Core::Result<std::vector<std::string>> cities(
        Repositories::ArchiveScope scope) const = 0;
};

/// The numbers behind the metrics page.
class MetricsSource {
public:
    virtual ~MetricsSource() = default;
    [[nodiscard]] virtual Core::Result<Repositories::LibraryMetrics> fetch() const = 0;
};

class RepositoryBookFacets final : public BookFacetSource {
public:
    explicit RepositoryBookFacets(Repositories::CatalogRepository& catalog) : m_catalog(catalog) {}
    Core::Result<std::vector<Repositories::LanguageRecord>> bookLanguages(
        Repositories::ArchiveScope scope) const override;
    Core::Result<std::vector<Repositories::CategoryRecord>> categories() const override;

private:
    Repositories::CatalogRepository& m_catalog;
};

class RepositoryMemberFacets final : public MemberFacetSource {
public:
    explicit RepositoryMemberFacets(Repositories::MemberRepository& members) : m_members(members) {}
    Core::Result<std::vector<std::string>> inscriptionYears(
        Repositories::ArchiveScope scope) const override;
    Core::Result<std::vector<std::string>> cities(Repositories::ArchiveScope scope) const override;

private:
    Repositories::MemberRepository& m_members;
};

class RepositoryMetrics final : public MetricsSource {
public:
    explicit RepositoryMetrics(Repositories::MetricsRepository& metrics) : m_metrics(metrics) {}
    Core::Result<Repositories::LibraryMetrics> fetch() const override;

private:
    Repositories::MetricsRepository& m_metrics;
};

}  // namespace VLMS
