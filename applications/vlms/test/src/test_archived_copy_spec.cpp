#include "TestDatabase.h"
#include "TestSeed.h"
#include "WorkflowTestSupport.h"

#include "ui/presentation/ArchivedCopySpec.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
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

class ArchivedCopySpecTest : public ::testing::Test {
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
        m_spec = std::make_unique<ArchivedCopySpec>(*m_catalog, *m_harness->archive);
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
    std::unique_ptr<ArchivedCopySpec> m_spec;
};

TEST_F(ArchivedCopySpecTest, ForcesArchivedScopeIgnoresLive)
{
    BookSeed live = uniqueBookSeed(1);
    live.title = "Live Book";
    live.initialCopyCount = 1;
    ASSERT_GT(seedBook(*m_db, live), 0);

    BookSeed archived = uniqueBookSeed(2);
    archived.title = "Archived Book";
    archived.initialCopyCount = 1;
    const qint64 bookId = seedBook(*m_db, archived);
    ASSERT_GT(bookId, 0);
    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    const qint64 copyId = copyIdsOf(*m_db, bookId).front();

    QueryState state;
    state.scope = Repositories::ArchiveScope::Live;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->totalCount(), 1);
    EXPECT_EQ(m_spec->idAt(0), copyId);
}

TEST_F(ArchivedCopySpecTest, DisplayRowsMatchArchiveColumns)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Optics";
    seed.language = "ar";
    seed.initialCopyCount = 1;
    const qint64 bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    const qint64 copyId = copyIdsOf(*m_db, bookId).front();

    QueryState state;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);

    EXPECT_EQ(m_spec->idAt(0), copyId);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::UserRole).toLongLong(), copyId);
    EXPECT_FALSE(m_spec->cell(0, 0, Qt::DisplayRole).toString().isEmpty());
    EXPECT_EQ(m_spec->cell(0, 1, Qt::DisplayRole).toString(),
              QString::fromStdString(Core::Strings::t("book.copy.source.arabic")));
    EXPECT_EQ(m_spec->cell(0, 2, Qt::DisplayRole).toString(), QStringLiteral("Optics"));
    EXPECT_EQ(m_spec->cell(0, 3, Qt::DisplayRole).toString(), QStringLiteral("0"));
    EXPECT_FALSE(m_spec->cell(0, 4, Qt::DisplayRole).toString().isEmpty());
}

TEST_F(ArchivedCopySpecTest, RankOfScansOrderedIdsWithoutNewApi)
{
    BookSeed a = uniqueBookSeed(1);
    a.title = "Aaa";
    a.initialCopyCount = 1;
    const qint64 bookA = seedBook(*m_db, a);
    ASSERT_GT(bookA, 0);
    ASSERT_TRUE(m_catalog->archiveBook(bookA));

    BookSeed b = uniqueBookSeed(2);
    b.title = "Bbb";
    b.initialCopyCount = 1;
    const qint64 bookB = seedBook(*m_db, b);
    ASSERT_GT(bookB, 0);
    ASSERT_TRUE(m_catalog->archiveBook(bookB));
    const qint64 copyB = copyIdsOf(*m_db, bookB).front();

    QueryState state;
    state.sortKey = QString::fromLatin1(Repositories::CopySort::kTitle);
    state.ascending = true;

    const auto actual = m_spec->rankOf(copyB, state);
    ASSERT_TRUE(actual);
    EXPECT_EQ(actual.value(), 1);

    const auto missing = m_spec->rankOf(999999, state);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().kind, Core::ErrorKind::NotFound);
    EXPECT_EQ(missing.error().key, "error.copy.notFound");
}

TEST_F(ArchivedCopySpecTest, PreviewMatchesArchiveDetails)
{
    BookSeed seed = uniqueBookSeed(1);
    seed.title = "Optics";
    seed.language = "ar";
    seed.initialCopyCount = 1;
    const qint64 bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    const qint64 copyId = copyIdsOf(*m_db, bookId).front();

    const PreviewContent preview = m_spec->preview(copyId);
    ASSERT_EQ(preview.fields.size(), 5);
    EXPECT_EQ(preview.fields.at(0).labelKey, QStringLiteral("archive.col.localId"));
    EXPECT_EQ(preview.fields.at(1).labelKey, QStringLiteral("archive.col.source"));
    EXPECT_EQ(preview.fields.at(2).labelKey, QStringLiteral("archive.col.title"));
    EXPECT_EQ(preview.fields.at(2).value, QStringLiteral("Optics"));
    EXPECT_EQ(preview.fields.at(3).labelKey, QStringLiteral("archive.col.archivedAt"));
    EXPECT_EQ(preview.fields.at(4).labelKey, QStringLiteral("archive.col.notes"));
}

TEST_F(ArchivedCopySpecTest, ActionsIncludeReuseAndDependsOn)
{
    EXPECT_EQ(m_spec->id(), QStringLiteral("archived-copies"));
    EXPECT_EQ(m_spec->dependsOn(), Entity::Copies);

    const QList<ColumnSpec> columns = m_spec->columns();
    ASSERT_EQ(columns.size(), 5);
    EXPECT_EQ(columns.at(0).sortKey, QString::fromLatin1(Repositories::CopySort::kLocalId));
    EXPECT_EQ(columns.at(3).sortKey, QString::fromLatin1(Repositories::CopySort::kLoans));
    EXPECT_EQ(columns.at(4).sortKey, QString::fromLatin1(Repositories::CopySort::kArchivedAt));

    const QList<PageAction> actions = m_spec->actions();
    QStringList actionIds;
    for (const PageAction& action : actions) {
        actionIds.append(action.id);
    }
    EXPECT_EQ(actionIds,
              (QStringList{QStringLiteral("loans"), QStringLiteral("restore"), QStringLiteral("reuse"),
                           QStringLiteral("purge"), QStringLiteral("tickAll"),
                           QStringLiteral("refresh")}));

    const PageAction* reuse = findAction(actions, QStringLiteral("reuse"));
    ASSERT_NE(reuse, nullptr);
    EXPECT_EQ(reuse->needs, PageAction::Needs::One);

    BookSeed seed = uniqueBookSeed(3);
    seed.initialCopyCount = 2;
    const qint64 bookId = seedBook(*m_db, seed);
    ASSERT_GT(bookId, 0);
    ASSERT_TRUE(m_catalog->archiveBook(bookId));
    const auto copies = copyIdsOf(*m_db, bookId);
    ASSERT_EQ(copies.size(), 2u);
    const qint64 numbered = copies.at(0);
    const qint64 numberless = copies.at(1);
    ASSERT_TRUE(m_db->exec("UPDATE book_copies SET local_id = NULL, global_copy_id = NULL "
                           "WHERE id = " + std::to_string(numberless)));

    ActionContext ctx;
    ctx.ids = {numbered};
    EXPECT_TRUE(reuse->enabled(ctx));
    ctx.ids = {numberless};
    EXPECT_FALSE(reuse->enabled(ctx));
}
