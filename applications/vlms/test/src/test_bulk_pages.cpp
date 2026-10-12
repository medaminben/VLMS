#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"

#include "ui/views/ListPage.h"
#include "ui/presentation/LoanListSpec.h"
#include "WorkflowTestSupport.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>

#include <QAbstractItemModel>
#include <QTableView>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

namespace {

ListPage::Config circulationConfig()
{
    ListPage::Config cfg;
    cfg.titleKey = QStringLiteral("page.circulation.title");
    cfg.bodyKey = QStringLiteral("page.circulation.body");
    cfg.searchPlaceholderKey = QStringLiteral("circulation.searchPlaceholder");
    cfg.imageObjectName = QStringLiteral("bookCover");
    cfg.imageBounds = bookCoverPreviewBounds();
    cfg.secondImageObjectName = QStringLiteral("loanMemberPhoto");
    cfg.secondImageBounds = bookCoverPreviewBounds();
    return cfg;
}

void tickLoan(QTableView* table, std::int64_t loanId)
{
    for (int row = 0; row < table->model()->rowCount(); ++row) {
        const QModelIndex index = table->model()->index(row, 0);
        if (index.data(Qt::UserRole).toLongLong() == loanId) {
            table->model()->setData(index, Qt::Checked, Qt::CheckStateRole);
            return;
        }
    }
    FAIL() << "loan " << loanId << " is not on the page";
}

bool loanArchived(TestDatabase& db, std::int64_t loanId)
{
    return !db.scalar("SELECT archived_at FROM loans WHERE id = " + std::to_string(loanId)).isNull();
}

}  // namespace

class test_ui_BulkPages : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_memberRepo =
            std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_page.reset();
        m_spec.reset();
        m_loanWorkflows.reset();
        m_circulation.reset();
        m_memberRepo.reset();
        m_catalog.reset();
        m_db.reset();
    }

    std::int64_t seedReturnedLoan(int n)
    {
        const std::int64_t member = seedMember(*m_db, uniqueMemberSeed(n));
        const std::int64_t book = seedBook(*m_db, uniqueBookSeed(n));
        return rawInsertLoan(*m_db, member, copyIdsOf(*m_db, book).front(), "2026-08-01",
                             "2026-08-15", "2026-08-10");
    }

    void openAndSelect(std::int64_t loanId)
    {
        m_loanWorkflows = std::make_unique<LoanWorkflowHarness>(*m_circulation);
        m_spec = std::make_unique<LoanListSpec>(*m_circulation, *m_catalog, *m_memberRepo,
                                                *m_loanWorkflows->loans);
        m_page = std::make_unique<ListPage>(m_spec.get(), m_loanWorkflows->events, circulationConfig());
        m_table = m_page->table();
        ASSERT_NE(m_table, nullptr);
        for (int row = 0; row < m_table->model()->rowCount(); ++row) {
            if (m_table->model()->index(row, 0).data(Qt::UserRole).toLongLong() == loanId) {
                m_table->selectRow(row);
                return;
            }
        }
        FAIL() << "loan " << loanId << " is not on the page";
    }

    void clickDelete()
    {
        clickButtonWithText(m_page.get(), QString::fromStdString(Core::Strings::t("circulation.delete")));
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::MemberRepository> m_memberRepo;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<LoanWorkflowHarness> m_loanWorkflows;
    std::unique_ptr<LoanListSpec> m_spec;
    std::unique_ptr<ListPage> m_page;
    QTableView* m_table = nullptr;
};

TEST_F(test_ui_BulkPages, TwoTickedReturnedLoansAreArchivedAndTheThirdStays)
{
    const std::int64_t first = seedReturnedLoan(1);
    const std::int64_t second = seedReturnedLoan(2);
    const std::int64_t highlighted = seedReturnedLoan(3);
    openAndSelect(highlighted);
    tickLoan(m_table, first);
    tickLoan(m_table, second);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(); }, QString::fromStdString(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, QStringLiteral("Archive 2 loans?"));
    EXPECT_TRUE(loanArchived(*m_db, first));
    EXPECT_TRUE(loanArchived(*m_db, second));
    EXPECT_FALSE(loanArchived(*m_db, highlighted));
}

TEST_F(test_ui_BulkPages, NothingTickedArchivesOnlyTheHighlightedLoan)
{
    const std::int64_t highlighted = seedReturnedLoan(4);
    const std::int64_t bystander = seedReturnedLoan(5);
    openAndSelect(highlighted);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(); }, QString::fromStdString(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, QString::fromStdString(Core::Strings::t("circulation.archiveLoan")));
    EXPECT_TRUE(loanArchived(*m_db, highlighted));
    EXPECT_FALSE(loanArchived(*m_db, bystander));
}

TEST_F(test_ui_BulkPages, ATickedOpenLoanStaysWhileTheReturnedOneIsArchived)
{
    const std::int64_t returned = seedReturnedLoan(6);
    const std::int64_t member = seedMember(*m_db, uniqueMemberSeed(7));
    const std::int64_t book = seedBook(*m_db, uniqueBookSeed(7));
    const std::int64_t open =
        rawInsertLoan(*m_db, member, copyIdsOf(*m_db, book).front(), "2026-09-01", "2026-09-15");
    openAndSelect(returned);
    tickLoan(m_table, open);
    tickLoan(m_table, returned);

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { clickDelete(); }, QString::fromStdString(Core::Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_NE(outcome.text, QString::fromStdString(Core::Strings::t("circulation.archiveLoan")));
    EXPECT_FALSE(loanArchived(*m_db, open));
    EXPECT_TRUE(loanArchived(*m_db, returned));
}
