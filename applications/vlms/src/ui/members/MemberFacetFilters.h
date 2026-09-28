#pragma once

#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QScrollArea>
#include <QStringList>

#include <functional>
#include <string>
#include <vector>

namespace VLMS {

class FacetList;

/**
 * The Members page's filter lists: status, sex, inscription year, age group,
 * city. Each list opens with its own "All ..." row, which names it, so the
 * lists carry no headings.
 *
 * The Circulation and Archive pages reuse it to filter loans by borrower.
 * `valueScope` decides which members the year and city lists are read from.
 */
class MemberFacetFilters final : public QScrollArea {
    Q_OBJECT

public:
    MemberFacetFilters(MemberRepository& repository,
                       ArchiveScope valueScope,
                       QWidget* parent = nullptr);

    /// Re-reads the labels and the year and city values; keeps the picks.
    void refresh();
    [[nodiscard]] MemberFacets facets() const;
    /// Puts every list back on All, without emitting changed().
    void reset();
    /// Which members the year and city lists are read from on the next refresh.
    void setValueScope(ArchiveScope scope) { m_valueScope = scope; }

    using YearSource = std::function<Result<std::vector<std::string>>()>;
    /// On a loan list the year is the year the loan was made, not the year
    /// the borrower registered. From then on the year list is read from
    /// `source`, and its picks come back from loanYears() instead of facets().
    void useLoanYears(YearSource source);
    [[nodiscard]] QStringList loanYears() const;

signals:
    void changed();

private:
    MemberRepository& m_repository;
    ArchiveScope m_valueScope;
    YearSource m_loanYearSource;
    FacetList* m_status = nullptr;
    FacetList* m_sex = nullptr;
    FacetList* m_year = nullptr;
    FacetList* m_ageGroup = nullptr;
    FacetList* m_city = nullptr;
};

}  // namespace VLMS
