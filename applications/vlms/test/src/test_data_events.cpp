#include "ui/workflows/DataEvents.h"

#include <QList>
#include <QSignalSpy>

#include <gtest/gtest.h>

TEST(DataEvents, FlagsCombineWithOr)
{
    const VLMS::EntitySet set = VLMS::Entity::Books | VLMS::Entity::Copies;
    EXPECT_TRUE(set.testFlag(VLMS::Entity::Books));
    EXPECT_TRUE(set.testFlag(VLMS::Entity::Copies));
    EXPECT_FALSE(set.testFlag(VLMS::Entity::Members));
}

TEST(DataEvents, ChangedEmitsWhatAndIds)
{
    VLMS::DataEvents events;
    QSignalSpy spy(&events, &VLMS::DataEvents::changed);

    const QList<qint64> ids{7, 9};
    emit events.changed(VLMS::Entity::Members | VLMS::Entity::Loans, ids);

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).value<VLMS::EntitySet>(),
              VLMS::Entity::Members | VLMS::Entity::Loans);
    EXPECT_EQ(spy.at(0).at(1).value<QList<qint64>>(), ids);
}
