#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/catalog/BookLoansDialog.h"
#include "ui/catalog/CatalogPage.h"
#include "ui/circulation/LoanCheckoutDialog.h"

#include <VLMS/Repositories/CatalogRepository.h>
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

class test_ui_CatalogLoans : public ::testing::Test {
protected:
    void SetUp() override
    {
        Locale::setCode("en");
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());
        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_GT(m_memberId, 0);
    }

    void TearDown() override
    {
        m_page.reset();
        m_circulation.reset();
        m_catalog.reset();
        m_db.reset();
        Locale::setCode("en");
    }

    /// Builds the page after the rows are seeded: it queries on construction.
    void openPage()
    {
        m_page = std::make_unique<CatalogPage>(*m_catalog, *m_circulation);
        m_table = m_page->findChild<QTableWidget*>();
        ASSERT_NE(m_table, nullptr);
    }

    void selectRowWithTitle(const QString& title)
    {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (m_table->item(row, 0) != nullptr && m_table->item(row, 0)->text() == title) {
                m_table->selectRow(row);
                return;
            }
        }
        FAIL() << "no row titled " << title.toStdString();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::unique_ptr<CatalogPage> m_page;
    QTableWidget* m_table = nullptr;
    std::int64_t m_memberId = 0;
};

TEST_F(test_ui_CatalogLoans, TheLoansButtonOpensEvenWithNoHistory)
{
    BookSeed untouched = uniqueBookSeed(1);
    untouched.title = "Untouched Book";
    ASSERT_GT(seedBook(*m_db, untouched), 0);

    openPage();
    auto* loans = buttonWithText(m_page.get(), T("catalog.loans"));
    ASSERT_NE(loans, nullptr);

    selectRowWithTitle(QStringLiteral("Untouched Book"));
    EXPECT_TRUE(loans->isEnabled()) << "a book never borrowed is the one about to be";
}

TEST_F(test_ui_CatalogLoans, AnEmptyHistorySaysSoAndHidesTheGrid)
{
    BookSeed untouched = uniqueBookSeed(1);
    untouched.title = "Untouched Book";
    const std::int64_t bookId = seedBook(*m_db, untouched);
    ASSERT_GT(bookId, 0);

    BookLoansDialog dialog(*m_circulation, bookId, QStringLiteral("Untouched Book"));
    auto* empty = dialog.findChild<QLabel*>(QStringLiteral("loanHistoryEmpty"));
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->isVisibleTo(&dialog));
    EXPECT_EQ(empty->text(), T("catalog.loanHistoryEmpty"));

    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_FALSE(table->isVisibleTo(&dialog)) << "an empty grid says nothing";
}

TEST_F(test_ui_CatalogLoans, TheScopedCheckoutOffersOnlyThisBooksFreeCopies)
{
    BookSeed wanted = uniqueBookSeed(1);
    wanted.title = "Wanted Book";
    wanted.initialCopyCount = 2;
    const std::int64_t wantedId = seedBook(*m_db, wanted);
    ASSERT_GT(wantedId, 0);

    // A second book with free copies: without the scope these would be offered.
    BookSeed other = uniqueBookSeed(2);
    other.title = "Other Book";
    other.initialCopyCount = 2;
    ASSERT_GT(seedBook(*m_db, other), 0);

    LoanScope scope;
    scope.bookId = wantedId;
    scope.label = QStringLiteral("Wanted Book");
    LoanCheckoutDialog dialog(*m_circulation, scope);

    const auto combos = dialog.findChildren<QComboBox*>();
    ASSERT_FALSE(combos.isEmpty());
    QComboBox* copies = combos.last();
    EXPECT_EQ(copies->count(), 2) << "only Wanted Book's two copies";
    for (int i = 0; i < copies->count(); ++i) {
        EXPECT_TRUE(copies->itemText(i).contains(QStringLiteral("Wanted Book")))
            << copies->itemText(i).toStdString();
    }
}

TEST_F(test_ui_CatalogLoans, ExtendAndReturnFollowTheSelectedRow)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Two States";
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copies.size(), 2U);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(0),
                            "2025-01-10", "2025-01-24", "2025-01-20"), 0);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(1), "2025-02-10", "2025-02-24"), 0);

    BookLoansDialog dialog(*m_circulation, bookId, QStringLiteral("Two States"));
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 2);

    auto* extend = buttonWithText(&dialog, T("circulation.extend"));
    auto* ret = buttonWithText(&dialog, T("circulation.return"));
    ASSERT_NE(extend, nullptr);
    ASSERT_NE(ret, nullptr);

    const auto rowWithBorrowDate = [table](const QString& date) {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 2)->text() == date) {
                return row;
            }
        }
        return -1;
    };

    const int openRow = rowWithBorrowDate(QStringLiteral("2025-02-10"));
    ASSERT_GE(openRow, 0);
    table->setCurrentCell(openRow, 0);
    EXPECT_TRUE(extend->isEnabled());
    EXPECT_TRUE(ret->isEnabled());

    const int returnedRow = rowWithBorrowDate(QStringLiteral("2025-01-10"));
    ASSERT_GE(returnedRow, 0);
    table->setCurrentCell(returnedRow, 0);
    EXPECT_FALSE(extend->isEnabled()) << "a returned loan cannot be extended";
    EXPECT_FALSE(ret->isEnabled());
}

TEST_F(test_ui_CatalogLoans, TheHistoryListsEveryLoanOfEveryCopy)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Two Copies";
    seed.initialCopyCount = 2;
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copies.size(), 2U);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(0),
                            "2025-01-10", "2025-01-24", "2025-01-20"), 0);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(1), "2025-02-10", "2025-02-24"), 0);
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, copies.at(0), "2025-03-10", "2025-03-24"), 0);

    // Another book, also borrowed: its loans must stay out of this history.
    BookSeed other = uniqueBookSeed(2);
    other.title = "Other Book";
    const std::int64_t otherId = seedBook(*m_db, other);
    ASSERT_GT(otherId, 0);
    const auto otherCopies = copyIdsOf(*m_db, otherId);
    ASSERT_FALSE(otherCopies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, otherCopies.at(0), "2025-04-10", "2025-04-24"), 0);

    BookLoansDialog dialog(*m_circulation, bookId, QStringLiteral("Two Copies"));
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 3) << "the history covers both copies, and only this book";
    for (int row = 0; row < table->rowCount(); ++row) {
        EXPECT_NE(table->item(row, 2)->text(), QStringLiteral("2025-04-10"))
            << "another book's loan leaked into this history";
    }
}

TEST_F(test_ui_CatalogLoans, TheHistoryNamesTheMemberNotTheTitle)
{
    MemberSeed borrower = uniqueMemberSeed(7);
    borrower.firstName = "Salim";
    borrower.lastName = "Trabelsi";
    const std::int64_t memberId = seedMember(*m_db, borrower);
    ASSERT_GT(memberId, 0);

    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Named Book";
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_FALSE(copies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copies.at(0), "2025-01-10", "2025-01-24"), 0);

    // A different book, borrowed by the fixture's member: one row, one name.
    BookSeed other = uniqueBookSeed(2);
    other.title = "Other Book";
    const std::int64_t otherId = seedBook(*m_db, other);
    ASSERT_GT(otherId, 0);
    const auto otherCopies = copyIdsOf(*m_db, otherId);
    ASSERT_FALSE(otherCopies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, otherCopies.at(0), "2025-05-10", "2025-05-24"), 0);

    BookLoansDialog dialog(*m_circulation, bookId, QStringLiteral("Named Book"));
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 1);
    const QString first = table->item(0, 0)->text();
    EXPECT_NE(first, QStringLiteral("Named Book")) << "the title column should be gone";
    EXPECT_TRUE(first.contains(QStringLiteral("Trabelsi")))
        << "first column: " << first.toStdString();
}

TEST_F(test_ui_CatalogLoans, TheWindowTitleCarriesTheBookTitle)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Titled Book";
    const std::int64_t bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);

    BookLoansDialog dialog(*m_circulation, bookId, QStringLiteral("Titled Book"));
    EXPECT_TRUE(dialog.windowTitle().contains(QStringLiteral("Titled Book")));
    EXPECT_FALSE(dialog.windowTitle().contains(QStringLiteral("{name}")))
        << "the placeholder was not replaced";
}

}  // namespace
