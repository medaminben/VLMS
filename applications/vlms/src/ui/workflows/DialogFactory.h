#pragma once

#include "ui/workflows/Forms.h"

#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Repositories/LoanTypes.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QString>

#include <memory>
#include <optional>

class QWidget;

namespace VLMS {

class BulkProgress;
class CategoryEditor;

/// Every window a workflow opens. Workflows never build a dialog or a message
/// box themselves, so tests drive them with a scripted fake. Form methods
/// return an empty optional when the librarian cancels.
class DialogFactory {
public:
    virtual ~DialogFactory() = default;

    virtual std::optional<BookForm> editBook(QWidget* parent, const BookEditRequest& request) = 0;
    /// No `member` means a new member.
    virtual std::optional<MemberForm> editMember(
        QWidget* parent, const std::optional<Repositories::MemberRecord>& member) = 0;
    virtual std::optional<LoanForm> checkout(QWidget* parent, const LoanForm& prefill) = 0;
    virtual std::optional<ReturnForm> returnLoan(QWidget* parent,
                                                 const Repositories::LoanRecord& loan) = 0;
    virtual std::optional<ExtendForm> extendLoan(QWidget* parent,
                                                 const Repositories::LoanRecord& loan) = 0;
    virtual void manageCategories(QWidget* parent, CategoryEditor& editor) = 0;
    /// Archive -> Reuse number, step 1. 0 means a new book.
    virtual std::optional<qint64> chooseReuseTarget(
        QWidget* parent, const Repositories::BookCopyRecord& archivedCopy) = 0;
    virtual void showLoanHistory(QWidget* parent, const LoanHistoryRequest& request) = 0;

    virtual bool confirmBulk(QWidget* parent, const BulkPlan& plan) = 0;
    virtual std::unique_ptr<BulkProgress> startProgress(QWidget* parent,
                                                        const QString& title,
                                                        int total) = 0;
    virtual void inform(QWidget* parent, const QString& title, const QString& text) = 0;
    virtual bool askYesNo(QWidget* parent, const QString& title, const QString& text) = 0;
    virtual bool warnWithAction(QWidget* parent,
                                const QString& title,
                                const QString& text,
                                const QString& actionLabel) = 0;
};

}  // namespace VLMS
