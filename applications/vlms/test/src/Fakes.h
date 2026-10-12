#pragma once

// Test doubles for the workflow interfaces. They live here, not in src/, so
// they never ship inside vlms_ui.

#include "ui/shell/Navigator.h"
#include "ui/workflows/BulkFlow.h"
#include "ui/workflows/CategoryEditor.h"
#include "ui/workflows/DialogFactory.h"
#include "ui/workflows/ErrorPresenter.h"

#include <QString>

#include <functional>
#include <string>
#include <vector>

namespace VLMS {

class FakeErrorPresenter final : public ErrorPresenter {
public:
    struct Call {
        QWidget* parent = nullptr;
        std::string key;
    };
    std::vector<Call> calls;

    void show(QWidget* parent, const Core::Error& error) override
    {
        calls.push_back(Call{parent, error.key});
    }
};

class FakeNavigator final : public Navigator {
public:
    PageId lastPage = PageId::Catalog;
    QString lastMembershipNumber;
    qint64 lastBookId = 0;
    ArchiveType lastArchiveType = ArchiveType::Members;
    int showMemberLoansCount = 0;

    void show(PageId page) override { lastPage = page; }

    void showMemberLoans(const QString& membershipNumber) override
    {
        ++showMemberLoansCount;
        lastPage = PageId::Circulation;
        lastMembershipNumber = membershipNumber;
    }

    void showBook(qint64 bookId) override
    {
        lastPage = PageId::Catalog;
        lastBookId = bookId;
    }

    void showArchive(ArchiveType type) override
    {
        lastPage = PageId::Archive;
        lastArchiveType = type;
    }
};

/// Scripted DialogFactory. Set `next*` before the call; read `calls`,
/// `plans`, `informs` and the captured requests after.
class FakeDialogFactory final : public DialogFactory {
public:
    struct Message {
        QString title;
        QString text;
    };

    std::optional<BookForm> nextBook;
    std::optional<MemberForm> nextMember;
    std::optional<LoanForm> nextLoan;
    std::optional<ReturnForm> nextReturn;
    std::optional<ExtendForm> nextExtend;
    std::optional<qint64> nextReuseTarget;
    bool confirmBulkResult = false;
    bool askYesNoResult = false;
    bool warnWithActionResult = false;
    /// Runs inside manageCategories, standing in for the librarian's clicks.
    std::function<void(CategoryEditor&)> categoryScript;

    std::vector<QString> calls;
    std::vector<BulkPlan> plans;
    std::vector<Message> informs;
    std::vector<BookEditRequest> bookRequests;
    std::vector<std::optional<Repositories::MemberRecord>> memberRequests;
    std::vector<LoanForm> checkoutPrefills;
    std::vector<LoanHistoryRequest> historyRequests;

    std::optional<BookForm> editBook(QWidget*, const BookEditRequest& request) override
    {
        calls.push_back(QStringLiteral("editBook"));
        bookRequests.push_back(request);
        return nextBook;
    }
    std::optional<MemberForm> editMember(
        QWidget*, const std::optional<Repositories::MemberRecord>& member) override
    {
        calls.push_back(QStringLiteral("editMember"));
        memberRequests.push_back(member);
        return nextMember;
    }
    std::optional<LoanForm> checkout(QWidget*, const LoanForm& prefill) override
    {
        calls.push_back(QStringLiteral("checkout"));
        checkoutPrefills.push_back(prefill);
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
    void manageCategories(QWidget*, CategoryEditor& editor) override
    {
        calls.push_back(QStringLiteral("manageCategories"));
        if (categoryScript) {
            categoryScript(editor);
        }
    }
    std::optional<qint64> chooseReuseTarget(QWidget*,
                                            const Repositories::BookCopyRecord&) override
    {
        calls.push_back(QStringLiteral("chooseReuseTarget"));
        return nextReuseTarget;
    }
    void showLoanHistory(QWidget*, const LoanHistoryRequest& request) override
    {
        calls.push_back(QStringLiteral("showLoanHistory"));
        historyRequests.push_back(request);
    }
    bool confirmBulk(QWidget*, const BulkPlan& plan) override
    {
        calls.push_back(QStringLiteral("confirmBulk"));
        plans.push_back(plan);
        return confirmBulkResult;
    }
    std::unique_ptr<BulkProgress> startProgress(QWidget*, const QString&, int) override
    {
        calls.push_back(QStringLiteral("startProgress"));
        return std::make_unique<NoProgress>();
    }
    void inform(QWidget*, const QString& title, const QString& text) override
    {
        calls.push_back(QStringLiteral("inform"));
        informs.push_back(Message{title, text});
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

    [[nodiscard]] int count(const QString& call) const
    {
        int n = 0;
        for (const QString& c : calls) {
            n += c == call ? 1 : 0;
        }
        return n;
    }

private:
    class NoProgress final : public BulkProgress {
    public:
        void step(int, const QString&) override {}
        [[nodiscard]] bool cancelled() const override { return false; }
    };
};

}  // namespace VLMS
