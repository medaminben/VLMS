#include <VLMS/Repositories/LoanPolicy.h>

#include <VLMS/Core/Text.h>

namespace VLMS::Repositories::LoanPolicy {

Date suggestedDueDate(const Date& borrowedOn)
{
    if (!borrowedOn.isValid()) {
        return {};
    }
    return borrowedOn.addDays(defaultLoanDays());
}

Date minimumExtensionDate(const Date& currentDue, const Date& today)
{
    if (!currentDue.isValid()) {
        return today;
    }
    const Date dayAfterCurrentDue = currentDue.addDays(1);
    return dayAfterCurrentDue < today ? today : dayAfterCurrentDue;
}

Date suggestedExtensionDate(const Date& currentDue, const Date& today)
{
    const Date effectiveFrom = (currentDue.isValid() && currentDue > today) ? currentDue : today;
    return effectiveFrom.addDays(defaultLoanDays());
}

Validation accepted()
{
    return {true, {}, {}};
}

Validation rejected(std::string key, std::string message)
{
    return {false, std::move(key), std::move(message)};
}

Date parseIsoDate(std::string_view text)
{
    return Date::fromIso(trim(text));
}

Validation validateLoanDates(std::string_view borrowedAt,
                             std::string_view dueAt,
                             const Date& today)
{
    const Date borrowed = parseIsoDate(borrowedAt);
    if (!borrowed.isValid()) {
        return rejected("error.loan.borrowUnreadable",
                        "Borrow date must be a real date in YYYY-MM-DD form.");
    }

    const Date due = parseIsoDate(dueAt);
    if (!due.isValid()) {
        return rejected("error.loan.dueUnreadable",
                        "Due date must be a real date in YYYY-MM-DD form.");
    }

    if (due <= borrowed) {
        return rejected("error.loan.dueNotAfterBorrow",
                        "Due date must be after the borrow date.");
    }

    if (borrowed > today) {
        return rejected("error.loan.borrowInFuture", "Borrow date cannot be in the future.");
    }

    return accepted();
}

Validation validateReturnDate(std::string_view returnedAt,
                              std::string_view storedBorrowedAt,
                              const Date& today)
{
    const Date returned = parseIsoDate(returnedAt);
    if (!returned.isValid()) {
        return rejected("error.loan.returnUnreadable",
                        "Return date must be a real date in YYYY-MM-DD form.");
    }

    const Date borrowed = parseIsoDate(storedBorrowedAt);
    if (!borrowed.isValid()) {
        return rejected("error.loan.storedBorrowUnreadable",
                        "This loan's borrow date cannot be read, so a return date cannot be checked "
                        "against it. Correct the loan first.");
    }

    if (returned < borrowed) {
        return rejected("error.loan.returnBeforeBorrow",
                        "Return date cannot be before the borrow date.");
    }

    if (returned > today) {
        return rejected("error.loan.returnInFuture", "Return date cannot be in the future.");
    }

    return accepted();
}

Validation validateExtension(std::string_view newDueAt,
                             std::string_view storedDueAt,
                             const Date& today)
{
    const Date newDue = parseIsoDate(newDueAt);
    if (!newDue.isValid()) {
        return rejected("error.loan.dueUnreadable",
                        "Due date must be a real date in YYYY-MM-DD form.");
    }

    const Date currentDue = parseIsoDate(storedDueAt);
    if (!currentDue.isValid()) {
        return rejected("error.loan.storedDueUnreadable",
                        "This loan's due date cannot be read, so it cannot be extended. Correct the "
                        "loan first.");
    }

    if (newDue <= currentDue) {
        return rejected("error.loan.extendNotAfter",
                        "New due date must be after the current due date.");
    }

    if (newDue < minimumExtensionDate(currentDue, today)) {
        return rejected("error.loan.extendInPast", "New due date cannot be in the past.");
    }

    return accepted();
}

}  // namespace VLMS::Repositories::LoanPolicy
