#include "TestDatabase.h"
#include "TestSeed.h"
#include "WorkflowTestSupport.h"

#include "ui/presentation/ArchivedBookSpec.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Repositories/CirculationRepository.h>
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

class ArchivedBookSpecTest : public ::testing::Test {
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
        m_spec = std::make_unique<ArchivedBookSpec>(*m_catalog, *m_circulation, *m_harness->archive);
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
    std::unique_ptr<ArchivedBookSpec> m_spec;
};

TEST_F(ArchivedBookSpecTest, ForcesArchivedScopeIgnoresLive)
{
    BookSeed live = uniqueBookSeed(1);
    live.title = "Live Title";
    ASSERT_GT(seedBook(*m_db, live), 0);

    BookSeed archived = uniqueBookSeed(2);
    archived.title = "Archived Title";
    const qint64 archivedId = seedBook(*m_db, archived);
    ASSERT_GT(archivedId, 0);
    ASSERT_TRUE(m_catalog->archiveBook(archivedId));

    QueryState state;
    state.scope = Repositories::ArchiveScope::Live;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->totalCount(), 1);
    EXPECT_EQ(m_spec->idAt(0), archivedId);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::DisplayRole).toString(), QStringLiteral("Archived Title"));
}

TEST_F(ArchivedBookSpecTest, DisplayRowsMatchArchiveColumns)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Optics";
    seed.authorName = "Ibn al-Haytham";
    seed.initialCopyCount = 2;
    const qint64 id = seedBook(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_catalog->archiveBook(id));

    QueryState state;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);

    EXPECT_EQ(m_spec->idAt(0), id);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::UserRole).toLongLong(), id);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::DisplayRole).toString(), QStringLiteral("Optics"));
    EXPECT_EQ(m_spec->cell(0, 1, Qt::DisplayRole).toString(), QStringLiteral("Ibn al-Haytham"));
    EXPECT_EQ(m_spec->cell(0, 2, Qt::DisplayRole).toString(), QStringLiteral("2"));
    EXPECT_FALSE(m_spec->cell(0, 3, Qt::DisplayRole).toString().isEmpty());
}

TEST_F(ArchivedBookSpecTest, RankOfMatchesRepository)
{
    BookSeed a = uniqueBookSeed(1);
    a.title = "Aaa";
    const qint64 idA = seedBook(*m_db, a);
    ASSERT_GT(idA, 0);
    ASSERT_TRUE(m_catalog->archiveBook(idA));

    BookSeed b = uniqueBookSeed(2);
    b.title = "Bbb";
    const qint64 idB = seedBook(*m_db, b);
    ASSERT_GT(idB, 0);
    ASSERT_TRUE(m_catalog->archiveBook(idB));

    QueryState state;
    state.sortKey = QString::fromLatin1(Repositories::BookSort::kTitle);
    state.ascending = true;

    Repositories::BookQuery query;
    query.archive = Repositories::ArchiveScope::Archived;
    query.sortColumn = Repositories::BookSort::kTitle;
    query.sortAscending = true;
    const auto expected = m_catalog->rankOfBook(idB, query);
    ASSERT_TRUE(expected);

    const auto actual = m_spec->rankOf(idB, state);
    ASSERT_TRUE(actual);
    EXPECT_EQ(actual.value(), expected.value());
}

TEST_F(ArchivedBookSpecTest, PreviewMatchesArchiveDetails)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Optics";
    seed.authorName = "Ibn al-Haytham";
    const qint64 id = seedBook(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_catalog->archiveBook(id));
    const auto stored = m_catalog->getBook(id);
    ASSERT_TRUE(stored);

    const PreviewContent preview = m_spec->preview(id);
    ASSERT_EQ(preview.fields.size(), 4);
    EXPECT_EQ(preview.fields.at(0).labelKey, QStringLiteral("archive.col.title"));
    EXPECT_EQ(preview.fields.at(0).value, QStringLiteral("Optics"));
    EXPECT_EQ(preview.fields.at(1).labelKey, QStringLiteral("archive.col.author"));
    EXPECT_EQ(preview.fields.at(2).labelKey, QStringLiteral("archive.col.copies"));
    EXPECT_EQ(preview.fields.at(2).value, QString::number(stored->totalCopies));
    EXPECT_EQ(preview.fields.at(3).labelKey, QStringLiteral("archive.col.archivedAt"));
}

TEST_F(ArchivedBookSpecTest, ActionsAndDependsOn)
{
    EXPECT_EQ(m_spec->id(), QStringLiteral("archived-books"));
    EXPECT_EQ(m_spec->dependsOn(), Entity::Books);

    const QList<ColumnSpec> columns = m_spec->columns();
    ASSERT_EQ(columns.size(), 4);
    EXPECT_EQ(columns.at(0).sortKey, QString::fromLatin1(Repositories::BookSort::kTitle));
    EXPECT_EQ(columns.at(3).sortKey, QString::fromLatin1(Repositories::BookSort::kArchivedAt));

    const QList<PageAction> actions = m_spec->actions();
    QStringList actionIds;
    for (const PageAction& action : actions) {
        actionIds.append(action.id);
    }
    EXPECT_EQ(actionIds,
              (QStringList{QStringLiteral("loans"), QStringLiteral("restore"), QStringLiteral("purge"),
                           QStringLiteral("tickAll"), QStringLiteral("refresh")}));
    EXPECT_EQ(findAction(actions, QStringLiteral("loans"))->needs, PageAction::Needs::One);
    EXPECT_EQ(findAction(actions, QStringLiteral("restore"))->needs, PageAction::Needs::Some);
    EXPECT_EQ(findAction(actions, QStringLiteral("purge"))->needs, PageAction::Needs::Some);
}
