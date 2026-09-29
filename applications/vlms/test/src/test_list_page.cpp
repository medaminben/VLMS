#include "ui/views/ListPage.h"
#include "ui/presentation/ListSpec.h"
#include "ui/presentation/PagedQueryModel.h"
#include "ui/views/UiHelpers.h"
#include "ui/workflows/DataEvents.h"

#include <VLMS/Core/Result.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QContextMenuEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
#include <QTimer>

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

    Core::Status load(const QueryState& state) override
    {
        ++loadCount;
        lastState = state;
        if (failNext) {
            failNext = false;
            return Core::Status::fail(Core::ErrorKind::Sql, "error.sql");
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

    PreviewContent preview(qint64 id) const override
    {
        ++previewCount;
        lastPreviewId = id;
        PreviewContent content;
        content.fields = {
            PreviewField{QStringLiteral("col.name"), QStringLiteral("preview-%1").arg(id), false},
        };
        return content;
    }

    struct Row {
        qint64 id = 0;
        QString name;
    };

    std::vector<Row> nextRows;
    int nextTotal = 0;
    bool failNext = false;

    std::vector<Row> rows;
    int total = 0;
    int loadCount = 0;
    mutable int previewCount = 0;
    mutable qint64 lastPreviewId = 0;
    QueryState lastState;
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

PageAction makeAction(const QString& id,
                      PageAction::Needs needs,
                      const QKeySequence& shortcut = {})
{
    PageAction action;
    action.id = id;
    action.labelKey = id;
    action.needs = needs;
    action.shortcut = shortcut;
    action.run = [](const ActionContext&) { return Core::Status::ok(); };
    return action;
}

QPushButton* actionButton(ListPage& page, const QString& id)
{
    return page.findChild<QPushButton*>(QStringLiteral("listAction_%1").arg(id));
}

}  // namespace

TEST(ListPage, SearchDebounce250msBeforeSetQuery)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Ada")}};
    spec.nextTotal = 1;

    DataEvents events;
    ListPage page(&spec, events, defaultConfig());
    ASSERT_GE(spec.loadCount, 1);
    const int loadsAfterConstruct = spec.loadCount;

    auto* search = page.findChild<QLineEdit*>(QStringLiteral("listSearch"));
    ASSERT_NE(search, nullptr);
    search->setText(QStringLiteral("ada"));

    EXPECT_EQ(spec.loadCount, loadsAfterConstruct);
    QTest::qWait(100);
    EXPECT_EQ(spec.loadCount, loadsAfterConstruct);

    QTest::qWait(200);
    EXPECT_EQ(spec.loadCount, loadsAfterConstruct + 1);
    EXPECT_EQ(spec.lastState.search, QStringLiteral("ada"));
    EXPECT_EQ(spec.lastState.page, 1);
}

TEST(ListPage, EnterSearchesImmediately)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Ada")}};
    spec.nextTotal = 1;

    DataEvents events;
    ListPage page(&spec, events, defaultConfig());
    const int loadsAfterConstruct = spec.loadCount;

    auto* search = page.findChild<QLineEdit*>(QStringLiteral("listSearch"));
    ASSERT_NE(search, nullptr);
    search->setText(QStringLiteral("ben"));
    QTest::keyClick(search, Qt::Key_Return);

    EXPECT_EQ(spec.loadCount, loadsAfterConstruct + 1);
    EXPECT_EQ(spec.lastState.search, QStringLiteral("ben"));
    EXPECT_EQ(spec.lastState.page, 1);

    const int afterEnter = spec.loadCount;
    QTest::qWait(300);
    EXPECT_EQ(spec.loadCount, afterEnter);
}

TEST(ListPage, ActionButtonsEnableByNeeds)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Ada")}, {2, QStringLiteral("Ben")}};
    spec.nextTotal = 2;
    spec.pageActions = {
        makeAction(QStringLiteral("add"), PageAction::Needs::Nothing),
        makeAction(QStringLiteral("edit"), PageAction::Needs::One),
        makeAction(QStringLiteral("archive"), PageAction::Needs::Some),
    };

    DataEvents events;
    ListPage page(&spec, events, defaultConfig());
    page.rebuildChrome();
    page.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&page));

    auto* add = actionButton(page, QStringLiteral("add"));
    auto* edit = actionButton(page, QStringLiteral("edit"));
    auto* archive = actionButton(page, QStringLiteral("archive"));
    ASSERT_NE(add, nullptr);
    ASSERT_NE(edit, nullptr);
    ASSERT_NE(archive, nullptr);

    page.table()->clearSelection();
    page.model()->clearTicks();
    QTest::qWait(0);

    EXPECT_TRUE(add->isEnabled());
    EXPECT_FALSE(edit->isEnabled());
    EXPECT_FALSE(archive->isEnabled());

    selectTableRow(page.table(), 0);
    QTest::qWait(0);
    EXPECT_TRUE(edit->isEnabled());
    EXPECT_TRUE(archive->isEnabled());

    ASSERT_TRUE(page.model()->setData(page.model()->index(0, 0), Qt::Checked, Qt::CheckStateRole));
    ASSERT_TRUE(page.model()->setData(page.model()->index(1, 0), Qt::Checked, Qt::CheckStateRole));
    page.table()->clearSelection();
    QTest::qWait(0);
    EXPECT_FALSE(edit->isEnabled());
    EXPECT_TRUE(archive->isEnabled());
}

TEST(ListPage, ContextMenuListsEnabledActions)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Ada")}};
    spec.nextTotal = 1;
    spec.pageActions = {
        makeAction(QStringLiteral("add"), PageAction::Needs::Nothing),
        makeAction(QStringLiteral("edit"), PageAction::Needs::One),
        makeAction(QStringLiteral("archive"), PageAction::Needs::Some),
    };

    DataEvents events;
    ListPage page(&spec, events, defaultConfig());
    page.rebuildChrome();
    page.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&page));

    selectTableRow(page.table(), 0);
    QTest::qWait(0);

    QStringList texts;
    bool sawMenu = false;
    QTimer::singleShot(0, &page, [&texts, &sawMenu] {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu == nullptr) {
            return;
        }
        sawMenu = true;
        for (QAction* action : menu->actions()) {
            if (!action->isSeparator()) {
                texts.append(action->text());
            }
        }
        menu->close();
    });

    const QPoint cell = page.table()->visualRect(page.model()->index(0, 0)).center();
    const QPoint global = page.table()->viewport()->mapToGlobal(cell);
    QContextMenuEvent event(QContextMenuEvent::Mouse, cell, global, Qt::NoModifier);
    ASSERT_TRUE(QApplication::sendEvent(page.table()->viewport(), &event));
    QTest::qWait(20);

    EXPECT_TRUE(sawMenu) << "context menu did not open";
    EXPECT_TRUE(texts.contains(QStringLiteral("add")));
    EXPECT_TRUE(texts.contains(QStringLiteral("edit")));
    EXPECT_TRUE(texts.contains(QStringLiteral("archive")));
}

TEST(ListPage, TableScopedShortcutsDoNotStealSearchCtrlA)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Ada")}, {2, QStringLiteral("Ben")}};
    spec.nextTotal = 2;
    spec.pageActions = {
        makeAction(QStringLiteral("tickAll"), PageAction::Needs::Nothing,
                   QKeySequence(QStringLiteral("Ctrl+A"))),
    };

    DataEvents events;
    ListPage page(&spec, events, defaultConfig());
    page.rebuildChrome();
    page.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&page));

    auto* search = page.findChild<QLineEdit*>(QStringLiteral("listSearch"));
    ASSERT_NE(search, nullptr);
    search->setFocus();
    search->setText(QStringLiteral("hello"));
    search->setCursorPosition(search->text().size());
    QTest::keyClick(search, Qt::Key_A, Qt::ControlModifier);
    QTest::qWait(0);

    EXPECT_TRUE(page.model()->checkedIds().isEmpty());
    EXPECT_EQ(search->selectedText(), QStringLiteral("hello"));

    page.table()->setFocus();
    QTest::keyClick(page.table(), Qt::Key_A, Qt::ControlModifier);
    QTest::qWait(0);
    EXPECT_EQ(page.model()->checkedIds().size(), 2);
}

TEST(ListPage, FailedLoadShowsRetryStrip)
{
    FakeListSpec spec;
    spec.nextRows = {{1, QStringLiteral("Ada")}};
    spec.nextTotal = 1;

    DataEvents events;
    ListPage page(&spec, events, defaultConfig());
    ASSERT_EQ(page.model()->rowCount(), 1);

    auto* strip = page.findChild<QWidget*>(QStringLiteral("listErrorStrip"));
    ASSERT_NE(strip, nullptr);
    EXPECT_FALSE(strip->isVisibleTo(&page));

    qRegisterMetaType<Core::Error>("VLMS::Core::Error");
    spec.failNext = true;
    page.model()->reload();
    QTest::qWait(0);

    EXPECT_TRUE(strip->isVisibleTo(&page));
    EXPECT_EQ(page.model()->rowCount(), 1);

    auto* retry = page.findChild<QPushButton*>(QStringLiteral("listErrorRetry"));
    ASSERT_NE(retry, nullptr);
    const int loadsBeforeRetry = spec.loadCount;
    QTest::mouseClick(retry, Qt::LeftButton);
    QTest::qWait(0);

    EXPECT_EQ(spec.loadCount, loadsBeforeRetry + 1);
    EXPECT_FALSE(strip->isVisibleTo(&page));
}

TEST(ListPage, SelectionCallsPreview)
{
    FakeListSpec spec;
    spec.nextRows = {{10, QStringLiteral("Ada")}, {20, QStringLiteral("Ben")}};
    spec.nextTotal = 2;

    DataEvents events;
    ListPage page(&spec, events, defaultConfig());
    page.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&page));

    const int before = spec.previewCount;
    selectTableRow(page.table(), 1);
    QTest::qWait(0);

    EXPECT_GT(spec.previewCount, before);
    EXPECT_EQ(spec.lastPreviewId, 20);
}
