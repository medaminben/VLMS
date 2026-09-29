#include "TestDatabase.h"
#include "TestSeed.h"
#include "WorkflowTestSupport.h"

#include "ui/LoanHistoryDialog.h"
#include "ui/presentation/LoanListSpec.h"
#include "QtBridge.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTableView>
#include <QTimer>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using VLMS::T;
using namespace Test;

namespace {

QPushButton* buttonWithText(QWidget* root, const QString& text)
{
    for (QPushButton* button : root->findChildren<QPushButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

void acceptNextDialog()
{
    auto* poll = new QTimer;
    poll->setInterval(10);
    QObject::connect(poll, &QTimer::timeout, [poll] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr || !dialog->isVisible()) {
            return;
        }
        poll->stop();
        poll->deleteLater();
        if (auto* box = dialog->findChild<QDialogButtonBox*>()) {
            if (QPushButton* ok = box->button(QDialogButtonBox::Ok)) {
                ok->click();
                return;
            }
        }
        dialog->accept();
    });
    poll->start();
}

/// Shared DB + workflow + history LoanListSpec for checklist-named TEST() cases.
/// Suite name must be exactly LoanHistoryDialog (not *Test) so Phase 6
/// `ctest -R 'LoanHistoryDialog.Book'` matches.
struct LoanHistoryEnv {
    LoanHistoryEnv()
    {
        VLMS::Core::Locale::setCode("en");
        db = std::make_unique<TestDatabase>();
        if (!db->isValid()) {
            return;
        }
        catalog = std::make_unique<VLMS::Repositories::CatalogRepository>(db->session(),
                                                                          db->resourcesDirectory());
        members = std::make_unique<VLMS::Repositories::MemberRepository>(db->session(),
                                                                         db->resourcesDirectory());
        circulation = std::make_unique<VLMS::Repositories::CirculationRepository>(db->session());
        workflows = std::make_unique<LoanWorkflowHarness>(*circulation);
        spec = std::make_unique<VLMS::LoanListSpec>(*circulation, *catalog, *members,
                                                    *workflows->loans);
    }

    ~LoanHistoryEnv()
    {
        spec.reset();
        workflows.reset();
        circulation.reset();
        members.reset();
        catalog.reset();
        db.reset();
        VLMS::Core::Locale::setCode("en");
    }

    [[nodiscard]] bool ok() const { return db && db->isValid() && spec && workflows; }

    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<VLMS::Repositories::CatalogRepository> catalog;
    std::unique_ptr<VLMS::Repositories::MemberRepository> members;
    std::unique_ptr<VLMS::Repositories::CirculationRepository> circulation;
    std::unique_ptr<LoanWorkflowHarness> workflows;
    std::unique_ptr<VLMS::LoanListSpec> spec;
};

}  // namespace

TEST(LoanHistoryDialog, BookPinSetsQueryStateBookId)
{
    LoanHistoryEnv env;
    ASSERT_TRUE(env.ok()) << (env.db ? env.db->lastError() : std::string("no db"));

    VLMS::LoanHistoryDialog dialog(*env.spec, *env.workflows->loans, env.workflows->events,
                                   VLMS::LoanHistoryDialog::Kind::Book, 42,
                                   QStringLiteral("Titled"),
                                   VLMS::Repositories::ArchiveScope::Any);
    dialog.show();
    QApplication::processEvents();

    ASSERT_TRUE(dialog.queryState().pinned.has_value());
    EXPECT_EQ(dialog.queryState().pinned->bookId, 42);
    EXPECT_EQ(dialog.queryState().pinned->memberId, 0);
    EXPECT_EQ(dialog.queryState().pinned->copyId, 0);
    EXPECT_EQ(dialog.queryState().scope, VLMS::Repositories::ArchiveScope::Any);
}

TEST(LoanHistoryDialog, MemberPinSetsQueryStateMemberId)
{
    LoanHistoryEnv env;
    ASSERT_TRUE(env.ok()) << (env.db ? env.db->lastError() : std::string("no db"));

    VLMS::LoanHistoryDialog dialog(*env.spec, *env.workflows->loans, env.workflows->events,
                                   VLMS::LoanHistoryDialog::Kind::Member, 7,
                                   QStringLiteral("Amina"),
                                   VLMS::Repositories::ArchiveScope::Any);
    dialog.show();
    QApplication::processEvents();

    ASSERT_TRUE(dialog.queryState().pinned.has_value());
    EXPECT_EQ(dialog.queryState().pinned->memberId, 7);
    EXPECT_EQ(dialog.queryState().pinned->bookId, 0);
    EXPECT_EQ(dialog.queryState().pinned->copyId, 0);
}

TEST(LoanHistoryDialog, ArchivedScopeUsesArchivedOrAny)
{
    LoanHistoryEnv env;
    ASSERT_TRUE(env.ok()) << (env.db ? env.db->lastError() : std::string("no db"));

    VLMS::LoanHistoryDialog dialog(*env.spec, *env.workflows->loans, env.workflows->events,
                                   VLMS::LoanHistoryDialog::Kind::ArchivedMember, 3,
                                   QStringLiteral("Archived"),
                                   VLMS::Repositories::ArchiveScope::Any);
    dialog.show();
    QApplication::processEvents();

    EXPECT_EQ(dialog.queryState().scope, VLMS::Repositories::ArchiveScope::Any);
    ASSERT_TRUE(dialog.queryState().pinned.has_value());
    EXPECT_EQ(dialog.queryState().pinned->memberId, 3);
    EXPECT_EQ(buttonWithText(&dialog, T("circulation.checkout")), nullptr)
        << "archive history is read-only";
}

TEST(LoanHistoryDialog, ReturnActionCallsLoanWorkflow)
{
    LoanHistoryEnv env;
    ASSERT_TRUE(env.ok()) << (env.db ? env.db->lastError() : std::string("no db"));

    const std::int64_t memberId = seedMember(*env.db, uniqueMemberSeed(1));
    ASSERT_GT(memberId, 0);
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Open Loan Book";
    const std::int64_t bookId = seedBook(*env.db, seed);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*env.db, bookId);
    ASSERT_FALSE(copies.empty());
    const std::int64_t loanId =
        rawInsertLoan(*env.db, memberId, copies.at(0), "2025-02-10", "2025-02-24");
    ASSERT_GT(loanId, 0);

    VLMS::LoanHistoryDialog dialog(*env.spec, *env.workflows->loans, env.workflows->events,
                                   VLMS::LoanHistoryDialog::Kind::Book, bookId,
                                   QStringLiteral("Open Loan Book"),
                                   VLMS::Repositories::ArchiveScope::Any);
    dialog.show();
    QApplication::processEvents();

    QTableView* table = dialog.table();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 1);
    table->selectRow(0);
    QApplication::processEvents();

    auto* ret = buttonWithText(&dialog, T("circulation.return"));
    ASSERT_NE(ret, nullptr);
    ASSERT_TRUE(ret->isEnabled());
    acceptNextDialog();
    ret->click();
    QApplication::processEvents();

    const auto loan = env.circulation->getLoan(loanId);
    ASSERT_TRUE(loan);
    EXPECT_FALSE(loan->returnedAt.empty()) << "return action must complete through LoanWorkflow";
}
