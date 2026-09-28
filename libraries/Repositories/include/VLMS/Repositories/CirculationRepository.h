#pragma once

#include <VLMS/Repositories/LoanTypes.h>
#include <VLMS/Core/Result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace VLMS::Database {
class SqliteSession;
}  // namespace VLMS::Database

namespace VLMS::Repositories {

class CirculationRepository {
public:
    explicit CirculationRepository(Database::SqliteSession& session);

    [[nodiscard]] Core::Result<std::vector<LoanRecord>> listLoans(const LoanQuery& query) const;
    [[nodiscard]] Core::Result<int> rankOfLoan(std::int64_t id, const LoanQuery& query) const;
    [[nodiscard]] Core::Result<int> countLoans(const LoanQuery& query) const;
    /// The years loans were made in, newest first, among the loans in `scope`.
    [[nodiscard]] Core::Result<std::vector<std::string>> listLoanYears(
        ArchiveScope scope = ArchiveScope::Live) const;
    [[nodiscard]] Core::Result<LoanRecord> getLoan(std::int64_t id) const;

    [[nodiscard]] Core::Result<std::int64_t> createLoan(const LoanInput& input);
    [[nodiscard]] Core::Status returnLoan(std::int64_t loanId,
                                                const std::string& returnedAt = {},
                                                const std::string& notes = {});
    [[nodiscard]] Core::Status extendLoan(std::int64_t loanId, const std::string& dueAt);
    [[nodiscard]] Core::Status archiveLoan(std::int64_t loanId);
    /// The same answer archiveLoan gives before it writes. A loan still out
    /// is refused even when the row is already archived.
    [[nodiscard]] Core::Status canArchiveLoan(std::int64_t loanId) const;
    [[nodiscard]] Core::Status restoreLoan(std::int64_t loanId);
    /// Destroys an archived loan. The bottom of the funnel: a loan has nothing
    /// beneath it, so being archived is the only condition.
    [[nodiscard]] Core::Status purgeLoan(std::int64_t loanId);
    /// The same answer purgeLoan gives before it writes.
    [[nodiscard]] Core::Status canPurgeLoan(std::int64_t loanId) const;

    [[nodiscard]] Core::Result<std::vector<LoanMemberOption>>
    listBorrowableMembers(const std::string& search = {}) const;
    [[nodiscard]] Core::Result<std::vector<LoanCopyOption>>
    listAvailableCopies(const std::string& search = {}, std::int64_t bookId = 0) const;

    [[nodiscard]] static std::vector<std::string> filterCodes();

private:
    [[nodiscard]] Core::Status memberCanBorrow(std::int64_t memberId) const;
    [[nodiscard]] Core::Status copyIsAvailable(std::int64_t bookCopyId) const;

    Database::SqliteSession& m_session;
};

}  // namespace VLMS::Repositories
