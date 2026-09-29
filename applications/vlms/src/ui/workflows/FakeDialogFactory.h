#pragma once

#include "ui/workflows/DialogFactory.h"

#include <vector>

namespace VLMS {

class FakeDialogFactory final : public DialogFactory {
public:
    std::optional<BookForm> nextBook;
    std::optional<MemberForm> nextMember;
    std::optional<LoanForm> nextLoan;
    std::optional<ReturnForm> nextReturn;
    std::optional<ExtendForm> nextExtend;
    std::optional<CategoryEdits> nextCategories;
    bool confirmBulkResult = false;
    bool askYesNoResult = false;
    bool warnWithActionResult = false;

    std::vector<QString> calls;

    std::optional<BookForm> addBook(QWidget*) override
    {
        calls.push_back(QStringLiteral("addBook"));
        return nextBook;
    }
    std::optional<BookForm> editBook(QWidget*, const BookForm&) override
    {
        calls.push_back(QStringLiteral("editBook"));
        return nextBook;
    }
    std::optional<MemberForm> addMember(QWidget*) override
    {
        calls.push_back(QStringLiteral("addMember"));
        return nextMember;
    }
    std::optional<MemberForm> editMember(QWidget*, const MemberForm&) override
    {
        calls.push_back(QStringLiteral("editMember"));
        return nextMember;
    }
    std::optional<LoanForm> checkout(QWidget*, const LoanForm&) override
    {
        calls.push_back(QStringLiteral("checkout"));
        return nextLoan;
    }
    std::optional<ReturnForm> returnLoan(QWidget*, const Repositories::LoanRecord&) override
    {
        calls.push_back(QStringLiteral("returnLoan"));
        return nextReturn;
    }
    std::optional<ExtendForm> extendLoan(QWidget*, const Repositories::LoanRecord&) override
    {
        calls.push_back(QStringLiteral("extendLoan"));
        return nextExtend;
    }
    std::optional<CategoryEdits> manageCategories(QWidget*) override
    {
        calls.push_back(QStringLiteral("manageCategories"));
        return nextCategories;
    }
    bool confirmBulk(QWidget*, const BulkPlan&) override
    {
        calls.push_back(QStringLiteral("confirmBulk"));
        return confirmBulkResult;
    }
    bool askYesNo(QWidget*, const QString&, const QString&) override
    {
        calls.push_back(QStringLiteral("askYesNo"));
        return askYesNoResult;
    }
    bool warnWithAction(QWidget*, const QString&, const QString&, const QString&) override
    {
        calls.push_back(QStringLiteral("warnWithAction"));
        return warnWithActionResult;
    }
};

}  // namespace VLMS
