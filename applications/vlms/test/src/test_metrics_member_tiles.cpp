#include "ui/presentation/ReadSources.h"
#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/views/MetricsPage.h"

#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MetricsRepository.h>

#include <QLabel>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

class test_ui_MetricsMemberTiles : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_metrics = std::make_unique<Repositories::MetricsRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_metrics.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MetricsRepository> m_metrics;
};

TEST_F(test_ui_MetricsMemberTiles, OnlyActiveAndNotActiveAreCounted)
{
    RepositoryMetrics source(*m_metrics);
    MetricsPage page(source);
    page.refreshMetrics();

    QStringList labels;
    for (QLabel* label : page.findChildren<QLabel*>(QStringLiteral("metricLabel"))) {
        labels << label->text();
    }
    EXPECT_TRUE(labels.contains(QStringLiteral("Total members")));
    EXPECT_TRUE(labels.contains(QStringLiteral("Active")));
    EXPECT_TRUE(labels.contains(QStringLiteral("Not active")));
    EXPECT_FALSE(labels.contains(QStringLiteral("Subscribed")));
    EXPECT_FALSE(labels.contains(QStringLiteral("Unsubscribed")));
    for (const QString& text : labels) {
        EXPECT_FALSE(text.startsWith(QStringLiteral("metrics."))) << text.toStdString();
    }
    // 4 overview + 3 members + 3 circulation.
    EXPECT_EQ(labels.size(), 10);
}

/// Checklist row 20: suite name must match `MetricsPage|DashboardPage`.
TEST(MetricsPage, RefreshPopulatesOverviewLabels)
{
    Core::Locale::setCode("en");
    TestDatabase db;
    ASSERT_TRUE(db.isValid()) << db.lastError();
    Repositories::MetricsRepository metrics(db.session());

    RepositoryMetrics source(metrics);
    MetricsPage page(source);
    page.refreshMetrics();

    QStringList labels;
    for (QLabel* label : page.findChildren<QLabel*>(QStringLiteral("metricLabel"))) {
        labels << label->text();
    }
    EXPECT_FALSE(labels.isEmpty());
    EXPECT_TRUE(labels.contains(QStringLiteral("Total members")));
    Core::Locale::setCode(Core::Locale::kDefaultCode);
}
