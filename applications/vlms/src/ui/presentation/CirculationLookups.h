#pragma once

#include <VLMS/Repositories/LoanTypes.h>

#include <QString>

#include <vector>

namespace VLMS {

/// Read-only circulation helpers for checkout dialogs. Implementations talk to
/// CirculationRepository; dialogs must not hold the repository themselves.
class CirculationLookups {
public:
    virtual ~CirculationLookups() = default;

    virtual std::vector<Repositories::LoanMemberOption> borrowableMembers(
        const QString& search = {}) const = 0;
    virtual std::vector<Repositories::LoanCopyOption> availableCopies(
        const QString& search = {}, qint64 bookId = 0) const = 0;
};

}  // namespace VLMS
