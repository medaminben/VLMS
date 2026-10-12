#include "ui/workflows/QtDialogFactory.h"

#include "ui/views/BookEditorDialog.h"
#include "ui/views/BulkAction.h"
#include "ui/views/CategoryManagerDialog.h"
#include "ui/views/LoanCheckoutDialog.h"
#include "ui/views/LoanExtendDialog.h"
#include "ui/views/LoanHistoryDialog.h"
#include "ui/views/LoanReturnDialog.h"
#include "ui/views/MemberEditorDialog.h"
#include "ui/views/ReuseNumberFlow.h"
#include "ui/views/UiHelpers.h"
#include "ui/presentation/CatalogLookups.h"
#include "ui/presentation/CirculationLookups.h"
#include "ui/presentation/MemberLookups.h"
#include "ui/services/OcrService.h"

#include <QDialog>

#include <memory>

namespace VLMS {
namespace {

LoanHistoryDialog::Kind dialogKind(const LoanHistoryKind kind)
{
    switch (kind) {
    case LoanHistoryKind::Book:
        return LoanHistoryDialog::Kind::Book;
    case LoanHistoryKind::Member:
        return LoanHistoryDialog::Kind::Member;
    case LoanHistoryKind::ArchivedMember:
        return LoanHistoryDialog::Kind::ArchivedMember;
    case LoanHistoryKind::ArchivedBook:
        return LoanHistoryDialog::Kind::ArchivedBook;
    case LoanHistoryKind::ArchivedCopy:
        return LoanHistoryDialog::Kind::ArchivedCopy;
    }
    return LoanHistoryDialog::Kind::Book;
}

}  // namespace

QtDialogFactory::QtDialogFactory(CatalogLookups& catalogLookups,
                                 MemberLookups& memberLookups,
                                 CirculationLookups& circulationLookups,
                                 OcrService& ocr)
    : m_catalogLookups(catalogLookups)
    , m_memberLookups(memberLookups)
    , m_circulationLookups(circulationLookups)
    , m_ocr(ocr)
{
}

std::optional<BookForm> QtDialogFactory::editBook(QWidget* parent, const BookEditRequest& request)
{
    std::unique_ptr<BookEditorDialog> dialog =
        request.book ? std::make_unique<BookEditorDialog>(m_catalogLookups, m_ocr, *request.book, parent)
                     : std::make_unique<BookEditorDialog>(m_catalogLookups, m_ocr, parent);
    dialog->setManageCategories(request.manageCategories);
    if (request.reserveNumberFrom) {
        dialog->reserveCopyNumber(*request.reserveNumberFrom);
    }
    if (dialog->exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    BookForm form;
    form.book = dialog->bookInput();
    form.copies = dialog->copyInputs();
    form.coverSourcePath = dialog->coverSourcePath();
    form.coverChanged = dialog->coverChanged();
    return form;
}

std::optional<MemberForm> QtDialogFactory::editMember(
    QWidget* parent, const std::optional<Repositories::MemberRecord>& member)
{
    std::unique_ptr<MemberEditorDialog> dialog =
        member ? std::make_unique<MemberEditorDialog>(m_memberLookups, *member, parent)
               : std::make_unique<MemberEditorDialog>(m_memberLookups, parent);
    if (dialog->exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    MemberForm form;
    form.member = dialog->memberInput();
    form.photoSourcePath = dialog->photoSourcePath();
    form.idImageSourcePath = dialog->idImageSourcePath();
    form.photoChanged = dialog->photoChanged();
    form.idImageChanged = dialog->idImageChanged();
    form.photoRemoved = dialog->photoRemoved();
    form.idImageRemoved = dialog->idImageRemoved();
    return form;
}

std::optional<LoanForm> QtDialogFactory::checkout(QWidget* parent, const LoanForm& prefill)
{
    ::LoanScope scope;
    scope.bookId = prefill.bookId;
    scope.memberId = prefill.memberId;
    LoanCheckoutDialog dialog(m_circulationLookups, scope, parent);
    if (!dialog.hasLendableCopy()) {
        return std::nullopt;
    }
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    LoanForm form;
    form.loan = dialog.loanInput();
    form.bookId = prefill.bookId;
    form.memberId = prefill.memberId;
    return form;
}

std::optional<ReturnForm> QtDialogFactory::returnLoan(QWidget* parent,
                                                     const Repositories::LoanRecord& loan)
{
    LoanReturnDialog dialog(loan, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return ReturnForm{dialog.returnedAt(), dialog.notes()};
}

std::optional<ExtendForm> QtDialogFactory::extendLoan(QWidget* parent,
                                                     const Repositories::LoanRecord& loan)
{
    LoanExtendDialog dialog(loan, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return ExtendForm{dialog.dueAt()};
}

void QtDialogFactory::manageCategories(QWidget* parent, CategoryEditor& editor)
{
    CategoryManagerDialog dialog(editor, parent);
    dialog.exec();
}

std::optional<qint64> QtDialogFactory::chooseReuseTarget(
    QWidget* parent, const Repositories::BookCopyRecord& archivedCopy)
{
    ReuseBookChooser chooser(m_catalogLookups, archivedCopy, parent);
    if (chooser.exec() != QDialog::Accepted || chooser.chosenBookId() < 0) {
        return std::nullopt;
    }
    return chooser.chosenBookId();
}

void QtDialogFactory::showLoanHistory(QWidget* parent, const LoanHistoryRequest& request)
{
    if (request.spec == nullptr || request.loans == nullptr || request.events == nullptr) {
        return;
    }
    LoanHistoryDialog dialog(*request.spec, *request.loans, *request.events,
                             dialogKind(request.kind), request.id, request.title, request.scope,
                             parent);
    dialog.exec();
}

bool QtDialogFactory::confirmBulk(QWidget* parent, const BulkPlan& plan)
{
    return VLMS::askYesNo(parent, plan.title, plan.text);
}

std::unique_ptr<BulkProgress> QtDialogFactory::startProgress(QWidget* parent,
                                                             const QString& title,
                                                             int total)
{
    return QtBulkUi(parent).startProgress(title, total);
}

void QtDialogFactory::inform(QWidget* parent, const QString& title, const QString& text)
{
    showInformation(parent, title, text);
}

bool QtDialogFactory::askYesNo(QWidget* parent, const QString& title, const QString& text)
{
    return VLMS::askYesNo(parent, title, text);
}

bool QtDialogFactory::warnWithAction(QWidget* parent,
                                     const QString& title,
                                     const QString& text,
                                     const QString& actionLabel)
{
    return showWarningWithAction(parent, title, text, actionLabel);
}

}  // namespace VLMS
