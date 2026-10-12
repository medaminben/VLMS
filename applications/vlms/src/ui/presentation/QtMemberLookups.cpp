#include "ui/presentation/QtMemberLookups.h"

#include "QtBridge.h"

#include <VLMS/Repositories/MemberRepository.h>

namespace VLMS {

QtMemberLookups::QtMemberLookups(Repositories::MemberRepository& members)
    : m_members(members)
{
}

QString QtMemberLookups::suggestMembershipNumber() const
{
    return qs(m_members.suggestMembershipNumber());
}

QStringList QtMemberLookups::cities(Repositories::ArchiveScope scope) const
{
    const auto cities = m_members.listCities(scope);
    return cities ? qsl(cities.value()) : QStringList();
}

QString QtMemberLookups::resolveImagePath(const QString& storedPath) const
{
    return qs(m_members.resolveImagePath(ss(storedPath)));
}

}  // namespace VLMS
