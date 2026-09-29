#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/LoanHistoryDialog.h"
#include "ui/circulation/LoanCheckoutDialog.h"
#include "ui/presentation/LoanListSpec.h"
#include "ui/presentation/QtCirculationLookups.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include "QtBridge.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableView>

#include <gtest/gtest.h>

#include <cstdint>
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

class test_ui_MemberLoansActions : public ::testing::Test {
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
        m_lookups = std::make_unique<QtCirculationLookups>(*m_circulation);
        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_GT(m_memberId, 0);
        m_workflows = std::make_unique<LoanWorkflowHarness>(*m_circulation);
        m_spec = std::make_unique<LoanListSpec>(*m_circulation, *m_catalog, *m_members,
                                                *m_workflows->loans);
    }

    void TearDown() override
    {
        m_spec.reset();
        m_workflows.reset();
        m_lookups.reset();
        m_circulation.reset();
        m_members.reset();
        m_catalog.reset();
        m_db.reset();
        Core::Locale::setCode("en");
    }

    std::unique_ptr<LoanHistoryDialog> makeMemberHistory(const QString& name)
    {
        return std::make_unique<LoanHistoryDialog>(
            *m_spec, *m_workflows->loans, m_workflows->events, LoanHistoryDialog::Kind::Member,
            m_memberId, name, Repositories::ArchiveScope::Any);
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<QtCirculationLookups> m_lookups;
    std::unique_ptr<LoanWorkflowHarness> m_workflows;
    std::unique_ptr<LoanListSpec> m_spec;
    std::int64_t m_memberId = 0;
};

TEST_F(test_ui_MemberLoansActions, AnEmptyHistorySaysSo)
{
    auto dialog = makeMemberHistory(QStringLiteral("Amina Ben Salah"));
    auto* empty = dialog->findChild<QLabel*>(QStringLiteral("loanHistoryEmpty"));
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->isVisibleTo(dialog.get()));
    EXPECT_EQ(empty->text(), T("members.loanHistoryEmpty"));
}

TEST_F(test_ui_MemberLoansActions, TheDialogCarriesTheThreeActions)
{
    auto dialog = makeMemberHistory(QStringLiteral("Amina Ben Salah"));
    EXPECT_NE(buttonWithText(dialog.get(), T("circulation.checkout")), nullptr);
    EXPECT_NE(buttonWithText(dialog.get(), T("circulation.extend")), nullptr);
    EXPECT_NE(buttonWithText(dialog.get(), T("circulation.return")), nullptr);
}

TEST_F(test_ui_MemberLoansActions, TheScopedCheckoutFixesTheMemberAndOffersEveryBook)
{
    BookSeed first = uniqueBookSeed(1);
    first.title = "First Book";
    ASSERT_GT(seedBook(*m_db, first), 0);
    BookSeed second = uniqueBookSeed(2);
    second.title = "Second Book";
    ASSERT_GT(seedBook(*m_db, second), 0);

    ::LoanScope scope;
    scope.memberId = m_memberId;
    scope.label = QStringLiteral("Amina Ben Salah");
    LoanCheckoutDialog dialog(*m_lookups, scope);

    const auto combos = dialog.findChildren<QComboBox*>();
    ASSERT_EQ(combos.size(), 1);
    EXPECT_EQ(combos.first()->count(), 2) << "both books' copies are on offer";

    bool namesTheMember = false;
    for (QLabel* label : dialog.findChildren<QLabel*>()) {
        if (label->text().contains(QStringLiteral("Amina Ben Salah"))) {
            namesTheMember = true;
        }
    }
    EXPECT_TRUE(namesTheMember) << "the fixed member is named in the dialog";
}

TEST_F(test_ui_MemberLoansActions, TheHistoryShowsWhatThisMemberBorrowed)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Borrowed Book";
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_FALSE(copies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(0), "2025-01-10", "2025-01-24"), 0);

    const std::int64_t otherMember = seedMember(*m_db, uniqueMemberSeed(2));
    ASSERT_GT(otherMember, 0);
    BookSeed other = uniqueBookSeed(2);
    other.title = "Other Book";
    const std::int64_t otherBook = seedBook(*m_db, other);
    ASSERT_GT(otherBook, 0);
    const auto otherCopies = copyIdsOf(*m_db, otherBook);
    ASSERT_FALSE(otherCopies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, otherMember, otherCopies.at(0), "2025-02-10", "2025-02-24"), 0);

    auto dialog = makeMemberHistory(QStringLiteral("Amina Ben Salah"));
    QTableView* table = dialog->table();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->model()->rowCount(), 1);
    // title is column 2 in LoanListSpec
    EXPECT_EQ(table->model()->index(0, 2).data().toString(), QStringLiteral("Borrowed Book"));
}

}  // namespace
