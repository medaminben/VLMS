#include "ui/presentation/PagedQueryModel.h"
#include "ui/presentation/ListSpec.h"
#include "ui/workflows/DataEvents.h"

#include <VLMS/Core/Result.h>

#include <QAbstractItemModel>
#include <QSignalSpy>
#include <Qt>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace VLMS;

namespace {

class FakeListSpec final : public ListSpec {
public:
    QString id() const override { return QStringLiteral("fake"); }

    QList<ColumnSpec> columns() const override
    {
        return {
            ColumnSpec{QStringLiteral("name"), QStringLiteral("col.name"), 120,
                       QStringLiteral("name")},
            ColumnSpec{QStringLiteral("code"), QStringLiteral("col.code"), 80,
                       QStringLiteral("code")},
        };
    }

    QList<FacetSpec> facets() const override { return {}; }
    QList<PageAction> actions() const override { return {}; }
    EntitySet dependsOn() const override { return Entity::Members; }

    Core::Status load(const QueryState& state) override
    {
        ++loadCount;
        lastState = state;
        if (failNext) {
            failNext = false;
            return Core::Status::fail(Core::ErrorKind::Sql, "fake.loadFailed");
        }
        rows = nextRows;
        total = nextTotal;
        return Core::Status::ok();
    }

    void retranslate() override {}

    int rowCount() const override { return static_cast<int>(rows.size()); }
    int totalCount() const override { return total; }

    qint64 idAt(int row) const override
    {
        if (row < 0 || row >= static_cast<int>(rows.size())) {
            return 0;
        }
        return rows[static_cast<size_t>(row)].id;
    }

    QVariant cell(int row, int column, int role) const override
    {
        if (role != Qt::DisplayRole || row < 0 || row >= static_cast<int>(rows.size())) {
            return {};
        }
        const auto& r = rows[static_cast<size_t>(row)];
        if (column == 0) {
            return r.name;
        }
        if (column == 1) {
            return r.code;
        }
        return {};
    }

    Core::Result<int> rankOf(qint64 id, const QueryState& /*state*/) const override
    {
        const auto it = ranks.find(id);
        if (it == ranks.end()) {
            return Core::Result<int>::fail(Core::ErrorKind::NotFound, "fake.notFound");
        }
        return Core::Result<int>::ok(it.value());
    }

    PreviewContent preview(qint64 /*id*/) const override { return {}; }

    struct Row {
        qint64 id = 0;
        QString name;
        QString code;
    };

    std::vector<Row> nextRows;
    int nextTotal = 0;
    bool failNext = false;

    std::vector<Row> rows;
    int total = 0;
    int loadCount = 0;
    QueryState lastState;
    QHash<qint64, int> ranks;
};

QList<qint64> sorted(QList<qint64> ids)
{
    std::sort(ids.begin(), ids.end());
    return ids;
}

}  // namespace

TEST(PagedQueryModel, ReloadEmitsLoadedAndRowCount)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Ada"), QStringLiteral("A")},
                     {2, QStringLiteral("Ben"), QStringLiteral("B")}};
    spec.nextTotal = 2;

    DataEvents events;
    PagedQueryModel model(&spec, events);
    QSignalSpy loaded(&model, &PagedQueryModel::loaded);
    QSignalSpy failed(&model, &PagedQueryModel::failed);

    model.reload();

    ASSERT_EQ(loaded.count(), 1);
    EXPECT_EQ(loaded.at(0).at(0).toInt(), 2);
    EXPECT_EQ(failed.count(), 0);
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.columnCount(), 2);
    EXPECT_EQ(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("Ada"));
    EXPECT_EQ(spec.loadCount, 1);
}

TEST(PagedQueryModel, CheckStateTracksIdsAcrossReload)
{
    FakeListSpec spec;
    spec.nextRows = {{10, QStringLiteral("A"), QStringLiteral("a")},
                     {20, QStringLiteral("B"), QStringLiteral("b")}};
    spec.nextTotal = 2;

    DataEvents events;
    PagedQueryModel model(&spec, events);
    model.reload();

    ASSERT_TRUE(model.setData(model.index(1, 0), Qt::Checked, Qt::CheckStateRole));
    EXPECT_EQ(sorted(model.checkedIds()), (QList<qint64>{20}));

    // Same ids, different order — check on id 20 must survive.
    spec.nextRows = {{20, QStringLiteral("B"), QStringLiteral("b")},
                     {10, QStringLiteral("A"), QStringLiteral("a")}};
    model.reload();

    EXPECT_EQ(sorted(model.checkedIds()), (QList<qint64>{20}));
    EXPECT_EQ(model.data(model.index(0, 0), Qt::CheckStateRole).toInt(),
              static_cast<int>(Qt::Checked));
    EXPECT_EQ(model.data(model.index(1, 0), Qt::CheckStateRole).toInt(),
              static_cast<int>(Qt::Unchecked));
}

TEST(PagedQueryModel, TickAllChecksEveryListedId)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("A"), QStringLiteral("a")},
                     {2, QStringLiteral("B"), QStringLiteral("b")},
                     {3, QStringLiteral("C"), QStringLiteral("c")}};
    spec.nextTotal = 3;

    DataEvents events;
    PagedQueryModel model(&spec, events);
    model.reload();
    model.tickAll();

    EXPECT_EQ(sorted(model.checkedIds()), (QList<qint64>{1, 2, 3}));
}

TEST(PagedQueryModel, UntickIdsMissingAfterReload)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("A"), QStringLiteral("a")},
                     {2, QStringLiteral("B"), QStringLiteral("b")}};
    spec.nextTotal = 2;

    DataEvents events;
    PagedQueryModel model(&spec, events);
    model.reload();
    model.tickAll();
    ASSERT_EQ(sorted(model.checkedIds()), (QList<qint64>{1, 2}));

    spec.nextRows = {{2, QStringLiteral("B"), QStringLiteral("b")}};
    spec.nextTotal = 1;
    model.reload();

    EXPECT_EQ(sorted(model.checkedIds()), (QList<qint64>{2}));
}

TEST(PagedQueryModel, SortUpdatesQueryStateAndReloads)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("A"), QStringLiteral("a")}};
    spec.nextTotal = 1;

    DataEvents events;
    PagedQueryModel model(&spec, events);
    model.reload();
    const int loadsBefore = spec.loadCount;

    model.sort(1, Qt::DescendingOrder);

    EXPECT_EQ(spec.loadCount, loadsBefore + 1);
    EXPECT_EQ(model.query().sortKey, QStringLiteral("code"));
    EXPECT_FALSE(model.query().ascending);
    EXPECT_EQ(spec.lastState.sortKey, QStringLiteral("code"));
    EXPECT_FALSE(spec.lastState.ascending);
}

TEST(PagedQueryModel, RevealIdMovesPageUsingRankOf)
{
    FakeListSpec spec;
    spec.nextRows = {{30, QStringLiteral("C"), QStringLiteral("c")}};
    spec.nextTotal = 30;
    spec.ranks.insert(30, 14);  // 0-based rank → page 2 of size 10, row 4

    DataEvents events;
    PagedQueryModel model(&spec, events);

    QueryState state;
    state.pageSize = 10;
    state.page = 1;
    model.setQuery(state);

    QSignalSpy target(&model, &PagedQueryModel::selectionTargetChanged);
    ASSERT_TRUE(model.revealId(30));

    EXPECT_EQ(model.query().page, 2);
    EXPECT_EQ(spec.lastState.page, 2);
    ASSERT_EQ(target.count(), 1);
    EXPECT_EQ(target.at(0).at(0).toInt(), 4);
}

TEST(PagedQueryModel, DataEventsMatchingDependsOnReloads)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("A"), QStringLiteral("a")}};
    spec.nextTotal = 1;

    DataEvents events;
    PagedQueryModel model(&spec, events);
    model.reload();
    const int loadsAfterReload = spec.loadCount;

    events.emitChanged(Entity::Books, {9});
    EXPECT_EQ(spec.loadCount, loadsAfterReload);

    events.emitChanged(Entity::Members, {1});
    EXPECT_EQ(spec.loadCount, loadsAfterReload + 1);
}

TEST(PagedQueryModel, FailedLoadKeepsOldRowsAndEmitsFailed)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Ada"), QStringLiteral("A")},
                     {2, QStringLiteral("Ben"), QStringLiteral("B")}};
    spec.nextTotal = 2;

    DataEvents events;
    PagedQueryModel model(&spec, events);
    model.reload();
    ASSERT_EQ(model.rowCount(), 2);

    qRegisterMetaType<Core::Error>("VLMS::Core::Error");
    QSignalSpy failed(&model, &PagedQueryModel::failed);
    QSignalSpy loaded(&model, &PagedQueryModel::loaded);

    spec.failNext = true;
    model.reload();

    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(loaded.count(), 0);
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.data(model.index(0, 0), Qt::DisplayRole).toString(), QStringLiteral("Ada"));
}
