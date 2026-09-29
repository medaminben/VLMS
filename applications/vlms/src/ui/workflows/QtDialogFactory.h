#pragma once

#include "ui/workflows/DialogFactory.h"

namespace VLMS::Repositories {
class CatalogRepository;
class CirculationRepository;
class MemberRepository;
}  // namespace VLMS::Repositories

namespace VLMS {

class QtDialogFactory final : public DialogFactory {
public:
    QtDialogFactory(Repositories::CatalogRepository& catalog,
                    Repositories::MemberRepository& members,
                    Repositories::CirculationRepository& circulation);

    std::optional<BookForm> addBook(QWidget* parent) override;
    std::optional<BookForm> editBook(QWidget* parent, const BookForm& initial) override;
    std::optional<MemberForm> addMember(QWidget* parent) override;
    std::optional<MemberForm> editMember(QWidget* parent, const MemberForm& initial) override;
    std::optional<LoanForm> checkout(QWidget* parent, const LoanForm& prefill) override;
    std::optional<ReturnForm> returnLoan(QWidget* parent,
                                         const Repositories::LoanRecord& loan) override;
    std::optional<ExtendForm> extendLoan(QWidget* parent,
                                         const Repositories::LoanRecord& loan) override;
    std::optional<CategoryEdits> manageCategories(QWidget* parent) override;
    bool confirmBulk(QWidget* parent, const BulkPlan& plan) override;
    bool askYesNo(QWidget* parent, const QString& title, const QString& text) override;
    bool warnWithAction(QWidget* parent,
                        const QString& title,
                        const QString& text,
                        const QString& actionLabel) override;

private:
    Repositories::CatalogRepository& m_catalog;
    Repositories::MemberRepository& m_members;
    Repositories::CirculationRepository& m_circulation;
};

}  // namespace VLMS
