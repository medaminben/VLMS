#include "WorkflowTestSupport.h"

#include "ui/presentation/BookListSpec.h"
#include "ui/presentation/PagedQueryModel.h"
#include "ui/presentation/QueryState.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>

#include "TestDatabase.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTableView>

#include <gtest/gtest.h>

#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <vector>

using namespace VLMS;
using namespace Test;

namespace {

constexpr qint64 kRefreshBudgetMs = 300;
constexpr int kViewWidth = 1280;
constexpr int kViewHeight = 800;
constexpr int kTrials = 5;

QString bundledDatabasePath()
{
    // Prefer the compile-time path: gtest_discover_tests splits the PROPERTIES
    // ENVIRONMENT string on ';' so VLMS_SCHEMA_PATH never reaches the process.
#ifdef VLMS_BENCH_DB_PATH
    {
        const QString compiled = QStringLiteral(VLMS_BENCH_DB_PATH);
        if (QFileInfo::exists(compiled)) {
            return compiled;
        }
    }
#endif

    const QByteArray schema = qgetenv("VLMS_SCHEMA_PATH");
    if (!schema.isEmpty()) {
        const QFileInfo schemaFile(QString::fromUtf8(schema));
        const QString candidate = schemaFile.dir().filePath(QStringLiteral("vlms.db"));
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

#ifdef VLMS_SCHEMA_PATH
    {
        const QFileInfo schemaFile(QStringLiteral(VLMS_SCHEMA_PATH));
        const QString candidate = schemaFile.dir().filePath(QStringLiteral("vlms.db"));
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
#endif

    const QStringList fallbacks = {
        QStringLiteral("database/vlms.db"),
        QStringLiteral("../database/vlms.db"),
        QStringLiteral("../../database/vlms.db"),
        QStringLiteral("../../../database/vlms.db"),
    };
    for (const QString& path : fallbacks) {
        if (QFileInfo::exists(path)) {
            return QFileInfo(path).absoluteFilePath();
        }
    }
    return {};
}

Repositories::BookQuery showAllQuery(const std::string& search = {})
{
    Repositories::BookQuery query;
    query.search = search;
    query.limit = std::numeric_limits<int>::max();
    query.offset = 0;
    return query;
}

qint64 medianMs(std::vector<qint64> samples)
{
    EXPECT_FALSE(samples.empty());
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

}  // namespace

TEST(VlmsBench, CatalogShowAllRefreshUnderBudget)
{
    if (qEnvironmentVariableIsSet("VLMS_SKIP_BENCH")) {
        GTEST_SKIP() << "VLMS_SKIP_BENCH is set";
    }

    const QString sourceDb = bundledDatabasePath();
    ASSERT_FALSE(sourceDb.isEmpty()) << "bundled database/vlms.db not found";

    TestDatabase db(TestDatabase::Mode::FromCopyOf, sourceDb.toStdString());
    ASSERT_TRUE(db.isValid()) << db.lastError();
    ASSERT_EQ(db.count("books"), 3071);
    ASSERT_EQ(db.count("book_copies"), 4712);

    auto catalog = std::make_unique<Repositories::CatalogRepository>(db.session(),
                                                                     db.resourcesDirectory());
    auto circulation = std::make_unique<Repositories::CirculationRepository>(db.session());
    BookWorkflowHarness harness(*catalog, *circulation);
    BookListSpec spec(*catalog, *harness.books);
    DataEvents events;
    PagedQueryModel model(&spec, events);

    const auto liveTotal = catalog->countBooks(showAllQuery());
    ASSERT_TRUE(liveTotal) << liveTotal.error().detail;
    // Bundled DB has a few archived titles; Show-All lists live rows only.
    ASSERT_EQ(liveTotal.value(), 3067);

    QTableView view;
    view.resize(kViewWidth, kViewHeight);
    view.setModel(&model);
    view.show();

    QueryState showAll;
    showAll.pageSize = 0;
    showAll.page = 1;
    QueryState searched = showAll;
    searched.search = QStringLiteral("the");

    // Warm once (first Show-All load).
    model.setQuery(showAll);
    ASSERT_EQ(model.rowCount(), liveTotal.value());
    view.viewport()->repaint();

    std::vector<qint64> samples;
    samples.reserve(kTrials);

    for (int trial = 0; trial < kTrials; ++trial) {
        model.setQuery(searched);

        QElapsedTimer timer;
        timer.start();
        model.setQuery(showAll);
        view.viewport()->repaint();
        samples.push_back(timer.elapsed());
    }

    ASSERT_EQ(model.rowCount(), liveTotal.value());

    const qint64 refreshMs = medianMs(samples);
    RecordProperty("refreshMs", static_cast<int>(refreshMs));
    std::cout << "Catalog Show-All median: " << refreshMs << " ms\n";

    std::ostringstream sampleLog;
    sampleLog << "samples:";
    for (qint64 sample : samples) {
        sampleLog << ' ' << sample;
    }

    EXPECT_LT(refreshMs, kRefreshBudgetMs)
        << "Show-All refresh median " << refreshMs << " ms (budget " << kRefreshBudgetMs
        << " ms); " << sampleLog.str();
}
