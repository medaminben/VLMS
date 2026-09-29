#include "TestDatabase.h"
#include "TestSeed.h"
#include "WorkflowTestSupport.h"

#include "ui/presentation/ArchivedLoanSpec.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/LoanTypes.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

namespace {

const PageAction* findAction(const QList<PageAction>& actions, const QString& id)
{
    for (const PageAction& action : actions) {
        if (action.id == id) {
            return &action;
        }
    }
    return nullptr;
}

}  // namespace

class ArchivedLoanSpecTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        Core::Locale::setCode("en");
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members =
            std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_catalog =
            std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_harness = std::make_unique<ArchiveWorkflowHarness>(*m_members, *m_catalog, *m_circulation);
        m_spec = std::make_unique<ArchivedLoanSpec>(*m_circulation, *m_catalog, *m_members,
                                                    *m_harness->archive);
    }

    void TearDown() override
    {
        m_spec.reset();
        m_harness.reset();
        m_circulation.reset();
        m_catalog.reset();
        m_members.reset();
        m_db.reset();
        Core::Locale::setCode("en");
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<ArchiveWorkflowHarness> m_harness;
    std::unique_ptr<ArchivedLoanSpec> m_spec;
};

TEST_F(ArchivedLoanSpecTest, ForcesArchivedScopeIgnoresLive)
{
    MemberSeed member = uniqueMemberSeed(1);
    const qint64 memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);
    BookSeed book = uniqueBookSeed(1);
    book.title = "Loan Title";
    book.initialCopyCount = 2;
    const qint64 bookId = seedBook(*m_db, book);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);

    const qint64 liveLoan =
        rawInsertLoan(*m_db, memberId, copies.at(0), "2026-09-01", "2026-09-15", "2026-09-10");
    ASSERT_GT(liveLoan, 0);
    const qint64 archivedLoan =
        rawInsertLoan(*m_db, memberId, copies.at(1), "2026-08-01", "2026-08-15", "2026-08-10");
    ASSERT_GT(archivedLoan, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(archivedLoan));

    QueryState state;
    state.scope = Repositories::ArchiveScope::Live;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->totalCount(), 1);
    EXPECT_EQ(m_spec->idAt(0), archivedLoan);
}

TEST_F(ArchivedLoanSpecTest, DisplayRowsMatchArchiveColumns)
{
    MemberSeed member = uniqueMemberSeed(1);
    const qint64 memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);
    BookSeed book = uniqueBookSeed(1);
    book.title = "Loan Title";
    book.initialCopyCount = 1;
    const qint64 bookId = seedBook(*m_db, book);
    ASSERT_GT(bookId, 0);
    const qint64 loanId =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15",
                      "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));
    const auto stored = m_circulation->getLoan(loanId);
    ASSERT_TRUE(stored);

    QueryState state;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);

    EXPECT_EQ(m_spec->idAt(0), loanId);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::UserRole).toLongLong(), loanId);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::DisplayRole).toString(),
              QString::fromStdString(stored->memberName));
    EXPECT_EQ(m_spec->cell(0, 1, Qt::DisplayRole).toString(),
              QString::fromStdString(stored->copyCode) + QStringLiteral(" / ")
                  + QString::fromStdString(stored->bookTitle));
    EXPECT_EQ(m_spec->cell(0, 2, Qt::DisplayRole).toString(),
              QString::fromStdString(stored->returnedAt));
    EXPECT_FALSE(m_spec->cell(0, 3, Qt::DisplayRole).toString().isEmpty());
}

TEST_F(ArchivedLoanSpecTest, RankOfMatchesRepository)
{
    MemberSeed member = uniqueMemberSeed(1);
    const qint64 memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);
    BookSeed book = uniqueBookSeed(1);
    book.initialCopyCount = 2;
    const qint64 bookId = seedBook(*m_db, book);
    ASSERT_GT(bookId, 0);
    const auto copies = copyIdsOf(*m_db, bookId);

    const qint64 first =
        rawInsertLoan(*m_db, memberId, copies.at(0), "2026-07-01", "2026-07-15", "2026-07-10");
    const qint64 second =
        rawInsertLoan(*m_db, memberId, copies.at(1), "2026-08-01", "2026-08-15", "2026-08-10");
    ASSERT_GT(first, 0);
    ASSERT_GT(second, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(first));
    ASSERT_TRUE(m_circulation->archiveLoan(second));

    QueryState state;
    state.sortKey = QString::fromLatin1(Repositories::LoanSort::kReturned);
    state.ascending = true;

    Repositories::LoanQuery query;
    query.archive = Repositories::ArchiveScope::Archived;
    query.sortColumn = Repositories::LoanSort::kReturned;
    query.sortAscending = true;
    const auto expected = m_circulation->rankOfLoan(second, query);
    ASSERT_TRUE(expected);

    const auto actual = m_spec->rankOf(second, state);
    ASSERT_TRUE(actual);
    EXPECT_EQ(actual.value(), expected.value());
}

TEST_F(ArchivedLoanSpecTest, PreviewMatchesArchiveDetails)
{
    MemberSeed member = uniqueMemberSeed(1);
    const qint64 memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);
    BookSeed book = uniqueBookSeed(1);
    book.title = "Loan Title";
    book.initialCopyCount = 1;
    const qint64 bookId = seedBook(*m_db, book);
    ASSERT_GT(bookId, 0);
    const qint64 loanId =
        rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(), "2026-09-01", "2026-09-15",
                      "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));
    const auto stored = m_circulation->getLoan(loanId);
    ASSERT_TRUE(stored);

    const PreviewContent preview = m_spec->preview(loanId);
    ASSERT_EQ(preview.fields.size(), 6);
    EXPECT_EQ(preview.fields.at(0).labelKey, QStringLiteral("archive.col.member"));
    EXPECT_EQ(preview.fields.at(1).labelKey, QStringLiteral("member.field.number"));
    EXPECT_EQ(preview.fields.at(2).labelKey, QStringLiteral("archive.col.copy"));
    EXPECT_EQ(preview.fields.at(3).labelKey, QStringLiteral("archive.col.returnedAt"));
    EXPECT_EQ(preview.fields.at(4).labelKey, QStringLiteral("archive.col.archivedAt"));
    EXPECT_EQ(preview.fields.at(5).labelKey, QStringLiteral("loan.field.notes"));
    EXPECT_TRUE(preview.fields.at(5).scrollable);
}

TEST_F(ArchivedLoanSpecTest, ActionsOmitLoansAndDependsOn)
{
    EXPECT_EQ(m_spec->id(), QStringLiteral("archived-loans"));
    EXPECT_EQ(m_spec->dependsOn(), Entity::Loans);

    const QList<ColumnSpec> columns = m_spec->columns();
    ASSERT_EQ(columns.size(), 4);
    EXPECT_EQ(columns.at(0).sortKey, QString::fromLatin1(Repositories::LoanSort::kMember));
    EXPECT_EQ(columns.at(1).sortKey, QString::fromLatin1(Repositories::LoanSort::kTitle));
    EXPECT_EQ(columns.at(2).sortKey, QString::fromLatin1(Repositories::LoanSort::kReturned));
    EXPECT_EQ(columns.at(3).sortKey, QString::fromLatin1(Repositories::LoanSort::kArchivedAt));

    const QList<PageAction> actions = m_spec->actions();
    QStringList actionIds;
    for (const PageAction& action : actions) {
        actionIds.append(action.id);
    }
    EXPECT_EQ(actionIds,
              (QStringList{QStringLiteral("restore"), QStringLiteral("purge"), QStringLiteral("tickAll"),
                           QStringLiteral("refresh")}));
    EXPECT_EQ(findAction(actions, QStringLiteral("restore"))->needs, PageAction::Needs::Some);
    EXPECT_EQ(findAction(actions, QStringLiteral("purge"))->needs, PageAction::Needs::Some);
    EXPECT_EQ(findAction(actions, QStringLiteral("loans")), nullptr);
}
