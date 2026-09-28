#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"

#include "ui/catalog/CatalogPage.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>

#include <QTableWidget>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

using namespace VLMS;
using namespace Test;

class test_ui_CatalogRemoval : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_page.reset();
        m_circulation.reset();
        m_catalog.reset();
        m_db.reset();
    }

    void openPageAndSelect(std::int64_t bookId)
    {
        m_page = std::make_unique<CatalogPage>(*m_catalog, *m_circulation);
        m_table = m_page->findChild<QTableWidget*>();
        ASSERT_NE(m_table, nullptr);
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (m_table->item(row, 0)->data(Qt::UserRole).toLongLong() == bookId) {
                m_table->selectRow(row);
                return;
            }
        }
        FAIL() << "book " << bookId << " is not on the catalogue page";
    }

    void clickDelete() { clickButtonWithText(m_page.get(), qs(Core::Strings::t("catalog.delete"))); }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<CatalogPage> m_page;
    QTableWidget* m_table = nullptr;
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

    // One box, and it is the refusal -- not a confirmation followed by a no.
    const QList<ModalOutcome> outcomes =
        runAndAnswerModals([this]() { clickDelete(); },
                           {ModalAnswer{qs(Core::Strings::t("common.ok")), std::nullopt, {}},
                            ModalAnswer{qs(Core::Strings::t("common.yes")), std::nullopt, {}}});

    ASSERT_TRUE(outcomes.at(0).appeared);
    EXPECT_EQ(outcomes.at(0).text, qs(Core::Strings::t("error.book.hasActiveLoans")));
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
        runAndAnswerModal([this]() { clickDelete(); }, qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Core::Strings::t("catalog.deleteConfirm")));
    EXPECT_FALSE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bookId))
                     .isNull());
    // Archived, never destroyed -- and the neighbour is untouched.
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books").toInt(), 2);
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM books WHERE id = " + std::to_string(bystander))
                    .isNull());
}

namespace {

void tickBook(QTableWidget* table, std::int64_t bookId)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem* item = table->item(row, 0);
        if (item != nullptr && item->data(Qt::UserRole).toLongLong() == bookId) {
            item->setCheckState(Qt::Checked);
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

TEST_F(test_ui_CatalogRemoval, TwoTickedBooksAreArchivedAndTheHighlightedThirdStays)
{
    const std::int64_t first = seedBook(*m_db, uniqueBookSeed(10));
    const std::int64_t second = seedBook(*m_db, uniqueBookSeed(11));
    const std::int64_t highlighted = seedBook(*m_db, uniqueBookSeed(12));
    openPageAndSelect(highlighted);
    tickBook(m_table, first);
    tickBook(m_table, second);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(); }, qs(Core::Strings::t("common.yes")));

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
    tickBook(m_table, onLoan);
    tickBook(m_table, free);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(); }, qs(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_NE(outcome.text, qs(Core::Strings::t("catalog.deleteConfirm")));
    EXPECT_FALSE(isArchived(*m_db, onLoan));
    EXPECT_TRUE(isArchived(*m_db, free));
}
