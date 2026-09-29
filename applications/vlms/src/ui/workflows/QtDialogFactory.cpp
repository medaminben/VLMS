#include "ui/workflows/QtDialogFactory.h"

#include "ui/views/UiHelpers.h"
#include "ui/views/BookEditorDialog.h"
#include "ui/views/CategoryManagerDialog.h"
#include "ui/views/LoanCheckoutDialog.h"
#include "ui/views/LoanExtendDialog.h"
#include "ui/views/LoanReturnDialog.h"
#include "ui/views/MemberEditorDialog.h"
#include "ui/presentation/CatalogLookups.h"
#include "ui/presentation/CirculationLookups.h"
#include "ui/presentation/MemberLookups.h"
#include "ui/services/OcrService.h"

#include <VLMS/Repositories/CatalogRepository.h>

#include <QDialog>

namespace VLMS {
namespace {

BookForm bookFormFromDialog(const BookEditorDialog& dialog)
{
    BookForm form;
    form.book = dialog.bookInput();
    form.copies = dialog.copyInputs();
    form.coverSourcePath = dialog.coverSourcePath();
    form.coverChanged = dialog.coverChanged();
    return form;
}

}  // namespace

QtDialogFactory::QtDialogFactory(Repositories::CatalogRepository& catalog,
                                 CatalogLookups& catalogLookups,
                                 MemberLookups& memberLookups,
                                 CirculationLookups& circulationLookups,
                                 OcrService& ocr)
    : m_catalog(catalog)
    , m_catalogLookups(catalogLookups)
    , m_memberLookups(memberLookups)
    , m_circulationLookups(circulationLookups)
    , m_ocr(ocr)
{
}

std::optional<BookForm> QtDialogFactory::addBook(QWidget* parent)
{
    BookEditorDialog dialog(m_catalogLookups, m_ocr, parent);
    dialog.setDialogFactory(this);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return bookFormFromDialog(dialog);
}

std::optional<BookForm> QtDialogFactory::editBook(QWidget* parent, const BookForm& initial)
{
    // Phase 1 wraps today's editor, which loads from a BookRecord. Callers that
    // only have BookInput rebuild a minimal record; workflows (Phase 2) will
    // pass the full record into a richer factory method.
    Repositories::BookRecord book;
    book.title = initial.book.title;
    book.authorName = initial.book.authorName;
    book.publisherName = initial.book.publisherName;
    book.categoryId = initial.book.categoryId;
    book.isbn = initial.book.isbn;
    book.publicationDate = initial.book.publicationDate;
    book.placeOfPublication = initial.book.placeOfPublication;
    book.pages = initial.book.pages;
    book.dimensions = initial.book.dimensions;
    book.language = initial.book.language;
    book.description = initial.book.description;
    BookEditorDialog dialog(m_catalogLookups, m_ocr, book, parent);
    dialog.setDialogFactory(this);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return bookFormFromDialog(dialog);
}

std::optional<MemberForm> QtDialogFactory::addMember(QWidget* parent)
{
    MemberEditorDialog dialog(m_memberLookups, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return MemberForm{dialog.memberInput()};
}

std::optional<MemberForm> QtDialogFactory::editMember(QWidget* parent, const MemberForm& initial)
{
    Repositories::MemberRecord member;
    member.membershipNumber = initial.member.membershipNumber;
    member.firstName = initial.member.firstName;
    member.lastName = initial.member.lastName;
    member.sex = initial.member.sex;
    member.dateOfBirth = initial.member.dateOfBirth;
    member.email = initial.member.email;
    member.phone = initial.member.phone;
    member.address = initial.member.address;
    member.city = initial.member.city;
    member.status = initial.member.status;
    member.notes = initial.member.notes;
    member.occupation = initial.member.occupation;
    member.fullName = initial.member.fullName;
    MemberEditorDialog dialog(m_memberLookups, member, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return MemberForm{dialog.memberInput()};
}

std::optional<LoanForm> QtDialogFactory::checkout(QWidget* parent, const LoanForm& prefill)
{
    LoanScope scope;
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

std::optional<CategoryEdits> QtDialogFactory::manageCategories(QWidget* parent)
{
    // CategoryManagerDialog applies edits as it goes and closes with Rejected
    // (Close). Running it is the success; cancel is Escape before any useful
    // work, which still leaves applied=true for Phase 1 — callers treat empty
    // optional as "factory declined to open", not "user closed".
    CategoryManagerDialog dialog(m_catalog, parent);
    dialog.exec();
    return CategoryEdits{true};
}

bool QtDialogFactory::confirmBulk(QWidget* parent, const BulkPlan& plan)
{
    return askYesNo(parent, plan.title, plan.text);
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
