#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/ListPage.h"
#include "ui/catalog/LocalNumberDelegate.h"
#include "ui/presentation/BookListSpec.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QHeaderView>
#include <QLineEdit>
#include <QStringList>
#include <QTableView>
#include <QTest>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

namespace {
constexpr int kLocalNumberColumn = 3;

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

QString tableText(QTableView* table, int row, int column)
{
    return table->model()->index(row, column).data(Qt::DisplayRole).toString();
}

QVariant tableData(QTableView* table, int row, int column, int role)
{
    return table->model()->index(row, column).data(role);
}

}  // namespace

class test_ui_CatalogLocalNumber : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(),
                                                                      m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_page.reset();
        m_spec.reset();
        m_workflows.reset();
        m_catalog.reset();
        m_db.reset();
    }

    QTableView* openPage()
    {
        m_workflows = std::make_unique<BookWorkflowHarness>(*m_catalog, *m_circulation);
        m_spec = std::make_unique<BookListSpec>(*m_catalog, *m_workflows->books);
        m_page = std::make_unique<ListPage>(m_spec.get(), m_workflows->events, catalogConfig());
        QTableView* table = m_page->table();
        if (table != nullptr) {
            table->setItemDelegateForColumn(kLocalNumberColumn, new LocalNumberDelegate(table));
        }
        return table;
    }

    /// Seeds one book whose copies carry exactly `numbers`, in that order.
    ///
    /// An empty `numbers` means a book with no live copies. createBook rejects
    /// initialCopyCount < 1 (rule C11b, "error.book.minCopies"), so that case is
    /// reached by seeding one copy and archiving it through saveCopies({}) — the
    /// path Catalogue Delete uses when the librarian removes the last copy row.
    std::int64_t seedWithNumbers(int index,
                                 const std::string& title,
                                 const std::vector<std::string>& numbers)
    {
        BookSeed seed = uniqueBookSeed(index);
        seed.title = title;
        seed.initialCopyCount = numbers.empty() ? 1 : static_cast<int>(numbers.size());
        const std::int64_t bookId = seedBook(*m_db, seed);
        if (bookId <= 0) {
            return 0;
        }
        if (numbers.empty()) {
            return m_catalog->saveCopies(bookId, {}) ? bookId : 0;
        }
        const auto copyIds = copyIdsOf(*m_db, bookId);
        if (copyIds.size() != numbers.size()) {
            return 0;
        }
        for (std::size_t i = 0; i < numbers.size(); ++i) {
            if (!rawSetCopyLocalId(*m_db, copyIds[i], numbers[i])) {
                return 0;
            }
        }
        return bookId;
    }

    /// Types `text` into the page's search box and commits (ListPage debounces).
    void search(const QString& text)
    {
        auto* box = m_page->findChild<QLineEdit*>(QStringLiteral("listSearch"));
        ASSERT_NE(box, nullptr);
        box->setText(text);
        QTest::keyClick(box, Qt::Key_Return);
        QApplication::processEvents();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<BookWorkflowHarness> m_workflows;
    std::unique_ptr<BookListSpec> m_spec;
    std::unique_ptr<ListPage> m_page;
};

TEST_F(test_ui_CatalogLocalNumber, ColumnsStartWithTitleAuthorCategoryLocalNumber)
{
    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->columnCount(), 6);
    EXPECT_EQ(table->model()->headerData(0, Qt::Horizontal).toString(), QStringLiteral("Title"));
    EXPECT_EQ(table->model()->headerData(1, Qt::Horizontal).toString(), QStringLiteral("Author"));
    EXPECT_EQ(table->model()->headerData(2, Qt::Horizontal).toString(), QStringLiteral("Category"));
    EXPECT_EQ(table->model()->headerData(3, Qt::Horizontal).toString(), QStringLiteral("Local no."));
    EXPECT_EQ(table->model()->headerData(4, Qt::Horizontal).toString(), QStringLiteral("Copies"));
    EXPECT_EQ(table->model()->headerData(5, Qt::Horizontal).toString(), QStringLiteral("Available"));

    for (int column = 0; column < table->model()->columnCount(); ++column) {
        const QString header = table->model()->headerData(column, Qt::Horizontal).toString();
        EXPECT_NE(header, QStringLiteral("ISBN"));
        EXPECT_NE(header, QStringLiteral("Language"));
    }
}

TEST_F(test_ui_CatalogLocalNumber, TheDelegateIsInstalledOnTheLocalNumberColumn)
{
    // Nothing else here notices if this wiring is missing or points at the
    // wrong column: the item carries no text, only the numbers under
    // LocalNumberDelegate::kNumbersRole, so an unpainted column would still
    // pass every other assertion in this file.
    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    EXPECT_NE(qobject_cast<LocalNumberDelegate*>(table->itemDelegateForColumn(kLocalNumberColumn)),
              nullptr);
}

TEST_F(test_ui_CatalogLocalNumber, ABookWithNoCopiesShowsNoNumbers)
{
    ASSERT_GT(seedWithNumbers(1, "Bare", {}), 0);
    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 1);

    const QStringList numbers =
        tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kNumbersRole).toStringList();
    EXPECT_TRUE(numbers.isEmpty());
    EXPECT_EQ(LocalNumberDelegate::cellText(numbers), QString::fromUtf8("—"));
}

TEST_F(test_ui_CatalogLocalNumber, ASingleCopyShowsItsNumberAlone)
{
    ASSERT_GT(seedWithNumbers(2, "Alone", {"1042"}), 0);
    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 1);

    const QStringList numbers =
        tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kNumbersRole).toStringList();
    EXPECT_EQ(numbers, (QStringList{QStringLiteral("1042")}));
    EXPECT_EQ(LocalNumberDelegate::cellText(numbers), QStringLiteral("1042"));
}

TEST_F(test_ui_CatalogLocalNumber, SeveralCopiesShowTheLowestNumberAndTheCount)
{
    // Out of order, and chosen so a lexical sort would read 1040, 104, 99.
    ASSERT_GT(seedWithNumbers(3, "Many", {"1040", "99", "104"}), 0);
    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 1);

    const QStringList numbers =
        tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kNumbersRole).toStringList();
    EXPECT_EQ(numbers, (QStringList{QStringLiteral("99"), QStringLiteral("104"),
                                    QStringLiteral("1040")}));
    EXPECT_EQ(LocalNumberDelegate::cellText(numbers), QStringLiteral("99 (+2)"));
}

TEST_F(test_ui_CatalogLocalNumber, TheCopiesOutOnLoanReachTheCell)
{
    const std::int64_t bookId = seedWithNumbers(20, "Lent Copies", {"7", "8", "9"});
    ASSERT_GT(bookId, 0);

    const auto copyIds = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copyIds.size(), 3U);

    MemberSeed member = uniqueMemberSeed(20);
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    // Unreturned: what "out on loan" means here is a loan row with no
    // returned_at, whatever its dates say.
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIds[1], "2020-01-01", "2020-01-15"), 0);

    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 1);

    EXPECT_EQ(tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kNumbersRole).toStringList(),
              (QStringList{QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9")}));
    EXPECT_EQ(tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kOnLoanRole).toStringList(),
              QStringList{QStringLiteral("8")})
        << "the delegate colours by this subset, so only the lent copy belongs in it";
}

TEST_F(test_ui_CatalogLocalNumber, ABookWithEveryCopyOnTheShelfCarriesAnEmptyOnLoanList)
{
    ASSERT_GT(seedWithNumbers(21, "Shelved", {"11", "12"}), 0);

    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 1);

    EXPECT_TRUE(
        tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kOnLoanRole).toStringList().isEmpty());
}

TEST_F(test_ui_CatalogLocalNumber, ClickingTheLocalNumberHeaderSortsByTheLowestNumber)
{
    ASSERT_GT(seedWithNumbers(4, "High", {"500"}), 0);
    ASSERT_GT(seedWithNumbers(5, "Low", {"900", "42"}), 0);

    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 2);

    table->model()->sort(kLocalNumberColumn, Qt::AscendingOrder);
    EXPECT_EQ(tableText(table, 0, 0), QStringLiteral("Low"));

    table->model()->sort(kLocalNumberColumn, Qt::DescendingOrder);
    EXPECT_EQ(tableText(table, 0, 0), QStringLiteral("High"));
}

TEST_F(test_ui_CatalogLocalNumber, BooksWithoutCopiesSortLastInBothDirections)
{
    ASSERT_GT(seedWithNumbers(6, "Has a copy", {"7"}), 0);
    ASSERT_GT(seedWithNumbers(7, "No copies", {}), 0);

    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 2);

    table->model()->sort(kLocalNumberColumn, Qt::AscendingOrder);
    EXPECT_EQ(tableText(table, 1, 0), QStringLiteral("No copies"));

    table->model()->sort(kLocalNumberColumn, Qt::DescendingOrder);
    EXPECT_EQ(tableText(table, 1, 0), QStringLiteral("No copies"));
}

TEST_F(test_ui_CatalogLocalNumber, CopiesAndAvailableLandInTheirOwnColumns)
{
    // Copies and Available are seeded so the two numbers differ (1 vs 0):
    // swapping the two writes would pass with equal values, so an assertion
    // built on equal copies could not catch it.
    MemberSeed member = uniqueMemberSeed(1);
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    const std::int64_t bookId = seedWithNumbers(8, "On loan", {"77"});
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copies.size(), 1u);

    const std::int64_t loanId =
        rawInsertLoan(*m_db, memberId, copies.at(0), "2020-01-01", "2020-01-15");
    ASSERT_GT(loanId, 0);

    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 1);

    const QString copiesText = tableText(table, 0, 4);
    const QString availableText = tableText(table, 0, 5);
    EXPECT_EQ(copiesText, QStringLiteral("1"));
    EXPECT_EQ(availableText, QStringLiteral("0"));
    EXPECT_NE(copiesText, availableText);
}

TEST_F(test_ui_CatalogLocalNumber, SearchingALocalNumberFindsTheBookHoldingThatCopy)
{
    ASSERT_GT(seedWithNumbers(9, "Wanted", {"4100"}), 0);
    ASSERT_GT(seedWithNumbers(10, "Other", {"77"}), 0);

    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 2);

    search(QStringLiteral("4100"));
    ASSERT_EQ(table->model()->rowCount(), 1);
    EXPECT_EQ(tableText(table, 0, 0), QStringLiteral("Wanted"));
}

TEST_F(test_ui_CatalogLocalNumber, TheMatchedNumberLeadsTheCell)
{
    // 1002 is the lowest, so it is what the cell shows unsearched. Searching a
    // higher copy has to bring that copy to the front, or the row gives the
    // librarian no sign of why it is on screen.
    ASSERT_GT(seedWithNumbers(11, "Several", {"1002", "15000", "15001"}), 0);

    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->model()->rowCount(), 1);
    EXPECT_EQ(LocalNumberDelegate::cellText(
                  tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kNumbersRole)
                      .toStringList()),
              QStringLiteral("1002 (+2)"));

    search(QStringLiteral("15000"));
    ASSERT_EQ(table->model()->rowCount(), 1);
    // The matched copy is marked, not moved: the drop-down still reads in
    // accession order, exactly as it does for a librarian's own pick.
    EXPECT_EQ(tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kNumbersRole).toStringList(),
              (QStringList{QStringLiteral("1002"), QStringLiteral("15000"),
                           QStringLiteral("15001")}));
    EXPECT_EQ(tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kSelectedRole).toString(),
              QStringLiteral("15000"));
    EXPECT_EQ(LocalNumberDelegate::cellText(
                  tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kNumbersRole)
                      .toStringList(),
                  tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kSelectedRole)
                      .toString()),
              QStringLiteral("15000 (+2)"));
}

TEST_F(test_ui_CatalogLocalNumber, ATitleSearchLeavesTheCellOnTheLowestNumber)
{
    ASSERT_GT(seedWithNumbers(12, "Several", {"1002", "15000"}), 0);

    auto* table = openPage();
    ASSERT_NE(table, nullptr);
    search(QStringLiteral("Several"));
    ASSERT_EQ(table->model()->rowCount(), 1);
    EXPECT_EQ(tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kNumbersRole).toStringList(),
              (QStringList{QStringLiteral("1002"), QStringLiteral("15000")}));
    EXPECT_TRUE(
        tableData(table, 0, kLocalNumberColumn, LocalNumberDelegate::kSelectedRole).toString().isEmpty());
}
