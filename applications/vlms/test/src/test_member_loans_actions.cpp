#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/circulation/LoanCheckoutDialog.h"
#include "ui/members/MemberLoansDialog.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include "QtBridge.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

using VLMS::Locale;
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
        Locale::setCode("en");
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_GT(m_memberId, 0);
    }

    void TearDown() override
    {
        m_circulation.reset();
        m_db.reset();
        Locale::setCode("en");
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::int64_t m_memberId = 0;
};

TEST_F(test_ui_MemberLoansActions, AnEmptyHistorySaysSo)
{
    MemberLoansDialog dialog(*m_circulation, m_memberId, QStringLiteral("Amina Ben Salah"));
    auto* empty = dialog.findChild<QLabel*>(QStringLiteral("loanHistoryEmpty"));
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->isVisibleTo(&dialog));
    EXPECT_EQ(empty->text(), T("members.loanHistoryEmpty"));
}

TEST_F(test_ui_MemberLoansActions, TheDialogCarriesTheThreeActions)
{
    MemberLoansDialog dialog(*m_circulation, m_memberId, QStringLiteral("Amina Ben Salah"));
    EXPECT_NE(buttonWithText(&dialog, T("circulation.checkout")), nullptr);
    EXPECT_NE(buttonWithText(&dialog, T("circulation.extend")), nullptr);
    EXPECT_NE(buttonWithText(&dialog, T("circulation.return")), nullptr);
}

TEST_F(test_ui_MemberLoansActions, TheScopedCheckoutFixesTheMemberAndOffersEveryBook)
{
    BookSeed first = uniqueBookSeed(1);
    first.title = "First Book";
    ASSERT_GT(seedBook(*m_db, first), 0);
    BookSeed second = uniqueBookSeed(2);
    second.title = "Second Book";
    ASSERT_GT(seedBook(*m_db, second), 0);

    LoanScope scope;
    scope.memberId = m_memberId;
    scope.label = QStringLiteral("Amina Ben Salah");
    LoanCheckoutDialog dialog(*m_circulation, scope);

    // One combo only: the copy one. The member is a label now.
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

    // Another member's loan of another book: it must not appear here.
    const std::int64_t otherMember = seedMember(*m_db, uniqueMemberSeed(2));
    ASSERT_GT(otherMember, 0);
    BookSeed other = uniqueBookSeed(2);
    other.title = "Other Book";
    const std::int64_t otherBook = seedBook(*m_db, other);
    ASSERT_GT(otherBook, 0);
    const auto otherCopies = copyIdsOf(*m_db, otherBook);
    ASSERT_FALSE(otherCopies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, otherMember, otherCopies.at(0), "2025-02-10", "2025-02-24"), 0);

    MemberLoansDialog dialog(*m_circulation, m_memberId, QStringLiteral("Amina Ben Salah"));
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("Borrowed Book"));
}

}  // namespace
