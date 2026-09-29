#include "ui/views/ListPage.h"
#include "ui/presentation/ListSpec.h"
#include "ui/presentation/PagedQueryModel.h"
#include "ui/views/UiHelpers.h"
#include "ui/workflows/DataEvents.h"

#include <VLMS/Core/Result.h>

#include <QApplication>
#include <QKeySequence>
#include <QLineEdit>
#include <QShortcut>
#include <QTableView>
#include <QTest>

#include <gtest/gtest.h>

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
        };
    }

    QList<FacetSpec> facets() const override { return {}; }
    QList<PageAction> actions() const override { return pageActions; }
    EntitySet dependsOn() const override { return Entity::Members; }

    Core::Status load(const QueryState& /*state*/) override
    {
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
        if (role != Qt::DisplayRole || column != 0
            || row < 0 || row >= static_cast<int>(rows.size())) {
            return {};
        }
        return rows[static_cast<size_t>(row)].name;
    }

    Core::Result<int> rankOf(qint64 /*id*/, const QueryState& /*state*/) const override
    {
        return Core::Result<int>::fail(Core::ErrorKind::NotFound, "fake.notFound");
    }

    PreviewContent preview(qint64 /*id*/) const override { return {}; }

    struct Row {
        qint64 id = 0;
        QString name;
    };

    std::vector<Row> nextRows;
    int nextTotal = 0;
    std::vector<Row> rows;
    int total = 0;
    QList<PageAction> pageActions;
};

ListPage::Config defaultConfig()
{
    ListPage::Config cfg;
    cfg.titleKey = QStringLiteral("page.members.title");
    cfg.bodyKey = QStringLiteral("page.members.body");
    cfg.searchPlaceholderKey = QStringLiteral("members.searchPlaceholder");
    cfg.imageObjectName = QStringLiteral("memberPhoto");
    cfg.imageBounds = memberPhotoPreviewBounds();
    cfg.previewPanelMinWidth = 200;
    return cfg;
}

PageAction makeAction(const QString& id, PageAction::Needs needs, int* runCount = nullptr)
{
    PageAction action;
    action.id = id;
    action.labelKey = id;
    action.needs = needs;
    action.run = [runCount](const ActionContext&) {
        if (runCount != nullptr) {
            ++(*runCount);
        }
        return Core::Status::ok();
    };
    return action;
}

bool hasShortcut(QWidget* root, const QKeySequence& sequence)
{
    const auto shortcuts = root->findChildren<QShortcut*>();
    for (const QShortcut* shortcut : shortcuts) {
        if (shortcut->key() == sequence) {
            return true;
        }
    }
    return false;
}

QShortcut* findShortcut(QWidget* root, const QKeySequence& sequence)
{
    const auto shortcuts = root->findChildren<QShortcut*>();
    for (QShortcut* shortcut : shortcuts) {
        if (shortcut->key() == sequence) {
            return shortcut;
        }
    }
    return nullptr;
}

}  // namespace

TEST(ListPageShortcuts, MembersHasCtrlNEnterCtrlLDelete)
{
    int addRuns = 0;
    int editRuns = 0;
    int loansRuns = 0;
    int archiveRuns = 0;

    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Ada")}};
    spec.nextTotal = 1;
    spec.pageActions = {
        makeAction(QStringLiteral("loans"), PageAction::Needs::One, &loansRuns),
        makeAction(QStringLiteral("add"), PageAction::Needs::Nothing, &addRuns),
        makeAction(QStringLiteral("edit"), PageAction::Needs::One, &editRuns),
        makeAction(QStringLiteral("archive"), PageAction::Needs::Some, &archiveRuns),
        makeAction(QStringLiteral("tickAll"), PageAction::Needs::Nothing),
        makeAction(QStringLiteral("refresh"), PageAction::Needs::Nothing),
    };

    DataEvents events;
    ListPage page(&spec, events, defaultConfig());
    page.rebuildChrome();
    page.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&page));

    EXPECT_TRUE(hasShortcut(&page, QKeySequence(QStringLiteral("Ctrl+N"))));
    EXPECT_TRUE(hasShortcut(&page, QKeySequence(Qt::Key_Return)));
    EXPECT_TRUE(hasShortcut(&page, QKeySequence(QStringLiteral("Ctrl+L"))));
    EXPECT_TRUE(hasShortcut(&page, QKeySequence(Qt::Key_Delete)));

    QTest::keyClick(&page, Qt::Key_N, Qt::ControlModifier);
    QTest::qWait(0);
    EXPECT_EQ(addRuns, 1);

    selectTableRow(page.table(), 0);
    page.table()->setFocus();
    QTest::keyClick(page.table(), Qt::Key_Return);
    QTest::qWait(0);
    EXPECT_EQ(editRuns, 1);

    QTest::keyClick(page.table(), Qt::Key_L, Qt::ControlModifier);
    QTest::qWait(0);
    EXPECT_EQ(loansRuns, 1);

    QTest::keyClick(page.table(), Qt::Key_Delete);
    QTest::qWait(0);
    EXPECT_EQ(archiveRuns, 1);
}

TEST(ListPageShortcuts, CirculationCheckoutIsCtrlN)
{
    int checkoutRuns = 0;

    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Loan")}};
    spec.nextTotal = 1;
    spec.pageActions = {
        makeAction(QStringLiteral("return"), PageAction::Needs::One),
        makeAction(QStringLiteral("checkout"), PageAction::Needs::Nothing, &checkoutRuns),
        makeAction(QStringLiteral("extend"), PageAction::Needs::One),
        makeAction(QStringLiteral("archive"), PageAction::Needs::Some),
        makeAction(QStringLiteral("tickAll"), PageAction::Needs::Nothing),
        makeAction(QStringLiteral("refresh"), PageAction::Needs::Nothing),
    };

    DataEvents events;
    ListPage page(&spec, events, defaultConfig());
    page.rebuildChrome();
    page.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&page));

    QShortcut* ctrlN = findShortcut(&page, QKeySequence(QStringLiteral("Ctrl+N")));
    ASSERT_NE(ctrlN, nullptr) << "checkout must map to Ctrl+N like add";
    EXPECT_EQ(ctrlN->parentWidget(), &page) << "Ctrl+N must be page-scoped";

    QTest::keyClick(&page, Qt::Key_N, Qt::ControlModifier);
    QTest::qWait(0);
    EXPECT_EQ(checkoutRuns, 1);
}

TEST(ListPageShortcuts, ArchivePurgeIsShiftDelete)
{
    int purgeRuns = 0;

    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Archived")}};
    spec.nextTotal = 1;
    spec.pageActions = {
        makeAction(QStringLiteral("loans"), PageAction::Needs::One),
        makeAction(QStringLiteral("restore"), PageAction::Needs::Some),
        makeAction(QStringLiteral("purge"), PageAction::Needs::Some, &purgeRuns),
        makeAction(QStringLiteral("tickAll"), PageAction::Needs::Nothing),
        makeAction(QStringLiteral("refresh"), PageAction::Needs::Nothing),
    };

    DataEvents events;
    ListPage page(&spec, events, defaultConfig());
    page.rebuildChrome();
    page.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&page));

    EXPECT_TRUE(hasShortcut(&page, QKeySequence(QStringLiteral("Shift+Del"))));

    selectTableRow(page.table(), 0);
    page.table()->setFocus();
    QTest::keyClick(page.table(), Qt::Key_Delete, Qt::ShiftModifier);
    QTest::qWait(0);
    EXPECT_EQ(purgeRuns, 1);
}

TEST(ListPageShortcuts, SearchLineEditKeepsCtrlAAndEnter)
{
    int tickRuns = 0;
    int editRuns = 0;

    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Ada")}, {2, QStringLiteral("Ben")}};
    spec.nextTotal = 2;
    spec.pageActions = {
        makeAction(QStringLiteral("edit"), PageAction::Needs::One, &editRuns),
        makeAction(QStringLiteral("tickAll"), PageAction::Needs::Nothing, &tickRuns),
    };

    DataEvents events;
    ListPage page(&spec, events, defaultConfig());
    page.rebuildChrome();
    page.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&page));

    auto* search = page.findChild<QLineEdit*>(QStringLiteral("listSearch"));
    ASSERT_NE(search, nullptr);

    // Enter in search searches immediately and must not trigger edit.
    search->setFocus();
    search->setText(QStringLiteral("ada"));
    QTest::keyClick(search, Qt::Key_Return);
    QTest::qWait(0);
    EXPECT_EQ(editRuns, 0);
    EXPECT_EQ(page.model()->query().search, QStringLiteral("ada"));

    // Ctrl+A in search selects search text and must not tick all rows.
    search->setFocus();
    search->setText(QStringLiteral("hello"));
    search->setCursorPosition(search->text().size());
    QTest::keyClick(search, Qt::Key_A, Qt::ControlModifier);
    QTest::qWait(0);
    EXPECT_EQ(tickRuns, 0);
    EXPECT_TRUE(page.model()->checkedIds().isEmpty());
    EXPECT_EQ(search->selectedText(), QStringLiteral("hello"));
}
