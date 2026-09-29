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

using VLMS::T;
using namespace VLMS;
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

class LoanHistoryDialogTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        Core::Locale::setCode("en");
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(),
                                                                      m_db->resourcesDirectory());
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(),
                                                                     m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_workflows = std::make_unique<LoanWorkflowHarness>(*m_circulation);
        m_spec = std::make_unique<LoanListSpec>(*m_circulation, *m_catalog, *m_members,
                                                *m_workflows->loans);
    }

    void TearDown() override
    {
        m_spec.reset();
        m_workflows.reset();
        m_circulation.reset();
        m_members.reset();
        m_catalog.reset();
        m_db.reset();
        Core::Locale::setCode("en");
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<LoanWorkflowHarness> m_workflows;
    std::unique_ptr<LoanListSpec> m_spec;
};

TEST_F(LoanHistoryDialogTest, BookPinSetsQueryStateBookId)
{
    LoanHistoryDialog dialog(*m_spec, *m_workflows->loans, m_workflows->events,
                             LoanHistoryDialog::Kind::Book, 42, QStringLiteral("Titled"),
                             Repositories::ArchiveScope::Any);
    dialog.show();
    QApplication::processEvents();

    ASSERT_TRUE(dialog.queryState().pinned.has_value());
    EXPECT_EQ(dialog.queryState().pinned->bookId, 42);
    EXPECT_EQ(dialog.queryState().pinned->memberId, 0);
    EXPECT_EQ(dialog.queryState().pinned->copyId, 0);
    EXPECT_EQ(dialog.queryState().scope, Repositories::ArchiveScope::Any);
}

TEST_F(LoanHistoryDialogTest, MemberPinSetsQueryStateMemberId)
{
    LoanHistoryDialog dialog(*m_spec, *m_workflows->loans, m_workflows->events,
                             LoanHistoryDialog::Kind::Member, 7, QStringLiteral("Amina"),
                             Repositories::ArchiveScope::Any);
    dialog.show();
    QApplication::processEvents();

    ASSERT_TRUE(dialog.queryState().pinned.has_value());
    EXPECT_EQ(dialog.queryState().pinned->memberId, 7);
    EXPECT_EQ(dialog.queryState().pinned->bookId, 0);
    EXPECT_EQ(dialog.queryState().pinned->copyId, 0);
}

TEST_F(LoanHistoryDialogTest, ArchivedScopeUsesArchivedOrAny)
{
    LoanHistoryDialog dialog(*m_spec, *m_workflows->loans, m_workflows->events,
                             LoanHistoryDialog::Kind::ArchivedMember, 3, QStringLiteral("Archived"),
                             Repositories::ArchiveScope::Any);
    dialog.show();
    QApplication::processEvents();

    EXPECT_EQ(dialog.queryState().scope, Repositories::ArchiveScope::Any);
    ASSERT_TRUE(dialog.queryState().pinned.has_value());
    EXPECT_EQ(dialog.queryState().pinned->memberId, 3);
    EXPECT_EQ(buttonWithText(&dialog, T("circulation.checkout")), nullptr)
        << "archive history is read-only";
}

TEST_F(LoanHistoryDialogTest, ReturnActionCallsLoanWorkflow)
{
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(1));
    ASSERT_GT(memberId, 0);
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Open Loan Book";
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_FALSE(copies.empty());
    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copies.at(0), "2025-02-10", "2025-02-24");
    ASSERT_GT(loanId, 0);

    LoanHistoryDialog dialog(*m_spec, *m_workflows->loans, m_workflows->events,
                             LoanHistoryDialog::Kind::Book, bookId, QStringLiteral("Open Loan Book"),
                             Repositories::ArchiveScope::Any);
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

    const auto loan = m_circulation->getLoan(loanId);
    ASSERT_TRUE(loan);
    EXPECT_FALSE(loan->returnedAt.empty()) << "return action must complete through LoanWorkflow";
}

}  // namespace
