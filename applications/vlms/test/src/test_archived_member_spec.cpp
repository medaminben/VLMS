#include "TestDatabase.h"
#include "TestSeed.h"
#include "WorkflowTestSupport.h"

#include "ui/presentation/ArchivedMemberSpec.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

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

class ArchivedMemberSpecTest : public ::testing::Test {
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
        m_spec = std::make_unique<ArchivedMemberSpec>(*m_members, *m_harness->archive);
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
    std::unique_ptr<ArchivedMemberSpec> m_spec;
};

TEST_F(ArchivedMemberSpecTest, ForcesArchivedScopeIgnoresLive)
{
    MemberSeed live = uniqueMemberSeed(1);
    live.city = "Tunis";
    const qint64 liveId = seedMember(*m_db, live);
    ASSERT_GT(liveId, 0);

    MemberSeed archived = uniqueMemberSeed(2);
    archived.city = "Sfax";
    const qint64 archivedId = seedMember(*m_db, archived);
    ASSERT_GT(archivedId, 0);
    ASSERT_TRUE(m_members->archiveMember(archivedId));

    QueryState state;
    state.scope = Repositories::ArchiveScope::Live;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->totalCount(), 1);
    EXPECT_EQ(m_spec->idAt(0), archivedId);
}

TEST_F(ArchivedMemberSpecTest, DisplayRowsMatchArchiveColumns)
{
    MemberSeed seed = uniqueMemberSeed(1);
    seed.city = "Tunis";
    seed.status = Repositories::MemberStatus::kActive;
    const qint64 id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_members->archiveMember(id));
    const auto stored = m_members->getMember(id);
    ASSERT_TRUE(stored);

    QueryState state;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);

    EXPECT_EQ(m_spec->idAt(0), id);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::UserRole).toLongLong(), id);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::DisplayRole).toString(),
              QString::fromStdString(stored->membershipNumber));
    EXPECT_EQ(m_spec->cell(0, 1, Qt::DisplayRole).toString(), QStringLiteral("Member1 Test"));
    EXPECT_EQ(m_spec->cell(0, 2, Qt::DisplayRole).toString(), QStringLiteral("Tunis"));
    EXPECT_EQ(m_spec->cell(0, 3, Qt::DisplayRole).toString(),
              QString::fromStdString(Core::Strings::memberStatusLabel(Repositories::MemberStatus::kActive)));
    EXPECT_EQ(m_spec->cell(0, 4, Qt::DisplayRole).toString(), QStringLiteral("0"));
    EXPECT_FALSE(m_spec->cell(0, 5, Qt::DisplayRole).toString().isEmpty());
}

TEST_F(ArchivedMemberSpecTest, RankOfMatchesRepository)
{
    MemberSeed a = uniqueMemberSeed(1);
    a.lastName = "Aaa";
    const qint64 idA = seedMember(*m_db, a);
    ASSERT_GT(idA, 0);
    ASSERT_TRUE(m_members->archiveMember(idA));

    MemberSeed b = uniqueMemberSeed(2);
    b.lastName = "Bbb";
    const qint64 idB = seedMember(*m_db, b);
    ASSERT_GT(idB, 0);
    ASSERT_TRUE(m_members->archiveMember(idB));

    QueryState state;
    state.sortKey = QString::fromLatin1(Repositories::MemberSort::kName);
    state.ascending = true;

    Repositories::MemberQuery query;
    query.archive = Repositories::ArchiveScope::Archived;
    query.sortColumn = Repositories::MemberSort::kName;
    query.sortAscending = true;
    const auto expected = m_members->rankOfMember(idB, query);
    ASSERT_TRUE(expected);

    const auto actual = m_spec->rankOf(idB, state);
    ASSERT_TRUE(actual);
    EXPECT_EQ(actual.value(), expected.value());
}

TEST_F(ArchivedMemberSpecTest, PreviewMatchesArchiveDetails)
{
    MemberSeed seed = uniqueMemberSeed(1);
    seed.city = "Tunis";
    seed.status = Repositories::MemberStatus::kActive;
    const qint64 id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_members->archiveMember(id));
    const auto stored = m_members->getMember(id);
    ASSERT_TRUE(stored);

    const PreviewContent preview = m_spec->preview(id);
    ASSERT_EQ(preview.fields.size(), 5);
    EXPECT_EQ(preview.fields.at(0).labelKey, QStringLiteral("archive.col.number"));
    EXPECT_EQ(preview.fields.at(0).value, QString::fromStdString(stored->membershipNumber));
    EXPECT_EQ(preview.fields.at(1).labelKey, QStringLiteral("archive.col.name"));
    EXPECT_EQ(preview.fields.at(1).value, QStringLiteral("Member1 Test"));
    EXPECT_EQ(preview.fields.at(2).labelKey, QStringLiteral("archive.col.city"));
    EXPECT_EQ(preview.fields.at(3).labelKey, QStringLiteral("archive.col.status"));
    EXPECT_EQ(preview.fields.at(4).labelKey, QStringLiteral("archive.col.archivedAt"));
}

TEST_F(ArchivedMemberSpecTest, ActionsRestorePurgeLoansAndDependsOn)
{
    EXPECT_EQ(m_spec->id(), QStringLiteral("archived-members"));
    EXPECT_EQ(m_spec->dependsOn(), Entity::Members);

    const QList<ColumnSpec> columns = m_spec->columns();
    ASSERT_EQ(columns.size(), 6);
    EXPECT_EQ(columns.at(0).key, QStringLiteral("number"));
    EXPECT_EQ(columns.at(0).sortKey, QString::fromLatin1(Repositories::MemberSort::kNumber));
    EXPECT_EQ(columns.at(4).sortKey, QString::fromLatin1(Repositories::MemberSort::kAllLoans));
    EXPECT_EQ(columns.at(5).sortKey, QString::fromLatin1(Repositories::MemberSort::kArchivedAt));

    const QList<PageAction> actions = m_spec->actions();
    QStringList actionIds;
    for (const PageAction& action : actions) {
        actionIds.append(action.id);
    }
    EXPECT_EQ(actionIds,
              (QStringList{QStringLiteral("loans"), QStringLiteral("restore"), QStringLiteral("purge"),
                           QStringLiteral("tickAll"), QStringLiteral("refresh")}));

    const PageAction* loans = findAction(actions, QStringLiteral("loans"));
    ASSERT_NE(loans, nullptr);
    EXPECT_EQ(loans->needs, PageAction::Needs::One);

    const PageAction* restore = findAction(actions, QStringLiteral("restore"));
    ASSERT_NE(restore, nullptr);
    EXPECT_EQ(restore->needs, PageAction::Needs::Some);

    const PageAction* purge = findAction(actions, QStringLiteral("purge"));
    ASSERT_NE(purge, nullptr);
    EXPECT_EQ(purge->needs, PageAction::Needs::Some);
}
