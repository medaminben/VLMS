#include "TestDatabase.h"
#include "TestSeed.h"
#include "WorkflowTestSupport.h"

#include "ui/presentation/MemberListSpec.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <memory>


using MemberListSpecUnderTest = VLMS::MemberListSpec;
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

}  // namespace

class MemberListSpec : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }
    static void TearDownTestSuite() { Core::Locale::setCode(Core::Locale::kDefaultCode); }

    void SetUp() override
    {
        Core::Locale::setCode("en");
        ASSERT_TRUE(m_dir.isValid());
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members =
            std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_harness = std::make_unique<MemberWorkflowHarness>(*m_members, *m_circulation, *m_catalog);
        m_spec = std::make_unique<MemberListSpecUnderTest>(*m_members, *m_harness->members);
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

    QTemporaryDir m_dir;
    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<MemberWorkflowHarness> m_harness;
    std::unique_ptr<MemberListSpecUnderTest> m_spec;
};

TEST_F(MemberListSpec, MapsFacetsSortAndPagingIntoMemberQuery)
{
    MemberSeed tunis = uniqueMemberSeed(1);
    tunis.city = "Tunis";
    tunis.sex = Repositories::MemberSex::kFemale;
    tunis.lastName = "Zzz";
    tunis.phone = "111";
    ASSERT_GT(seedMember(*m_db, tunis), 0);

    MemberSeed sfax = uniqueMemberSeed(2);
    sfax.city = "Sfax";
    sfax.sex = Repositories::MemberSex::kMale;
    sfax.lastName = "Aaa";
    sfax.phone = "222";
    ASSERT_GT(seedMember(*m_db, sfax), 0);

    MemberSeed sousse = uniqueMemberSeed(3);
    sousse.city = "Sousse";
    sousse.sex = Repositories::MemberSex::kMale;
    sousse.lastName = "Mmm";
    sousse.phone = "333";
    ASSERT_GT(seedMember(*m_db, sousse), 0);

    QueryState state;
    state.facets.insert(QStringLiteral("city"), {QStringLiteral("Sfax")});
    state.facets.insert(QStringLiteral("sex"),
                        {QString::fromLatin1(Repositories::MemberSex::kMale)});
    state.sortKey = QString::fromLatin1(Repositories::MemberSort::kName);
    state.ascending = true;
    state.pageSize = 10;
    state.page = 1;

    ASSERT_TRUE(m_spec->load(state));
    EXPECT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->totalCount(), 1);
    EXPECT_EQ(m_spec->cell(0, 1, Qt::DisplayRole).toString(), QStringLiteral("Member2 Aaa"));
    EXPECT_EQ(m_spec->cell(0, 3, Qt::DisplayRole).toString(), QStringLiteral("Sfax"));

    state = QueryState{};
    state.sortKey = QString::fromLatin1(Repositories::MemberSort::kName);
    state.ascending = true;
    state.pageSize = 1;
    state.page = 2;
    ASSERT_TRUE(m_spec->load(state));
    EXPECT_EQ(m_spec->rowCount(), 1);
    EXPECT_EQ(m_spec->totalCount(), 3);
    EXPECT_EQ(m_spec->cell(0, 1, Qt::DisplayRole).toString(), QStringLiteral("Member3 Mmm"));
}

TEST_F(MemberListSpec, DisplayRowsUsePrebuiltStrings)
{
    MemberSeed seed = uniqueMemberSeed(1);
    seed.city = "Tunis";
    seed.phone = "555-0100";
    seed.status = Repositories::MemberStatus::kActive;
    const qint64 id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    const auto stored = m_members->getMember(id);
    ASSERT_TRUE(stored);

    QueryState state;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);

    EXPECT_EQ(m_spec->cell(0, 0, Qt::DisplayRole).toString(),
              QString::fromStdString(stored->membershipNumber));
    EXPECT_EQ(m_spec->idAt(0), id);
    EXPECT_EQ(m_spec->cell(0, 0, Qt::UserRole).toLongLong(), id);
    EXPECT_EQ(m_spec->cell(0, 1, Qt::DisplayRole).toString(), QStringLiteral("Member1 Test"));
    EXPECT_EQ(m_spec->cell(0, 2, Qt::DisplayRole).toString(), QStringLiteral("555-0100"));
    EXPECT_EQ(m_spec->cell(0, 3, Qt::DisplayRole).toString(), QStringLiteral("Tunis"));
    EXPECT_EQ(m_spec->cell(0, 4, Qt::DisplayRole).toString(),
              QString::fromStdString(Core::Strings::memberStatusLabel(Repositories::MemberStatus::kActive)));
    EXPECT_EQ(m_spec->cell(0, 5, Qt::DisplayRole).toString(), QStringLiteral("0"));
}

TEST_F(MemberListSpec, RetranslateDoesNotQuery)
{
    MemberSeed seed = uniqueMemberSeed(1);
    seed.status = Repositories::MemberStatus::kActive;
    ASSERT_GT(seedMember(*m_db, seed), 0);

    QueryState state;
    ASSERT_TRUE(m_spec->load(state));
    ASSERT_EQ(m_spec->rowCount(), 1);
    const QString english =
        QString::fromStdString(Core::Strings::memberStatusLabel(Repositories::MemberStatus::kActive));
    EXPECT_EQ(m_spec->cell(0, 4, Qt::DisplayRole).toString(), english);

    Core::Locale::setCode("fr");
    const QString french =
        QString::fromStdString(Core::Strings::memberStatusLabel(Repositories::MemberStatus::kActive));
    ASSERT_NE(english, french);

    const int rowsBefore = m_spec->rowCount();
    const int totalBefore = m_spec->totalCount();
    m_spec->retranslate();

    EXPECT_EQ(m_spec->rowCount(), rowsBefore);
    EXPECT_EQ(m_spec->totalCount(), totalBefore);
    EXPECT_EQ(m_spec->cell(0, 4, Qt::DisplayRole).toString(), french);
}

TEST_F(MemberListSpec, RankOfMatchesRepository)
{
    MemberSeed a = uniqueMemberSeed(1);
    a.lastName = "Aaa";
    ASSERT_GT(seedMember(*m_db, a), 0);
    MemberSeed b = uniqueMemberSeed(2);
    b.lastName = "Bbb";
    const qint64 idB = seedMember(*m_db, b);
    ASSERT_GT(idB, 0);
    MemberSeed c = uniqueMemberSeed(3);
    c.lastName = "Ccc";
    ASSERT_GT(seedMember(*m_db, c), 0);

    QueryState state;
    state.sortKey = QString::fromLatin1(Repositories::MemberSort::kName);
    state.ascending = true;

    Repositories::MemberQuery query;
    query.sortColumn = Repositories::MemberSort::kName;
    query.sortAscending = true;
    const auto expected = m_members->rankOfMember(idB, query);
    ASSERT_TRUE(expected);

    const auto actual = m_spec->rankOf(idB, state);
    ASSERT_TRUE(actual);
    EXPECT_EQ(actual.value(), expected.value());
}

TEST_F(MemberListSpec, PreviewLoadsPhotoPathAndFields)
{
    MemberSeed seed = uniqueMemberSeed(1);
    seed.notes = "Keep me scrollable";
    seed.city = "Tunis";
    seed.phone = "555";
    seed.status = Repositories::MemberStatus::kActive;
    const qint64 id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);

    const QString photoPath = writePng(m_dir, QStringLiteral("photo.png"));
    ASSERT_TRUE(m_members->setPhotoImage(id, photoPath.toStdString()));

    const PreviewContent preview = m_spec->preview(id);
    EXPECT_FALSE(preview.imagePath.isEmpty());
    EXPECT_TRUE(QFile::exists(preview.imagePath));
    EXPECT_TRUE(preview.imagePath.contains(QStringLiteral("photo")));

    ASSERT_FALSE(preview.fields.isEmpty());
    EXPECT_EQ(preview.fields.first().labelKey, QStringLiteral("member.field.notes"));
    EXPECT_TRUE(preview.fields.first().scrollable);
    EXPECT_EQ(preview.fields.first().value, QStringLiteral("Keep me scrollable"));

    bool sawName = false;
    bool sawStatus = false;
    for (const PreviewField& field : preview.fields) {
        if (field.labelKey == QStringLiteral("members.col.name")) {
            sawName = true;
            EXPECT_EQ(field.value, QStringLiteral("Member1 Test"));
        }
        if (field.labelKey == QStringLiteral("member.field.status")) {
            sawStatus = true;
            EXPECT_EQ(field.value,
                      QString::fromStdString(
                          Core::Strings::memberStatusLabel(Repositories::MemberStatus::kActive)));
        }
    }
    EXPECT_TRUE(sawName);
    EXPECT_TRUE(sawStatus);
}

TEST_F(MemberListSpec, DependsOnMembers)
{
    EXPECT_EQ(m_spec->id(), QStringLiteral("members"));
    EXPECT_EQ(m_spec->dependsOn(), Entity::Members | Entity::Loans);

    const QList<ColumnSpec> columns = m_spec->columns();
    ASSERT_EQ(columns.size(), 6);
    EXPECT_EQ(columns.at(0).key, QStringLiteral("number"));
    EXPECT_EQ(columns.at(0).titleKey, QStringLiteral("members.col.number"));
    EXPECT_EQ(columns.at(0).width, 140);
    EXPECT_EQ(columns.at(0).sortKey, QString::fromLatin1(Repositories::MemberSort::kNumber));
    EXPECT_EQ(columns.at(1).width, 260);
    EXPECT_EQ(columns.at(2).width, 160);

    const QList<FacetSpec> facets = m_spec->facets();
    QStringList keys;
    for (const FacetSpec& facet : facets) {
        keys.append(facet.key);
    }
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
    EXPECT_TRUE(actionIds.contains(QStringLiteral("loans")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("add")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("edit")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("archive")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("tickAll")));
    EXPECT_TRUE(actionIds.contains(QStringLiteral("refresh")));
}
