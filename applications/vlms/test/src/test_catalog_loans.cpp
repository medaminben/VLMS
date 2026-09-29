#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/ListPage.h"
#include "ui/LoanHistoryDialog.h"
#include "ui/presentation/BookListSpec.h"
#include "ui/presentation/LoanListSpec.h"
#include "WorkflowTestSupport.h"
#include "ui/circulation/LoanCheckoutDialog.h"
#include "ui/presentation/QtCirculationLookups.h"

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

ListPage::Config catalogConfig()
{
    ListPage::Config cfg;
    cfg.titleKey = QStringLiteral("page.catalog.title");
    cfg.bodyKey = QStringLiteral("page.catalog.body");
    cfg.searchPlaceholderKey = QStringLiteral("catalog.searchPlaceholder");
    cfg.imageObjectName = QStringLiteral("bookCover");
    cfg.imageBounds = bookCoverPreviewBounds();
    return cfg;
}

class test_ui_CatalogLoans : public ::testing::Test {
protected:
    void SetUp() override
    {
        Core::Locale::setCode("en");
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_lookups = std::make_unique<QtCirculationLookups>(*m_circulation);
        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_GT(m_memberId, 0);
    }

    void TearDown() override
    {
        m_page.reset();
        m_spec.reset();
        m_workflows.reset();
        m_lookups.reset();
        m_members.reset();
        m_circulation.reset();
        m_catalog.reset();
        m_db.reset();
        Core::Locale::setCode("en");
    }

    void openPage()
    {
        m_workflows = std::make_unique<BookWorkflowHarness>(*m_catalog, *m_circulation, *m_members);
        m_spec = std::make_unique<BookListSpec>(*m_catalog, *m_workflows->books);
        m_page = std::make_unique<ListPage>(m_spec.get(), m_workflows->events, catalogConfig());
        m_table = m_page->table();
        ASSERT_NE(m_table, nullptr);
    }

    std::unique_ptr<LoanHistoryDialog> makeBookHistory(qint64 bookId, const QString& title)
    {
        return std::make_unique<LoanHistoryDialog>(
            *m_workflows->loanHistorySpec, *m_workflows->loans, m_workflows->events,
            LoanHistoryDialog::Kind::Book, bookId, title, Repositories::ArchiveScope::Any);
    }

    void selectRowWithTitle(const QString& title)
    {
        for (int row = 0; row < m_table->model()->rowCount(); ++row) {
            if (m_table->model()->index(row, 0).data(Qt::DisplayRole).toString() == title) {
                m_table->selectRow(row);
                return;
            }
        }
        FAIL() << "no row titled " << title.toStdString();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<QtCirculationLookups> m_lookups;
    std::unique_ptr<BookWorkflowHarness> m_workflows;
    std::unique_ptr<BookListSpec> m_spec;
    std::unique_ptr<ListPage> m_page;
    QTableView* m_table = nullptr;
    std::int64_t m_memberId = 0;
};

TEST_F(test_ui_CatalogLoans, TheLoansButtonOpensEvenWithNoHistory)
{
    BookSeed untouched = uniqueBookSeed(1);
    untouched.title = "Untouched Book";
    ASSERT_GT(seedBook(*m_db, untouched), 0);

    openPage();
    auto* loans = m_page->findChild<QPushButton*>(QStringLiteral("listAction_loans"));
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

    m_workflows = std::make_unique<BookWorkflowHarness>(*m_catalog, *m_circulation, *m_members);
    auto dialog = makeBookHistory(bookId, QStringLiteral("Untouched Book"));
    auto* empty = dialog->findChild<QLabel*>(QStringLiteral("loanHistoryEmpty"));
    ASSERT_NE(empty, nullptr);
    EXPECT_TRUE(empty->isVisibleTo(dialog.get()));
    EXPECT_EQ(empty->text(), T("catalog.loanHistoryEmpty"));

    QTableView* table = dialog->table();
    ASSERT_NE(table, nullptr);
    EXPECT_FALSE(table->isVisibleTo(dialog.get())) << "an empty grid says nothing";
}

TEST_F(test_ui_CatalogLoans, TheScopedCheckoutOffersOnlyThisBooksFreeCopies)
{
    BookSeed wanted = uniqueBookSeed(1);
    wanted.title = "Wanted Book";
    wanted.initialCopyCount = 2;
    const std::int64_t wantedId = seedBook(*m_db, wanted);
    ASSERT_GT(wantedId, 0);

    BookSeed other = uniqueBookSeed(2);
    other.title = "Other Book";
    other.initialCopyCount = 2;
    ASSERT_GT(seedBook(*m_db, other), 0);

    ::LoanScope scope;
    scope.bookId = wantedId;
    scope.label = QStringLiteral("Wanted Book");
    LoanCheckoutDialog dialog(*m_lookups, scope);

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

    m_workflows = std::make_unique<BookWorkflowHarness>(*m_catalog, *m_circulation, *m_members);
    auto dialog = makeBookHistory(bookId, QStringLiteral("Two States"));
    QTableView* table = dialog->table();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 2);

    auto* extend = buttonWithText(dialog.get(), T("circulation.extend"));
    auto* ret = buttonWithText(dialog.get(), T("circulation.return"));
    ASSERT_NE(extend, nullptr);
    ASSERT_NE(ret, nullptr);

    const auto rowWithBorrowDate = [table](const QString& date) {
        for (int row = 0; row < table->model()->rowCount(); ++row) {
            // borrowed column is index 3 in LoanListSpec
            if (table->model()->index(row, 3).data().toString() == date) {
                return row;
            }
        }
        return -1;
    };

    const int openRow = rowWithBorrowDate(QStringLiteral("2025-02-10"));
    ASSERT_GE(openRow, 0);
    table->selectRow(openRow);
    EXPECT_TRUE(extend->isEnabled());
    EXPECT_TRUE(ret->isEnabled());

    const int returnedRow = rowWithBorrowDate(QStringLiteral("2025-01-10"));
    ASSERT_GE(returnedRow, 0);
    table->selectRow(returnedRow);
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

    BookSeed other = uniqueBookSeed(2);
    other.title = "Other Book";
    const std::int64_t otherId = seedBook(*m_db, other);
    ASSERT_GT(otherId, 0);
    const auto otherCopies = copyIdsOf(*m_db, otherId);
    ASSERT_FALSE(otherCopies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, otherCopies.at(0), "2025-04-10", "2025-04-24"), 0);

    m_workflows = std::make_unique<BookWorkflowHarness>(*m_catalog, *m_circulation, *m_members);
    auto dialog = makeBookHistory(bookId, QStringLiteral("Two Copies"));
    QTableView* table = dialog->table();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->model()->rowCount(), 3) << "the history covers both copies, and only this book";
    for (int row = 0; row < table->model()->rowCount(); ++row) {
        EXPECT_NE(table->model()->index(row, 3).data().toString(), QStringLiteral("2025-04-10"))
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

    BookSeed other = uniqueBookSeed(2);
    other.title = "Other Book";
    const std::int64_t otherId = seedBook(*m_db, other);
    ASSERT_GT(otherId, 0);
    const auto otherCopies = copyIdsOf(*m_db, otherId);
    ASSERT_FALSE(otherCopies.empty());
    ASSERT_GT(rawInsertLoan(*m_db, m_memberId, otherCopies.at(0), "2025-05-10", "2025-05-24"), 0);

    m_workflows = std::make_unique<BookWorkflowHarness>(*m_catalog, *m_circulation, *m_members);
    auto dialog = makeBookHistory(bookId, QStringLiteral("Named Book"));
    QTableView* table = dialog->table();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 1);
    const QString first = table->model()->index(0, 0).data().toString();
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

    m_workflows = std::make_unique<BookWorkflowHarness>(*m_catalog, *m_circulation, *m_members);
    auto dialog = makeBookHistory(bookId, QStringLiteral("Titled Book"));
    EXPECT_TRUE(dialog->windowTitle().contains(QStringLiteral("Titled Book")));
    EXPECT_FALSE(dialog->windowTitle().contains(QStringLiteral("{name}")))
        << "the placeholder was not replaced";
}

}  // namespace
