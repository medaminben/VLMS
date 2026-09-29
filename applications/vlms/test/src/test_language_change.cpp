#include "Application.h"

#include "TestDatabase.h"
#include "TestSeed.h"
#include "WorkflowTestSupport.h"

#include "ui/ListPage.h"
#include "ui/presentation/BookListSpec.h"
#include "ui/UiHelpers.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QStandardPaths>
#include <QTableView>
#include <QWidget>

#include <gtest/gtest.h>

using namespace VLMS;
using namespace Test;

namespace {

class LanguageChangeRecorder final : public QWidget {
public:
    int languageChangeCount = 0;

protected:
    void changeEvent(QEvent* event) override
    {
        if (event != nullptr && event->type() == QEvent::LanguageChange) {
            ++languageChangeCount;
        }
        QWidget::changeEvent(event);
    }
};

[[nodiscard]] Application* appInstance()
{
    return qobject_cast<Application*>(QCoreApplication::instance());
}

}  // namespace

TEST(LanguageChange, SetUiLocaleSendsLanguageChangeToTopLevelWidgets)
{
    Application* app = appInstance();
    ASSERT_NE(app, nullptr);

    LanguageChangeRecorder recorder;
    recorder.setAttribute(Qt::WA_DontShowOnScreen, true);
    recorder.show();
    ASSERT_TRUE(recorder.isWindow());

    const QString start = app->uiLocale();
    const QString next = (start == QLatin1String("en")) ? QStringLiteral("fr")
                                                        : QStringLiteral("en");
    const int before = recorder.languageChangeCount;

    app->setUiLocale(next);

    EXPECT_GT(recorder.languageChangeCount, before);
    EXPECT_EQ(app->uiLocale(), next);

    app->setUiLocale(start);
}

TEST(LanguageChange, NoLanguageChangedSignalExists)
{
    EXPECT_LT(Application::staticMetaObject.indexOfSignal("languageChanged()"), 0);

    Application* app = appInstance();
    ASSERT_NE(app, nullptr);

    TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    seedBook(db, uniqueBookSeed(2));

    Repositories::CatalogRepository catalog(db.session(), db.resourcesDirectory());
    Repositories::MemberRepository members(db.session(), db.resourcesDirectory());
    Repositories::CirculationRepository circulation(db.session());
    BookWorkflowHarness workflows(catalog, circulation, members);
    BookListSpec spec(catalog, *workflows.books);

    ListPage::Config cfg;
    cfg.titleKey = QStringLiteral("page.catalog.title");
    cfg.bodyKey = QStringLiteral("page.catalog.body");
    cfg.searchPlaceholderKey = QStringLiteral("catalog.searchPlaceholder");
    cfg.imageObjectName = QStringLiteral("bookCover");
    cfg.imageBounds = bookCoverPreviewBounds();
    // Top-level so Application::setUiLocale's broadcast reaches this page.
    ListPage page(&spec, workflows.events, cfg, /*parent=*/nullptr);
    page.setAttribute(Qt::WA_DontShowOnScreen, true);
    page.QWidget::show();

    auto* title = page.findChild<QLabel*>(QStringLiteral("pageTitle"));
    ASSERT_NE(title, nullptr);

    const QString start = app->uiLocale();
    app->setUiLocale(QStringLiteral("en"));
    const QString english = title->text();
    ASSERT_FALSE(english.isEmpty());

    app->setUiLocale(QStringLiteral("ar"));
    EXPECT_NE(title->text(), english);

    app->setUiLocale(start);
}

TEST(LanguageChange, ListPageRebuildsColumnHeadersWithoutReloadQuery)
{
    const std::string restore = Core::Locale::code();
    Core::Locale::setCode("en");

    TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    seedBook(db, uniqueBookSeed(1));

    Repositories::CatalogRepository catalog(db.session(), db.resourcesDirectory());
    Repositories::MemberRepository members(db.session(), db.resourcesDirectory());
    Repositories::CirculationRepository circulation(db.session());
    BookWorkflowHarness workflows(catalog, circulation, members);
    BookListSpec spec(catalog, *workflows.books);

    ListPage::Config cfg;
    cfg.titleKey = QStringLiteral("page.catalog.title");
    cfg.bodyKey = QStringLiteral("page.catalog.body");
    cfg.searchPlaceholderKey = QStringLiteral("catalog.searchPlaceholder");
    cfg.imageObjectName = QStringLiteral("bookCover");
    cfg.imageBounds = bookCoverPreviewBounds();
    ListPage page(&spec, workflows.events, cfg);
    page.setAttribute(Qt::WA_DontShowOnScreen, true);
    page.show();

    QTableView* table = page.table();
    ASSERT_NE(table, nullptr);
    ASSERT_NE(table->model(), nullptr);
    ASSERT_GT(table->model()->columnCount(), 0);

    const QString englishHeader =
        table->model()->headerData(0, Qt::Horizontal, Qt::DisplayRole).toString();
    ASSERT_FALSE(englishHeader.isEmpty());

    const QueryState queryBefore = page.model()->query();
    const int rowsBefore = table->model()->rowCount();

    Core::Locale::setCode("ar");
    QEvent languageChange(QEvent::LanguageChange);
    QCoreApplication::sendEvent(&page, &languageChange);

    const QString arabicHeader =
        table->model()->headerData(0, Qt::Horizontal, Qt::DisplayRole).toString();
    EXPECT_NE(arabicHeader, englishHeader);
    EXPECT_EQ(page.model()->query().search, queryBefore.search);
    EXPECT_EQ(page.model()->query().page, queryBefore.page);
    EXPECT_EQ(page.model()->query().pageSize, queryBefore.pageSize);
    EXPECT_EQ(table->model()->rowCount(), rowsBefore);

    Core::Locale::setCode(restore);
}

TEST(LanguageChange, RtlLayoutAppliedForArabic)
{
    Application* app = appInstance();
    ASSERT_NE(app, nullptr);

    const QString start = app->uiLocale();
    app->setUiLocale(QStringLiteral("en"));
    EXPECT_EQ(app->layoutDirection(), Qt::LeftToRight);

    app->setUiLocale(QStringLiteral("ar"));
    EXPECT_EQ(app->layoutDirection(), Qt::RightToLeft);
    EXPECT_TRUE(Core::Locale::isRtl());

    app->setUiLocale(start);
}
