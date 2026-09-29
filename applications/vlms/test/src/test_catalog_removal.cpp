#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"

#include "QtBridge.h"
#include "ui/ListPage.h"
#include "ui/presentation/BookListSpec.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>

#include <QAbstractItemModel>
#include <QPushButton>
#include <QTableView>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

using namespace VLMS;
using namespace Test;

namespace {

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

void clickDelete(ListPage* page)
{
    auto* button = page->findChild<QPushButton*>(QStringLiteral("listAction_archive"));
    ASSERT_NE(button, nullptr);
    button->click();
}

void tickBook(ListPage* page, std::int64_t bookId)
{
    QTableView* table = page->table();
    ASSERT_NE(table, nullptr);
    for (int row = 0; row < table->model()->rowCount(); ++row) {
        if (table->model()->index(row, 0).data(Qt::UserRole).toLongLong() == bookId) {
            ASSERT_TRUE(table->model()->setData(table->model()->index(row, 0), Qt::Checked,
                                                Qt::CheckStateRole));
            return;
        }
    }
    FAIL() << "book " << bookId << " is not on the catalogue page";
}

bool isArchived(TestDatabase& db, std::int64_t bookId)
{
    return !db.scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId)).isNull();
}

}  // namespace

class test_ui_CatalogRemoval : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(),
                                                                      m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_page.reset();
        m_spec.reset();
        m_workflows.reset();
        m_members.reset();
        m_circulation.reset();
        m_catalog.reset();
        m_db.reset();
    }

    void openPageAndSelect(std::int64_t bookId)
    {
        m_workflows = std::make_unique<BookWorkflowHarness>(*m_catalog, *m_circulation, *m_members);
        m_spec = std::make_unique<BookListSpec>(*m_catalog, *m_workflows->books);
        m_page = std::make_unique<ListPage>(m_spec.get(), m_workflows->events, catalogConfig());
        m_table = m_page->table();
        ASSERT_NE(m_table, nullptr);
        for (int row = 0; row < m_table->model()->rowCount(); ++row) {
            if (m_table->model()->index(row, 0).data(Qt::UserRole).toLongLong() == bookId) {
                m_table->selectRow(row);
                return;
            }
        }
        FAIL() << "book " << bookId << " is not on the catalogue page";
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<BookWorkflowHarness> m_workflows;
    std::unique_ptr<BookListSpec> m_spec;
    std::unique_ptr<ListPage> m_page;
    QTableView* m_table = nullptr;
};

TEST_F(test_ui_CatalogRemoval, ABookWithACopyOutIsRefusedBeforeTheConfirmationIsAsked)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(1));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(2));
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(1));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01",
                            "2026-09-15"),
              0);
    openPageAndSelect(bookId);

    const QList<ModalOutcome> outcomes = runAndAnswerModals(
        [this]() { clickDelete(m_page.get()); },
        {ModalAnswer{T("common.ok"), std::nullopt, {}},
         ModalAnswer{T("common.yes"), std::nullopt, {}}});

    ASSERT_TRUE(outcomes.at(0).appeared);
    EXPECT_EQ(outcomes.at(0).text, T("error.book.hasActiveLoans"));
    EXPECT_FALSE(outcomes.at(1).appeared);
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId))
                    .isNull());
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bystander))
                    .isNull());
}

TEST_F(test_ui_CatalogRemoval, ABookWhoseCopiesAreAllInArchivesTheTitleOnYes)
{
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(3));
    const std::int64_t bystander = seedBook(*m_db, uniqueBookSeed(4));
    openPageAndSelect(bookId);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(m_page.get()); }, T("common.yes"));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, T("catalog.deleteConfirm"));
    EXPECT_FALSE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId))
                     .isNull());
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books").toInt(), 2);
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bystander))
                    .isNull());
}

TEST_F(test_ui_CatalogRemoval, TwoTickedBooksAreArchivedAndTheHighlightedThirdStays)
{
    const std::int64_t first = seedBook(*m_db, uniqueBookSeed(10));
    const std::int64_t second = seedBook(*m_db, uniqueBookSeed(11));
    const std::int64_t highlighted = seedBook(*m_db, uniqueBookSeed(12));
    openPageAndSelect(highlighted);
    tickBook(m_page.get(), first);
    tickBook(m_page.get(), second);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(m_page.get()); }, T("common.yes"));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, QStringLiteral("Archive 2 books?"));
    EXPECT_TRUE(isArchived(*m_db, first));
    EXPECT_TRUE(isArchived(*m_db, second));
    EXPECT_FALSE(isArchived(*m_db, highlighted));
}

TEST_F(test_ui_CatalogRemoval, ATickedBookOnLoanStaysWhileTheOtherIsArchived)
{
    const std::int64_t onLoan = seedBook(*m_db, uniqueBookSeed(20));
    const std::int64_t free = seedBook(*m_db, uniqueBookSeed(21));
    const std::int64_t memberId = seedMember(*m_db, uniqueMemberSeed(20));
    ASSERT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, onLoan).front(), "2026-09-01",
                            "2026-09-15"),
              0);
    openPageAndSelect(onLoan);
    tickBook(m_page.get(), onLoan);
    tickBook(m_page.get(), free);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(m_page.get()); }, T("common.yes"));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_NE(outcome.text, T("catalog.deleteConfirm"));
    EXPECT_FALSE(isArchived(*m_db, onLoan));
    EXPECT_TRUE(isArchived(*m_db, free));
}
