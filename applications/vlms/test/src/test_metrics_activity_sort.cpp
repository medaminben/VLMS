#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/metrics/MetricsPage.h"

#include <VLMS/Core/Date.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/MetricsRepository.h>
#include <VLMS/Core/MetricsTypes.h>

#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <gtest/gtest.h>

#include <memory>

using VLMS::Date;
using VLMS::Locale;
using namespace VLMS::Test;

class test_ui_MetricsActivitySort : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_metrics = std::make_unique<MetricsRepository>(m_db->session());

        MemberSeed member = uniqueMemberSeed(1);
        member.status = MemberStatus::kActive;
        m_memberId = seedMember(*m_db, member);
        ASSERT_GT(m_memberId, 0);
        ASSERT_TRUE(rawSetRegisteredAt(*m_db, m_memberId, "2019-01-15"));

        BookSeed book = uniqueBookSeed(1);
        book.initialCopyCount = 3;
        const std::int64_t bookId = seedBook(*m_db, book);
        ASSERT_GT(bookId, 0);
        m_copies = copyIdsOf(*m_db, bookId);
        ASSERT_EQ(m_copies.size(), 3u);
    }

    bool borrowOn(const int copyIndex, const Date& borrowed)
    {
        return rawInsertLoan(*m_db, m_memberId, m_copies.at(static_cast<std::size_t>(copyIndex)),
                             borrowed.toIso(), borrowed.addDays(14).toIso())
            > 0;
    }

    static int intAt(const QTableWidget* table, const int row, const int column)
    {
        return table->item(row, column)->data(Qt::DisplayRole).toInt();
    }

    static void expectPeriodMatches(const QTableWidget* table,
                                    const QString& label,
                                    const MetricsPeriodCounts& expected)
    {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0)->text() != label) {
                continue;
            }
            EXPECT_EQ(intAt(table, row, 1), expected.checkouts) << label.toStdString();
            EXPECT_EQ(intAt(table, row, 2), expected.returns) << label.toStdString();
            EXPECT_EQ(intAt(table, row, 3), expected.newMembers) << label.toStdString();
            return;
        }
        ADD_FAILURE() << "missing period row: " << label.toStdString();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MetricsRepository> m_metrics;
    std::int64_t m_memberId = 0;
    std::vector<std::int64_t> m_copies;
};

TEST_F(test_ui_MetricsActivitySort, RefreshKeepsCountsOnSortedPeriodRows)
{
    const Date today = Date::todayLocal();
    ASSERT_TRUE(borrowOn(0, today));
    ASSERT_TRUE(borrowOn(1, today.addDays(-6)));
    ASSERT_TRUE(borrowOn(2, today.addDays(-6)));

    const LibraryMetrics expected = VLMS_UNWRAP(m_metrics->fetchMetrics());
    ASSERT_GT(expected.thisWeek.checkouts, expected.today.checkouts);

    MetricsPage page(*m_metrics);
    auto* table = page.findChild<QTableWidget*>(QStringLiteral("metricsActivityTable"));
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 3);

    emit table->horizontalHeader()->sectionClicked(1);
    emit table->horizontalHeader()->sectionClicked(1);
    EXPECT_NE(table->item(0, 0)->text(), QStringLiteral("Today"));

    page.refreshMetrics();

    expectPeriodMatches(table, QStringLiteral("Today"), expected.today);
    expectPeriodMatches(table, QStringLiteral("Last 7 days"), expected.thisWeek);
    expectPeriodMatches(table, QStringLiteral("This month"), expected.thisMonth);
}
