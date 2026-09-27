#include "ui/members/MemberFacetFilters.h"

#include <VLMS/Core/Strings.h>

#include "QtBridge.h"
#include "ui/FacetList.h"
#include "ui/UiHelpers.h"

#include <QFrame>
#include <QVBoxLayout>

namespace VLMS {
namespace {

constexpr int kValueListRows = 8;

QList<FacetList::Entry> codedEntries(const std::vector<std::string>& codes,
                                     std::string (*labelFor)(std::string_view))
{
    QList<FacetList::Entry> entries;
    for (const std::string& code : codes) {
        entries.append({qs(labelFor(code)), qs(code)});
    }
    return entries;
}

QStringList codesOf(const FacetList* list)
{
    return list == nullptr ? QStringList() : list->selectedCodes();
}

}  // namespace

MemberFacetFilters::MemberFacetFilters(MemberRepository& repository,
                                       const ArchiveScope valueScope,
                                       QWidget* parent)
    : QScrollArea(parent),
      m_repository(repository),
      m_valueScope(valueScope)
{
    setObjectName(QStringLiteral("memberFilters"));
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* inner = new QWidget(this);
    auto* layout = new QVBoxLayout(inner);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    const auto add = [this, inner, layout](const char* objectName) {
        auto* list = new FacetList(QString::fromLatin1(objectName), true, inner);
        connect(list, &FacetList::selectionEdited, this, &MemberFacetFilters::changed);
        layout->addWidget(list);
        return list;
    };
    m_status = add("statusFilter");
    m_sex = add("sexFilter");
    m_year = add("yearFilter");
    m_ageGroup = add("ageGroupFilter");
    m_city = add("cityFilter");
    layout->addStretch(1);

    setWidget(inner);
}

void MemberFacetFilters::refresh()
{
    m_status->setEntries(T("members.allStatuses"),
                         codedEntries(MemberRepository::statusCodes(),
                                      Strings::memberStatusLabel));
    m_status->fitRows(m_status->count());
    m_sex->setEntries(T("members.allSexes"),
                      codedEntries(MemberRepository::sexCodes(), Strings::memberSexLabel));
    m_sex->fitRows(m_sex->count());
    m_ageGroup->setEntries(T("members.allAgeGroups"),
                           codedEntries(MemberRepository::ageGroupCodes(),
                                        Strings::memberAgeGroupLabel));
    m_ageGroup->fitRows(m_ageGroup->count());

    const auto fillValues = [this](FacetList* list, const QString& allLabel,
                                   const Result<std::vector<std::string>>& values) {
        if (!values) {
            showRepoError(this, values.error());
            return;
        }
        QList<FacetList::Entry> entries;
        for (const std::string& value : values.value()) {
            entries.append({qs(value), qs(value)});
        }
        list->setEntries(allLabel, entries);
        list->fitRows(kValueListRows);
    };
    if (m_loanYearSource) {
        fillValues(m_year, T("circulation.allLoanYears"), m_loanYearSource());
    } else {
        fillValues(m_year, T("members.allYears"),
                   m_repository.listInscriptionYears(m_valueScope));
    }
    fillValues(m_city, T("members.allCities"), m_repository.listCities(m_valueScope));
}

MemberFacets MemberFacetFilters::facets() const
{
    MemberFacets facets;
    facets.statuses = svl(codesOf(m_status));
    facets.sexes = svl(codesOf(m_sex));
    if (!m_loanYearSource) {
        facets.inscriptionYears = svl(codesOf(m_year));
    }
    facets.ageGroups = svl(codesOf(m_ageGroup));
    facets.cities = svl(codesOf(m_city));
    return facets;
}

void MemberFacetFilters::useLoanYears(YearSource source)
{
    m_loanYearSource = std::move(source);
}

QStringList MemberFacetFilters::loanYears() const
{
    return m_loanYearSource ? codesOf(m_year) : QStringList();
}

void MemberFacetFilters::reset()
{
    for (FacetList* list : {m_status, m_sex, m_year, m_ageGroup, m_city}) {
        list->selectAll();
    }
}

}  // namespace VLMS
