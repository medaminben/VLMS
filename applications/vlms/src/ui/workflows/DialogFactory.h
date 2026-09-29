#pragma once

#include "ui/workflows/Forms.h"

#include <VLMS/Repositories/LoanTypes.h>

#include <QString>

#include <optional>

class QWidget;

namespace VLMS {

class DialogFactory {
public:
    virtual ~DialogFactory() = default;

    virtual std::optional<BookForm> addBook(QWidget* parent) = 0;
    virtual std::optional<BookForm> editBook(QWidget* parent, const BookForm& initial) = 0;
    virtual std::optional<MemberForm> addMember(QWidget* parent) = 0;
    virtual std::optional<MemberForm> editMember(QWidget* parent, const MemberForm& initial) = 0;
    virtual std::optional<LoanForm> checkout(QWidget* parent, const LoanForm& prefill) = 0;
    virtual std::optional<ReturnForm> returnLoan(QWidget* parent,
                                                 const Repositories::LoanRecord& loan) = 0;
    virtual std::optional<ExtendForm> extendLoan(QWidget* parent,
                                                 const Repositories::LoanRecord& loan) = 0;
    virtual std::optional<CategoryEdits> manageCategories(QWidget* parent) = 0;
    virtual bool confirmBulk(QWidget* parent, const BulkPlan& plan) = 0;
    virtual bool askYesNo(QWidget* parent, const QString& title, const QString& text) = 0;
    virtual bool warnWithAction(QWidget* parent,
                                const QString& title,
                                const QString& text,
                                const QString& actionLabel) = 0;
};

}  // namespace VLMS
