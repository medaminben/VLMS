#include "Fakes.h"

#include "ui/workflows/BulkRunner.h"
#include "ui/workflows/DataEvents.h"

#include <VLMS/Core/Locale.h>

#include <QSignalSpy>
#include <QWidget>

#include <gtest/gtest.h>

namespace {

QList<BulkRow> rowsFor(const QList<qint64>& ids)
{
    QList<BulkRow> rows;
    for (int i = 0; i < ids.size(); ++i) {
        rows.append(BulkRow{ids.at(i), i, QStringLiteral("Row %1").arg(ids.at(i))});
    }
    return rows;
}

VLMS::Core::Status blocked()
{
    return VLMS::Core::Status::fail(VLMS::Core::ErrorKind::Validation, "error.loan.archiveOpen");
}

}  // namespace

class BulkRunner : public ::testing::Test {
protected:
    static void SetUpTestSuite() { VLMS::Core::Locale::setCode("en"); }
    static void TearDownTestSuite()
    {
        VLMS::Core::Locale::setCode(VLMS::Core::Locale::kDefaultCode);
    }

    int run(const QList<qint64>& ids,
            const std::function<VLMS::Core::Status(const BulkRow&)>& check,
            const std::function<VLMS::Core::Status(const BulkRow&)>& act)
    {
        BulkActionTexts texts;
        texts.title = QStringLiteral("Archive");
        texts.verb = QStringLiteral("archive");
        texts.passive = QStringLiteral("archived");
        texts.noun = QStringLiteral("loans");
        return runner.run(&parent, texts, rowsFor(ids), check, act, VLMS::Entity::Loans);
    }

    VLMS::FakeDialogFactory dialogs;
    VLMS::DataEvents events;
    VLMS::BulkRunner runner{dialogs, events};
    QSignalSpy changed{&events, &VLMS::DataEvents::changed};
    QWidget parent;
    QList<qint64> acted;

    std::function<VLMS::Core::Status(const BulkRow&)> pass = [](const BulkRow&) {
        return VLMS::Core::Status::ok();
    };
    std::function<VLMS::Core::Status(const BulkRow&)> record = [this](const BulkRow& row) {
        acted.append(row.id);
        return VLMS::Core::Status::ok();
    };
};

TEST_F(BulkRunner, DeclinedConfirmRunsNothingAndEmitsNothing)
{
    dialogs.confirmBulkResult = false;

    EXPECT_EQ(run({1, 2, 3}, pass, record), 0);

    EXPECT_EQ(dialogs.count(QStringLiteral("confirmBulk")), 1);
    EXPECT_TRUE(acted.isEmpty());
    EXPECT_EQ(changed.count(), 0);
}

TEST_F(BulkRunner, ConfirmedRunsEveryRowAndEmitsOnce)
{
    dialogs.confirmBulkResult = true;

    EXPECT_EQ(run({1, 2, 3}, pass, record), 3);

    EXPECT_EQ(acted, QList<qint64>({1, 2, 3}));
    EXPECT_EQ(dialogs.count(QStringLiteral("startProgress")), 1);
    ASSERT_EQ(changed.count(), 1);
    EXPECT_EQ(changed.at(0).at(1).value<QList<qint64>>(), QList<qint64>({1, 2, 3}));
    EXPECT_EQ(dialogs.count(QStringLiteral("inform")), 0);
}

TEST_F(BulkRunner, BlockedRowsAreNamedInThePlanAndSkipped)
{
    dialogs.confirmBulkResult = true;
    const auto check = [](const BulkRow& row) {
        return row.id == 2 ? blocked() : VLMS::Core::Status::ok();
    };

    EXPECT_EQ(run({1, 2, 3}, check, record), 2);

    ASSERT_EQ(dialogs.plans.size(), 1U);
    EXPECT_TRUE(dialogs.plans.front().text.contains(QStringLiteral("Row 2")));
    EXPECT_EQ(acted, QList<qint64>({1, 3}));
    EXPECT_EQ(changed.at(0).at(1).value<QList<qint64>>(), QList<qint64>({1, 3}));
}

TEST_F(BulkRunner, EveryRowBlockedInformsAndRunsNothing)
{
    EXPECT_EQ(run({1, 2}, [](const BulkRow&) { return blocked(); }, record), 0);

    EXPECT_EQ(dialogs.count(QStringLiteral("confirmBulk")), 0);
    EXPECT_EQ(dialogs.count(QStringLiteral("inform")), 1);
    EXPECT_TRUE(acted.isEmpty());
    EXPECT_EQ(changed.count(), 0);
}

TEST_F(BulkRunner, FailedRowsEndInOneSummaryAndOnlySuccessesAreEmitted)
{
    dialogs.confirmBulkResult = true;
    const auto act = [this](const BulkRow& row) {
        acted.append(row.id);
        return row.id == 3 ? blocked() : VLMS::Core::Status::ok();
    };

    EXPECT_EQ(run({1, 2, 3}, pass, act), 2);

    ASSERT_EQ(dialogs.informs.size(), 1U);
    EXPECT_TRUE(dialogs.informs.front().text.contains(QStringLiteral("2")));
    ASSERT_EQ(changed.count(), 1);
    EXPECT_EQ(changed.at(0).at(1).value<QList<qint64>>(), QList<qint64>({1, 2}));
}
