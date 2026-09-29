#include "ui/workflows/FakeDialogFactory.h"

#include <gtest/gtest.h>

TEST(FakeDialogFactory, ReturnsScriptedBookForm)
{
    VLMS::FakeDialogFactory factory;
    VLMS::BookForm form;
    form.book.title = "T";
    factory.nextBook = form;

    const auto got = factory.addBook(nullptr);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->book.title, "T");
    ASSERT_EQ(factory.calls.size(), 1u);
    EXPECT_EQ(factory.calls.front(), QStringLiteral("addBook"));
}

TEST(FakeDialogFactory, CancelReturnsEmpty)
{
    VLMS::FakeDialogFactory factory;
    EXPECT_FALSE(factory.editMember(nullptr, {}).has_value());
}
