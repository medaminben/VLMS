#include "WorkflowTestSupport.h"

#include "QtBridge.h"
#include "ui/catalog/LocalNumberDelegate.h"
#include "ui/presentation/BookListSpec.h"
#include "ui/presentation/PagedQueryModel.h"
#include "ui/presentation/QueryState.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Repositories/CirculationRepository.h>

#include "TestDatabase.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>

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

qint64 fillOldStyleTable(QTableWidget& table,
                         Repositories::CatalogRepository& catalog,
                         const std::string& search)
{
    QElapsedTimer timer;
    timer.start();

    // Mirror CatalogPage::refreshBooks query shape (count → list → fill).
    Repositories::BookQuery query = showAllQuery(search);
    const auto totalCount = catalog.countBooks(query);
    EXPECT_TRUE(totalCount) << totalCount.error().detail;
    if (!totalCount) {
        return timer.elapsed();
    }

    query.limit = totalCount.value() > 0 ? totalCount.value() : 1;
    query.offset = 0;
    const auto booksResult = catalog.listBooks(query);
    EXPECT_TRUE(booksResult) << booksResult.error().detail;
    if (!booksResult) {
        return timer.elapsed();
    }

    const auto& books = booksResult.value();
    table.setRowCount(static_cast<int>(books.size()));
    for (int row = 0; row < static_cast<int>(books.size()); ++row) {
        const Repositories::BookRecord& book = books.at(static_cast<size_t>(row));

        auto* titleItem = new QTableWidgetItem(qs(book.title));
        titleItem->setData(Qt::UserRole, QVariant::fromValue(book.id));
        table.setItem(row, 0, titleItem);
        table.setItem(row, 1, new QTableWidgetItem(qs(book.authorName)));
        table.setItem(row, 2, new QTableWidgetItem(qs(book.categoryLabel)));

        auto* localItem = new QTableWidgetItem;
        QStringList localNumbers;
        localNumbers.reserve(static_cast<int>(book.localIds.size()));
        for (const std::string& localId : book.localIds) {
            localNumbers.append(qs(localId));
        }
        localItem->setData(LocalNumberDelegate::kNumbersRole, localNumbers);
        if (!book.matchedLocalId.empty()) {
            localItem->setData(LocalNumberDelegate::kSelectedRole, qs(book.matchedLocalId));
        }
        QStringList onLoanNumbers;
        onLoanNumbers.reserve(static_cast<int>(book.localIdsOnLoan.size()));
        for (const std::string& localId : book.localIdsOnLoan) {
            onLoanNumbers.append(qs(localId));
        }
        localItem->setData(LocalNumberDelegate::kOnLoanRole, onLoanNumbers);
        table.setItem(row, 3, localItem);

        table.setItem(row, 4, new QTableWidgetItem(QString::number(book.totalCopies)));
        table.setItem(row, 5, new QTableWidgetItem(QString::number(book.availableCopies)));
    }

    table.viewport()->repaint();
    return timer.elapsed();
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

    QTableWidget oldTable(0, 6);
    oldTable.resize(kViewWidth, kViewHeight);
    oldTable.setItemDelegateForColumn(3, new LocalNumberDelegate(&oldTable));
    oldTable.show();

    QueryState showAll;
    showAll.pageSize = 0;
    showAll.page = 1;
    QueryState searched = showAll;
    searched.search = QStringLiteral("the");

    // Warm both paths once (first Show-All load).
    model.setQuery(showAll);
    ASSERT_EQ(model.rowCount(), liveTotal.value());
    view.viewport()->repaint();
    fillOldStyleTable(oldTable, *catalog, {});
    ASSERT_EQ(oldTable.rowCount(), liveTotal.value());

    std::vector<qint64> newSamples;
    std::vector<qint64> oldSamples;
    newSamples.reserve(kTrials);
    oldSamples.reserve(kTrials);

    for (int trial = 0; trial < kTrials; ++trial) {
        model.setQuery(searched);
        fillOldStyleTable(oldTable, *catalog, "the");

        QElapsedTimer timer;
        timer.start();
        model.setQuery(showAll);
        view.viewport()->repaint();
        newSamples.push_back(timer.elapsed());

        oldSamples.push_back(fillOldStyleTable(oldTable, *catalog, {}));
    }

    ASSERT_EQ(model.rowCount(), liveTotal.value());
    ASSERT_EQ(oldTable.rowCount(), liveTotal.value());

    const qint64 newRefreshMs = medianMs(newSamples);
    const qint64 oldRefreshMs = medianMs(oldSamples);
    RecordProperty("newRefreshMs", static_cast<int>(newRefreshMs));
    RecordProperty("oldRefreshMs", static_cast<int>(oldRefreshMs));
    std::cout << "Catalog Show-All medians: new=" << newRefreshMs
              << " ms, old=" << oldRefreshMs << " ms\n";

    std::ostringstream sampleLog;
    sampleLog << "new samples:";
    for (qint64 sample : newSamples) {
        sampleLog << ' ' << sample;
    }
    sampleLog << "; old samples:";
    for (qint64 sample : oldSamples) {
        sampleLog << ' ' << sample;
    }

    EXPECT_LT(newRefreshMs, kRefreshBudgetMs)
        << "new Show-All refresh median " << newRefreshMs << " ms (budget " << kRefreshBudgetMs
        << " ms); " << sampleLog.str();

    // Until Task 14: old QTableWidget fill must not beat the new path.
    // Debug builds pay extra in QAbstractItemModel reset + ListSpec display
    // rebuild (~10 ms here) vs QTableWidgetItem churn; RelWithDebInfo/Release
    // compare with zero slack. Do not widen the 300 ms budget for this.
#if defined(NDEBUG)
    constexpr qint64 kOldPathSlackMs = 0;
#else
    constexpr qint64 kOldPathSlackMs = 20;
#endif
    EXPECT_LE(newRefreshMs, oldRefreshMs + kOldPathSlackMs)
        << "new path median " << newRefreshMs << " ms vs old path median " << oldRefreshMs
        << " ms (slack " << kOldPathSlackMs << "); " << sampleLog.str();
}
