#include "TestDatabase.h"
#include "TestSeed.h"
#include "WorkflowTestSupport.h"

#include "ui/views/LoanHistoryDialog.h"
#include "ui/presentation/LoanListSpec.h"
#include "QtBridge.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QTableView>
#include <QTest>
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

namespace {

/// The same history spec, but every dialog scripted: what the shared action
/// bar triggers is read back from FakeDialogFactory instead of a real modal.
struct FakeLoanHistoryEnv {
    FakeLoanHistoryEnv()
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
        bulk = std::make_unique<VLMS::BulkRunner>(dialogs, events);
        loans = std::make_unique<VLMS::LoanWorkflow>(*circulation, dialogs, errors, events,
                                                     navigator, *bulk);
        spec = std::make_unique<VLMS::LoanListSpec>(*circulation, *catalog, *members, *loans);
    }

    ~FakeLoanHistoryEnv() { VLMS::Core::Locale::setCode("en"); }

    [[nodiscard]] bool ok() const { return db && db->isValid() && spec; }

    /// One book with one copy lent to one member, open unless `returnedAt` is set.
    void seedLoan(const char* returnedAt = nullptr)
    {
        memberId = seedMember(*db, uniqueMemberSeed(7));
        bookId = seedBook(*db, uniqueBookSeed(7));
        const auto copies = copyIdsOf(*db, bookId);
        loanId = returnedAt == nullptr
            ? rawInsertLoan(*db, memberId, copies.at(0), "2025-02-10", "2025-02-24")
            : rawInsertLoan(*db, memberId, copies.at(0), "2025-02-10", "2025-02-24", returnedAt);
    }

    std::unique_ptr<VLMS::LoanHistoryDialog> dialogFor(VLMS::LoanHistoryDialog::Kind kind,
                                                       qint64 id)
    {
        const auto scope = (kind == VLMS::LoanHistoryDialog::Kind::Book
                            || kind == VLMS::LoanHistoryDialog::Kind::Member)
            ? VLMS::Repositories::ArchiveScope::Any
            : VLMS::Repositories::ArchiveScope::Archived;
        return std::make_unique<VLMS::LoanHistoryDialog>(*spec, *loans, events, kind, id,
                                                         QStringLiteral("History"), scope);
    }

    std::unique_ptr<TestDatabase> db;
    std::unique_ptr<VLMS::Repositories::CatalogRepository> catalog;
    std::unique_ptr<VLMS::Repositories::MemberRepository> members;
    std::unique_ptr<VLMS::Repositories::CirculationRepository> circulation;
    VLMS::FakeDialogFactory dialogs;
    VLMS::FakeErrorPresenter errors;
    VLMS::FakeNavigator navigator;
    VLMS::DataEvents events;
    std::unique_ptr<VLMS::BulkRunner> bulk;
    std::unique_ptr<VLMS::LoanWorkflow> loans;
    std::unique_ptr<VLMS::LoanListSpec> spec;
    std::int64_t memberId = 0;
    std::int64_t bookId = 0;
    std::int64_t loanId = 0;
};

QStringList contextMenuTexts(QTableView* table)
{
    QStringList texts;
    QTimer::singleShot(0, table, [&texts] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu == nullptr) {
            return;
        }
        for (QAction* action : menu->actions()) {
            if (!action->isSeparator()) {
                texts.append(action->text());
            }
        }
        menu->close();
    });
    const QPoint cell = table->visualRect(table->model()->index(0, 0)).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, cell,
                            table->viewport()->mapToGlobal(cell), Qt::NoModifier);
    QApplication::sendEvent(table->viewport(), &event);
    QTest::qWait(20);
    return texts;
}

}  // namespace

// The spec promises the same buttons, menu and shortcuts on every list; the
// history dialog was the one list without them.
TEST(LoanHistoryDialog, BookHistoryCtrlNChecksOutThatBook)
{
    FakeLoanHistoryEnv env;
    ASSERT_TRUE(env.ok());
    env.seedLoan("2025-02-20");
    auto dialog = env.dialogFor(VLMS::LoanHistoryDialog::Kind::Book, env.bookId);
    dialog->show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(dialog.get()));

    QTest::keyClick(dialog.get(), Qt::Key_N, Qt::ControlModifier);

    ASSERT_EQ(env.dialogs.checkoutPrefills.size(), 1u);
    EXPECT_EQ(env.dialogs.checkoutPrefills.front().bookId, env.bookId);
    EXPECT_EQ(env.dialogs.checkoutPrefills.front().memberId, 0);
}

TEST(LoanHistoryDialog, MemberHistoryCtrlNChecksOutForThatMember)
{
    FakeLoanHistoryEnv env;
    ASSERT_TRUE(env.ok());
    env.seedLoan("2025-02-20");
    auto dialog = env.dialogFor(VLMS::LoanHistoryDialog::Kind::Member, env.memberId);
    dialog->show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(dialog.get()));

    QTest::keyClick(dialog.get(), Qt::Key_N, Qt::ControlModifier);

    ASSERT_EQ(env.dialogs.checkoutPrefills.size(), 1u);
    EXPECT_EQ(env.dialogs.checkoutPrefills.front().memberId, env.memberId);
}

TEST(LoanHistoryDialog, BookHistoryContextMenuOffersTheButtonsActions)
{
    FakeLoanHistoryEnv env;
    ASSERT_TRUE(env.ok());
    env.seedLoan();
    auto dialog = env.dialogFor(VLMS::LoanHistoryDialog::Kind::Book, env.bookId);
    dialog->show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(dialog.get()));
    dialog->table()->selectRow(0);

    const QStringList texts = contextMenuTexts(dialog->table());

    EXPECT_TRUE(texts.contains(T("circulation.checkout")));
    EXPECT_TRUE(texts.contains(T("circulation.extend")));
    EXPECT_TRUE(texts.contains(T("circulation.return")));
}

// Open or returned comes from the loan record, not from comparing the status
// cell with a translated word.
TEST(LoanHistoryDialog, ReturnAndExtendFollowTheLoanInEveryLanguage)
{
    for (const char* locale : {"en", "ar"}) {
        SCOPED_TRACE(locale);
        FakeLoanHistoryEnv env;
        ASSERT_TRUE(env.ok());
        env.seedLoan("2025-02-20");
        VLMS::Core::Locale::setCode(locale);
        auto dialog = env.dialogFor(VLMS::LoanHistoryDialog::Kind::Book, env.bookId);
        dialog->show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(dialog.get()));
        dialog->table()->selectRow(0);
        QApplication::processEvents();

        QPushButton* ret = buttonWithText(dialog.get(), T("circulation.return"));
        QPushButton* extend = buttonWithText(dialog.get(), T("circulation.extend"));
        ASSERT_NE(ret, nullptr);
        ASSERT_NE(extend, nullptr);
        EXPECT_FALSE(ret->isEnabled()) << "a returned loan cannot be returned again";
        EXPECT_FALSE(extend->isEnabled());

    }
}

TEST(LoanHistoryDialog, ArchivedHistoryOffersNoActions)
{
    FakeLoanHistoryEnv env;
    ASSERT_TRUE(env.ok());
    env.seedLoan("2025-02-20");
    auto dialog = env.dialogFor(VLMS::LoanHistoryDialog::Kind::ArchivedBook, env.bookId);
    dialog->show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(dialog.get()));

    EXPECT_EQ(buttonWithText(dialog.get(), T("circulation.checkout")), nullptr);
    EXPECT_TRUE(dialog->findChildren<QShortcut*>().isEmpty());
    QTest::keyClick(dialog.get(), Qt::Key_N, Qt::ControlModifier);
    EXPECT_TRUE(env.dialogs.checkoutPrefills.empty());
}
