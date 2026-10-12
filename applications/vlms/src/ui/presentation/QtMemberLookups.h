#pragma once

#include "ui/presentation/MemberLookups.h"

namespace VLMS::Repositories {
class MemberRepository;
}

namespace VLMS {

class QtMemberLookups final : public MemberLookups {
public:
    explicit QtMemberLookups(Repositories::MemberRepository& members);

    QString suggestMembershipNumber() const override;
    QStringList cities(Repositories::ArchiveScope scope =
                           Repositories::ArchiveScope::Live) const override;
    QString resolveImagePath(const QString& storedPath) const override;

private:
    Repositories::MemberRepository& m_members;
};

}  // namespace VLMS
