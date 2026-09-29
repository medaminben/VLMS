#pragma once

#include "ui/workflows/DialogFactory.h"

namespace VLMS {

class CatalogLookups;
class CirculationLookups;
class MemberLookups;
class OcrService;

/// The real windows behind DialogFactory. The one place in workflows/ that
/// builds widgets.
class QtDialogFactory final : public DialogFactory {
public:
    QtDialogFactory(CatalogLookups& catalogLookups,
                    MemberLookups& memberLookups,
                    CirculationLookups& circulationLookups,
                    OcrService& ocr);

    std::optional<BookForm> editBook(QWidget* parent, const BookEditRequest& request) override;
    std::optional<MemberForm> editMember(
        QWidget* parent, const std::optional<Repositories::MemberRecord>& member) override;
    std::optional<LoanForm> checkout(QWidget* parent, const LoanForm& prefill) override;
    std::optional<ReturnForm> returnLoan(QWidget* parent,
                                         const Repositories::LoanRecord& loan) override;
    std::optional<ExtendForm> extendLoan(QWidget* parent,
                                         const Repositories::LoanRecord& loan) override;
    void manageCategories(QWidget* parent, CategoryEditor& editor) override;
    std::optional<qint64> chooseReuseTarget(
        QWidget* parent, const Repositories::BookCopyRecord& archivedCopy) override;
    void showLoanHistory(QWidget* parent, const LoanHistoryRequest& request) override;
    bool confirmBulk(QWidget* parent, const BulkPlan& plan) override;
    std::unique_ptr<BulkProgress> startProgress(QWidget* parent,
                                                const QString& title,
                                                int total) override;
    void inform(QWidget* parent, const QString& title, const QString& text) override;
    bool askYesNo(QWidget* parent, const QString& title, const QString& text) override;
    bool warnWithAction(QWidget* parent,
                        const QString& title,
                        const QString& text,
                        const QString& actionLabel) override;

private:
    CatalogLookups& m_catalogLookups;
    MemberLookups& m_memberLookups;
    CirculationLookups& m_circulationLookups;
    OcrService& m_ocr;
};

}  // namespace VLMS
