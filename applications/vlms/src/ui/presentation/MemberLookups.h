#pragma once

#include <VLMS/Repositories/ArchiveTypes.h>

#include <QString>
#include <QStringList>

namespace VLMS {

/// Read-only member helpers for form dialogs. Implementations talk to
/// MemberRepository; dialogs must not hold the repository themselves.
class MemberLookups {
public:
    virtual ~MemberLookups() = default;

    virtual QString suggestMembershipNumber() const = 0;
    virtual QStringList cities(Repositories::ArchiveScope scope =
                                   Repositories::ArchiveScope::Live) const = 0;
    virtual QString resolveImagePath(const QString& storedPath) const = 0;
};

}  // namespace VLMS
