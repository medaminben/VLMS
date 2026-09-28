#include <VLMS/Core/Result.h>

#include <gtest/gtest.h>

using namespace VLMS;

TEST(test_core_Result, ValuedOkHoldsTheValue)
{
    const auto result = Core::Result<int>::ok(7);
    EXPECT_TRUE(result);
    EXPECT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 7);
    EXPECT_EQ(*result, 7);
}

TEST(test_core_Result, ValuedFailHasNoValueAndCarriesKind)
{
    const auto result = Core::Result<int>::fail(Core::ErrorKind::Sql, "error.sql");
    EXPECT_FALSE(result);
    EXPECT_FALSE(result.hasValue());
    EXPECT_EQ(result.kind(), Core::ErrorKind::Sql);
    EXPECT_EQ(result.error().key, "error.sql");
}

TEST(test_core_Result, NotFoundIsDistinctFromSql)
{
    const auto missing = Core::Result<int>::fail(Core::ErrorKind::NotFound, "error.book.notFound");
    EXPECT_FALSE(missing);
    EXPECT_EQ(missing.kind(), Core::ErrorKind::NotFound);
    EXPECT_NE(missing.kind(), Core::ErrorKind::Sql);
}

TEST(test_core_Result, StatusOkIsTruthy)
{
    EXPECT_TRUE(Core::Status::ok());
}

TEST(test_core_Result, StatusFailCarriesKeyAndDetail)
{
    const auto status = Core::Status::fail(Core::ErrorKind::Validation, "error.copy.duplicateGlobal", "AR-1");
    EXPECT_FALSE(status);
    EXPECT_EQ(status.kind(), Core::ErrorKind::Validation);
    EXPECT_EQ(status.error().key, "error.copy.duplicateGlobal");
    EXPECT_EQ(status.error().detail, "AR-1");
}

TEST(test_core_Result, AsStatusMapsAFailedResult)
{
    const auto result = Core::Result<int>::fail(Core::ErrorKind::NotFound, "error.member.notFound");
    const auto status = Core::asStatus(result);
    EXPECT_FALSE(status);
    EXPECT_EQ(status.kind(), Core::ErrorKind::NotFound);
    EXPECT_EQ(status.error().key, "error.member.notFound");
}
