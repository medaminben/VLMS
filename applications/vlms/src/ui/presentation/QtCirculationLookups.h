#pragma once

#include "ui/presentation/CirculationLookups.h"

namespace VLMS::Repositories {
class CirculationRepository;
}

namespace VLMS {

class QtCirculationLookups final : public CirculationLookups {
public:
    explicit QtCirculationLookups(Repositories::CirculationRepository& circulation);

    std::vector<Repositories::LoanMemberOption> borrowableMembers(
        const QString& search = {}) const override;
    std::vector<Repositories::LoanCopyOption> availableCopies(
        const QString& search = {}, qint64 bookId = 0) const override;

private:
    Repositories::CirculationRepository& m_circulation;
};

}  // namespace VLMS
