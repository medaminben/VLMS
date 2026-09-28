#include <VLMS/Core/Result.h>

#include <gtest/gtest.h>

using namespace VLMS;

using VLMS::ErrorKind;
using VLMS::Result;
using VLMS::Status;

TEST(test_core_Result, ValuedOkHoldsTheValue)
{
    const auto result = Result<int>::ok(7);
    EXPECT_TRUE(result);
    EXPECT_TRUE(result.hasValue());
    EXPECT_EQ(result.value(), 7);
    EXPECT_EQ(*result, 7);
}

TEST(test_core_Result, ValuedFailHasNoValueAndCarriesKind)
{
    const auto result = Result<int>::fail(ErrorKind::Sql, "error.sql");
    EXPECT_FALSE(result);
    EXPECT_FALSE(result.hasValue());
    EXPECT_EQ(result.kind(), ErrorKind::Sql);
    EXPECT_EQ(result.error().key, "error.sql");
}

TEST(test_core_Result, NotFoundIsDistinctFromSql)
{
    const auto missing = Result<int>::fail(ErrorKind::NotFound, "error.book.notFound");
    EXPECT_FALSE(missing);
    EXPECT_EQ(missing.kind(), ErrorKind::NotFound);
    EXPECT_NE(missing.kind(), ErrorKind::Sql);
}

TEST(test_core_Result, StatusOkIsTruthy)
{
    EXPECT_TRUE(Status::ok());
}

TEST(test_core_Result, StatusFailCarriesKeyAndDetail)
{
    const auto status = Status::fail(ErrorKind::Validation, "error.copy.duplicateGlobal", "AR-1");
    EXPECT_FALSE(status);
    EXPECT_EQ(status.kind(), ErrorKind::Validation);
    EXPECT_EQ(status.error().key, "error.copy.duplicateGlobal");
    EXPECT_EQ(status.error().detail, "AR-1");
}

TEST(test_core_Result, AsStatusMapsAFailedResult)
{
    const auto result = Result<int>::fail(ErrorKind::NotFound, "error.member.notFound");
    const auto status = VLMS::asStatus(result);
    EXPECT_FALSE(status);
    EXPECT_EQ(status.kind(), ErrorKind::NotFound);
    EXPECT_EQ(status.error().key, "error.member.notFound");
}
