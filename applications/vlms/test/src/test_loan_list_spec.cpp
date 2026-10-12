#include "TestDatabase.h"
#include "TestSeed.h"
#include "WorkflowTestSupport.h"

#include "ui/presentation/LoanListSpec.h"

#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/LoanTypes.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>


using LoanListSpecUnderTest = VLMS::LoanListSpec;
namespace Core = VLMS::Core;
namespace Repositories = VLMS::Repositories;
using VLMS::ActionContext;
using VLMS::ColumnSpec;
using VLMS::Entity;
using VLMS::FacetSpec;
using VLMS::PageAction;
using VLMS::PreviewContent;
using VLMS::PreviewField;
using VLMS::QueryState;

using namespace Test;

namespace {

QString writePng(const QTemporaryDir& dir, const QString& name)
{
    const QString path = dir.filePath(name);
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    EXPECT_TRUE(image.save(path, "PNG"));
    return path;
}

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

class LoanListSpec : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        Core::Locale::setCode("en");
        // Open vs overdue depends on the clock; pin the day for the whole test.
        m_clock = std::make_unique<Core::ScopedClock>(Core::Date(2026, 9, 19));
        ASSERT_TRUE(m_dir.isValid());
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(),
                                                                      m_db->resourcesDirectory());
        m_members =
            std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_harness = std::make_unique<LoanWorkflowHarness>(*m_circulation);
        m_spec = std::make_unique<LoanListSpecUnderTest>(*m_circulation, *m_catalog, *m_members,
                                                *m_harness->loans);
    }

    void TearDown() override
    {
        m_spec.reset();
        m_harness.reset();
        m_circulation.reset();
        m_members.reset();
        m_catalog.reset();
        m_db.reset();
        m_clock.reset();
        Core::Locale::setCode("en");
    }

    void seedOpenOverdueAndReturned()
    {
        MemberSeed member = uniqueMemberSeed(1);
        member.status = Repositories::MemberStatus::kActive;
        m_memberId = seedMember(*m_db, member);
        ASSERT_GT(m_memberId, 0);

        BookSeed book = uniqueBookSeed(1);
        book.title = "Circulation Title";
        book.initialCopyCount = 3;
        m_bookId = seedBook(*m_db, book);
        ASSERT_GT(m_bookId, 0);
        const auto copies = copyIdsOf(*m_db, m_bookId);
        ASSERT_EQ(copies.size(), 3u);
        m_copyIds = copies;

        const Core::Date today(2026, 9, 19);
        m_openId = rawInsertLoan(*m_db, m_memberId, copies.at(0), today.addDays(-2).toIso(),
                                 today.addDays(12).toIso());
        m_overdueId = rawInsertLoan(*m_db, m_memberId, copies.at(1), today.addDays(-40).toIso(),
                                    today.addDays(-26).toIso());
        m_returnedId = rawInsertLoan(*m_db, m_memberId, copies.at(2), today.addDays(-60).toIso(),
                                     today.addDays(-46).toIso(), today.addDays(-50).toIso());
        ASSERT_GT(m_openId, 0);
        ASSERT_GT(m_overdueId, 0);
        ASSERT_GT(m_returnedId, 0);
    }

    QTemporaryDir m_dir;
    std::unique_ptr<Core::ScopedClock> m_clock;
    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<LoanWorkflowHarness> m_harness;
    std::unique_ptr<LoanListSpecUnderTest> m_spec;
    std::int64_t m_memberId = 0;
    std::int64_t m_bookId = 0;
    std::vector<std::int64_t> m_copyIds;
    std::int64_t m_openId = 0;
    std::int64_t m_overdueId = 0;
    std::int64_t m_returnedId = 0;
};

TEST_F(LoanListSpec, FilterOpenShowsOnlyOpenLoans)
{
    seedOpenOverdueAndReturned();

    QueryState state;
    state.facets.insert(QStringLiteral("loanFilter"),
                        {QString::fromLatin1(Repositories::LoanFilter::kOpen)});
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->totalCount(), 1);
    EXPECT_EQ(m_spec->idAt(0), m_openId);
}

TEST_F(LoanListSpec, FilterOverdueShowsOnlyOverdueLoans)
{
    seedOpenOverdueAndReturned();

    QueryState state;
    state.facets.insert(QStringLiteral("loanFilter"),
                        {QString::fromLatin1(Repositories::LoanFilter::kOverdue)});
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->idAt(0), m_overdueId);
}

TEST_F(LoanListSpec, FilterReturnedShowsOnlyReturnedLoans)
{
    seedOpenOverdueAndReturned();

    QueryState state;
    state.facets.insert(QStringLiteral("loanFilter"),
                        {QString::fromLatin1(Repositories::LoanFilter::kReturned)});
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->idAt(0), m_returnedId);
}

TEST_F(LoanListSpec, FilterAllShowsEveryLoan)
{
    seedOpenOverdueAndReturned();

    QueryState state;
    state.facets.insert(QStringLiteral("loanFilter"),
                        {QString::fromLatin1(Repositories::LoanFilter::kAll)});
    ASSERT_TRUE(m_spec->load(state));
    EXPECT_EQ(m_spec->rowCount(), 3);
    EXPECT_EQ(m_spec->totalCount(), 3);

    QList<qint64> ids;
    for (int row = 0; row < m_spec->rowCount(); ++row) {
        ids.append(m_spec->idAt(row));
    }
    EXPECT_TRUE(ids.contains(m_openId));
    EXPECT_TRUE(ids.contains(m_overdueId));
    EXPECT_TRUE(ids.contains(m_returnedId));
}

TEST_F(LoanListSpec, PinnedMemberIdRestrictsToThatBorrower)
{
    seedOpenOverdueAndReturned();

    MemberSeed other = uniqueMemberSeed(2);
    const qint64 otherMember = seedMember(*m_db, other);
    ASSERT_GT(otherMember, 0);
    BookSeed otherBook = uniqueBookSeed(2);
    const qint64 otherBookId = seedBook(*m_db, otherBook);
    ASSERT_GT(otherBookId, 0);
    const qint64 otherLoan =
        rawInsertLoan(*m_db, otherMember, copyIdsOf(*m_db, otherBookId).front(), "2026-09-01",
                      "2026-09-15");
    ASSERT_GT(otherLoan, 0);

    QueryState state;
    state.pinned = VLMS::LoanScope{.memberId = m_memberId};
    ASSERT_TRUE(m_spec->load(state));
    EXPECT_EQ(m_spec->totalCount(), 3);
    for (int row = 0; row < m_spec->rowCount(); ++row) {
        EXPECT_NE(m_spec->idAt(row), otherLoan);
    }

    state.pinned = VLMS::LoanScope{.memberId = otherMember};
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->idAt(0), otherLoan);
}

TEST_F(LoanListSpec, PinnedBookIdAndCopyIdRestrictLoans)
{
    seedOpenOverdueAndReturned();

    QueryState byBook;
    byBook.pinned = VLMS::LoanScope{.bookId = m_bookId};
    ASSERT_TRUE(m_spec->load(byBook));
    EXPECT_EQ(m_spec->totalCount(), 3);

    QueryState byCopy;
    byCopy.pinned = VLMS::LoanScope{.copyId = m_copyIds.at(0)};
    ASSERT_TRUE(m_spec->load(byCopy));
    ASSERT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->idAt(0), m_openId);
}

TEST_F(LoanListSpec, DisplayRowsUsePrebuiltStatusLabels)
{
    seedOpenOverdueAndReturned();

    QueryState state;
    state.facets.insert(QStringLiteral("loanFilter"),
                        {QString::fromLatin1(Repositories::LoanFilter::kOpen)});
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);

    EXPECT_EQ(m_spec->idAt(0), m_openId);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::UserRole).toLongLong(), m_openId);
    EXPECT_EQ(m_spec->cell(0, 2, Qt::DisplayRole).toString(), QStringLiteral("Circulation Title"));
    EXPECT_EQ(m_spec->cell(0, 5, Qt::DisplayRole).toString(),
              QString::fromStdString(Core::Strings::t("circulation.status.open")));

    state.facets.insert(QStringLiteral("loanFilter"),
                        {QString::fromLatin1(Repositories::LoanFilter::kOverdue)});
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->cell(0, 5, Qt::DisplayRole).toString(),
              QString::fromStdString(Core::Strings::t("circulation.status.overdue")));

    state.facets.insert(QStringLiteral("loanFilter"),
                        {QString::fromLatin1(Repositories::LoanFilter::kReturned)});
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->cell(0, 5, Qt::DisplayRole).toString(),
              QString::fromStdString(Core::Strings::t("circulation.status.returned")));
}

TEST_F(LoanListSpec, RetranslateDoesNotQuery)
{
    seedOpenOverdueAndReturned();

    QueryState state;
    state.facets.insert(QStringLiteral("loanFilter"),
                        {QString::fromLatin1(Repositories::LoanFilter::kOpen)});
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    const QString english =
        QString::fromStdString(Core::Strings::t("circulation.status.open"));
    EXPECT_EQ(m_spec->cell(0, 5, Qt::DisplayRole).toString(), english);

    Core::Locale::setCode("fr");
    const QString french = QString::fromStdString(Core::Strings::t("circulation.status.open"));
    ASSERT_NE(english, french);

    const int rowsBefore = m_spec->rowCount();
    const int totalBefore = m_spec->totalCount();
    m_spec->retranslate();

    EXPECT_EQ(m_spec->rowCount(), rowsBefore);
    EXPECT_EQ(m_spec->totalCount(), totalBefore);
    EXPECT_EQ(m_spec->cell(0, 5, Qt::DisplayRole).toString(), french);
}

TEST_F(LoanListSpec, RankOfMatchesRepository)
{
    seedOpenOverdueAndReturned();

    QueryState state;
    state.sortKey = QString::fromLatin1(Repositories::LoanSort::kBorrowed);
    state.ascending = true;

    Repositories::LoanQuery query;
    query.sortColumn = Repositories::LoanSort::kBorrowed;
    query.sortAscending = true;
    const auto expected = m_circulation->rankOfLoan(m_openId, query);
    ASSERT_TRUE(expected);

    const auto actual = m_spec->rankOf(m_openId, state);
    ASSERT_TRUE(actual);
    EXPECT_EQ(actual.value(), expected.value());
}

TEST_F(LoanListSpec, PreviewLoadsCoverPhotoAndFields)
{
    seedOpenOverdueAndReturned();

    const QString coverPath = writePng(m_dir, QStringLiteral("cover.png"));
    ASSERT_TRUE(m_catalog->setCoverImage(m_bookId, coverPath.toStdString()));
    const QString photoPath = writePng(m_dir, QStringLiteral("photo.png"));
    ASSERT_TRUE(m_members->setPhotoImage(m_memberId, photoPath.toStdString()));

    const PreviewContent preview = m_spec->preview(m_openId);
    EXPECT_FALSE(preview.imagePath.isEmpty());
    EXPECT_TRUE(QFile::exists(preview.imagePath));
    EXPECT_TRUE(preview.imagePath.contains(QStringLiteral("cover")));
    EXPECT_FALSE(preview.secondImagePath.isEmpty());
    EXPECT_TRUE(QFile::exists(preview.secondImagePath));
    EXPECT_TRUE(preview.secondImagePath.contains(QStringLiteral("photo")));

    ASSERT_FALSE(preview.fields.isEmpty());
    EXPECT_EQ(preview.fields.first().labelKey, QStringLiteral("loan.field.notes"));
    EXPECT_TRUE(preview.fields.first().scrollable);

    bool sawTitle = false;
    bool sawStatus = false;
    for (const PreviewField& field : preview.fields) {
        if (field.labelKey == QStringLiteral("book.field.title")) {
            sawTitle = true;
            EXPECT_EQ(field.value, QStringLiteral("Circulation Title"));
        }
        if (field.labelKey == QStringLiteral("circulation.col.status")) {
            sawStatus = true;
            EXPECT_EQ(field.value,
                      QString::fromStdString(Core::Strings::t("circulation.status.open")));
        }
    }
    EXPECT_TRUE(sawTitle);
    EXPECT_TRUE(sawStatus);
}

TEST_F(LoanListSpec, ActionsEnableReturnExtendForOpenAndArchiveForReturned)
{
    seedOpenOverdueAndReturned();

    const QList<PageAction> actions = m_spec->actions();
    const PageAction* ret = findAction(actions, QStringLiteral("return"));
    const PageAction* extend = findAction(actions, QStringLiteral("extend"));
    const PageAction* archive = findAction(actions, QStringLiteral("archive"));
    const PageAction* checkout = findAction(actions, QStringLiteral("checkout"));
    ASSERT_NE(ret, nullptr);
    ASSERT_NE(extend, nullptr);
    ASSERT_NE(archive, nullptr);
    ASSERT_NE(checkout, nullptr);
    EXPECT_EQ(ret->needs, PageAction::Needs::One);
    EXPECT_EQ(extend->needs, PageAction::Needs::One);
    EXPECT_EQ(archive->needs, PageAction::Needs::Some);
    EXPECT_EQ(checkout->needs, PageAction::Needs::Nothing);
    ASSERT_TRUE(ret->enabled);
    ASSERT_TRUE(extend->enabled);
    ASSERT_TRUE(archive->enabled);

    ActionContext openCtx;
    openCtx.ids = {m_openId};
    EXPECT_TRUE(ret->enabled(openCtx));
    EXPECT_TRUE(extend->enabled(openCtx));
    EXPECT_FALSE(archive->enabled(openCtx));

    ActionContext returnedCtx;
    returnedCtx.ids = {m_returnedId};
    EXPECT_FALSE(ret->enabled(returnedCtx));
    EXPECT_FALSE(extend->enabled(returnedCtx));
    EXPECT_TRUE(archive->enabled(returnedCtx));

    ActionContext tickedCtx;
    tickedCtx.ids = {m_openId, m_returnedId};
    EXPECT_TRUE(archive->enabled(tickedCtx));
}

TEST_F(LoanListSpec, DependsOnLoansMembersCopiesBooks)
{
    EXPECT_EQ(m_spec->id(), QStringLiteral("loans"));
    EXPECT_EQ(m_spec->dependsOn(),
              Entity::Loans | Entity::Members | Entity::Copies | Entity::Books);

    const QList<ColumnSpec> columns = m_spec->columns();
    ASSERT_EQ(columns.size(), 6);
    EXPECT_EQ(columns.at(0).key, QStringLiteral("member"));
    EXPECT_EQ(columns.at(0).titleKey, QStringLiteral("circulation.col.member"));
    EXPECT_EQ(columns.at(0).width, 200);
    EXPECT_EQ(columns.at(0).sortKey, QString::fromLatin1(Repositories::LoanSort::kMember));
    EXPECT_EQ(columns.at(1).key, QStringLiteral("number"));
    EXPECT_EQ(columns.at(1).width, 130);
    EXPECT_EQ(columns.at(1).sortKey, QString::fromLatin1(Repositories::LoanSort::kNumber));
    EXPECT_EQ(columns.at(2).key, QStringLiteral("title"));
    EXPECT_EQ(columns.at(2).width, 300);
    EXPECT_EQ(columns.at(2).sortKey, QString::fromLatin1(Repositories::LoanSort::kTitle));
    EXPECT_EQ(columns.at(3).key, QStringLiteral("borrowed"));
    EXPECT_EQ(columns.at(3).width, 110);
    EXPECT_EQ(columns.at(3).sortKey, QString::fromLatin1(Repositories::LoanSort::kBorrowed));
    EXPECT_EQ(columns.at(4).key, QStringLiteral("due"));
    EXPECT_EQ(columns.at(4).width, 110);
    EXPECT_EQ(columns.at(4).sortKey, QString::fromLatin1(Repositories::LoanSort::kDue));
    EXPECT_EQ(columns.at(5).key, QStringLiteral("status"));
    EXPECT_EQ(columns.at(5).width, 110);
    EXPECT_EQ(columns.at(5).sortKey, QString::fromLatin1(Repositories::LoanSort::kStatus));

    const QList<FacetSpec> facets = m_spec->facets();
    QStringList keys;
    for (const FacetSpec& facet : facets) {
        keys.append(facet.key);
    }
    EXPECT_TRUE(keys.contains(QStringLiteral("loanFilter")));
    EXPECT_TRUE(keys.contains(QStringLiteral("status")));
    EXPECT_TRUE(keys.contains(QStringLiteral("sex")));
    EXPECT_TRUE(keys.contains(QStringLiteral("year")));
    EXPECT_TRUE(keys.contains(QStringLiteral("ageGroup")));
    EXPECT_TRUE(keys.contains(QStringLiteral("city")));

    const QList<PageAction> actions = m_spec->actions();
    QStringList actionIds;
    for (const PageAction& action : actions) {
        actionIds.append(action.id);
    }
    EXPECT_TRUE(actionIds.contains(QStringLiteral("return")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("checkout")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("extend")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("archive")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("tickAll")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("refresh")));
}
