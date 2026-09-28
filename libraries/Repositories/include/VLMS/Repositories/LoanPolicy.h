#pragma once

#include <VLMS/Core/Date.h>

#include <string>
#include <string_view>

namespace VLMS::Repositories::LoanPolicy {

[[nodiscard]] constexpr int defaultLoanDays() { return 14; }

[[nodiscard]] Core::Date suggestedDueDate(const Core::Date& borrowedOn);
[[nodiscard]] Core::Date minimumExtensionDate(const Core::Date& currentDue, const Core::Date& today);
[[nodiscard]] Core::Date suggestedExtensionDate(const Core::Date& currentDue, const Core::Date& today);

struct Validation {
    bool ok = true;
    std::string key;
    std::string message;

    [[nodiscard]] explicit operator bool() const { return ok; }
};

[[nodiscard]] Validation accepted();
[[nodiscard]] Validation rejected(std::string key, std::string message);

[[nodiscard]] Core::Date parseIsoDate(std::string_view text);

[[nodiscard]] Validation validateLoanDates(std::string_view borrowedAt,
                                           std::string_view dueAt,
                                           const Core::Date& today);

[[nodiscard]] Validation validateReturnDate(std::string_view returnedAt,
                                            std::string_view storedBorrowedAt,
                                            const Core::Date& today);

[[nodiscard]] Validation validateExtension(std::string_view newDueAt,
                                           std::string_view storedDueAt,
                                           const Core::Date& today);

}  // namespace VLMS::Repositories::LoanPolicy
