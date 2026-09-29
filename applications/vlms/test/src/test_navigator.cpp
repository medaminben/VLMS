#include "Fakes.h"

#include <gtest/gtest.h>

TEST(FakeNavigator, RecordsShowMemberLoans)
{
    VLMS::FakeNavigator nav;
    nav.showMemberLoans(QStringLiteral("M-1"));
    EXPECT_EQ(nav.showMemberLoansCount, 1);
    EXPECT_EQ(nav.lastPage, VLMS::PageId::Circulation);
    EXPECT_EQ(nav.lastMembershipNumber, QStringLiteral("M-1"));
}
