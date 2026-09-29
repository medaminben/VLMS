#include "Fakes.h"

#include <gtest/gtest.h>

TEST(FakeDialogFactory, ReturnsScriptedBookForm)
{
    VLMS::FakeDialogFactory factory;
    VLMS::BookForm form;
    form.book.title = "T";
    factory.nextBook = form;

    const auto got = factory.editBook(nullptr, {});
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->book.title, "T");
    ASSERT_EQ(factory.calls.size(), 1u);
    EXPECT_EQ(factory.calls.front(), QStringLiteral("editBook"));
}

TEST(FakeDialogFactory, CancelReturnsEmpty)
{
    VLMS::FakeDialogFactory factory;
    EXPECT_FALSE(factory.editMember(nullptr, std::nullopt).has_value());
}
