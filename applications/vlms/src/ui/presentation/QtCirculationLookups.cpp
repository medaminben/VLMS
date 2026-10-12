#include "ui/presentation/QtCirculationLookups.h"

#include "QtBridge.h"

#include <VLMS/Repositories/CirculationRepository.h>

namespace VLMS {

QtCirculationLookups::QtCirculationLookups(Repositories::CirculationRepository& circulation)
    : m_circulation(circulation)
{
}

std::vector<Repositories::LoanMemberOption> QtCirculationLookups::borrowableMembers(
    const QString& search) const
{
    const auto members = m_circulation.listBorrowableMembers(ss(search));
    return members ? members.value() : std::vector<Repositories::LoanMemberOption>{};
}

std::vector<Repositories::LoanCopyOption> QtCirculationLookups::availableCopies(
    const QString& search, qint64 bookId) const
{
    const auto copies = m_circulation.listAvailableCopies(ss(search), bookId);
    return copies ? copies.value() : std::vector<Repositories::LoanCopyOption>{};
}

}  // namespace VLMS
