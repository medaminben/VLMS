#pragma once

#include <VLMS/Core/Date.h>

#include <string>
#include <string_view>

namespace VLMS::Repositories::LoanPolicy {

[[nodiscard]] constexpr int defaultLoanDays() { return 14; }

[[nodiscard]] Date suggestedDueDate(const Date& borrowedOn);
[[nodiscard]] Date minimumExtensionDate(const Date& currentDue, const Date& today);
[[nodiscard]] Date suggestedExtensionDate(const Date& currentDue, const Date& today);

struct Validation {
    bool ok = true;
    std::string key;
    std::string message;

    [[nodiscard]] explicit operator bool() const { return ok; }
};

[[nodiscard]] Validation accepted();
[[nodiscard]] Validation rejected(std::string key, std::string message);

[[nodiscard]] Date parseIsoDate(std::string_view text);

[[nodiscard]] Validation validateLoanDates(std::string_view borrowedAt,
                                           std::string_view dueAt,
                                           const Date& today);

[[nodiscard]] Validation validateReturnDate(std::string_view returnedAt,
                                            std::string_view storedBorrowedAt,
                                            const Date& today);

[[nodiscard]] Validation validateExtension(std::string_view newDueAt,
                                           std::string_view storedDueAt,
                                           const Date& today);

}  // namespace VLMS::Repositories::LoanPolicy
