#include "ui/workflows/FakeErrorPresenter.h"
#include "ui/workflows/QtErrorPresenter.h"

#include <VLMS/Core/Result.h>

#include <QWidget>

#include <gtest/gtest.h>

TEST(FakeErrorPresenter, RecordsShowCalls)
{
    VLMS::FakeErrorPresenter presenter;
    QWidget parent;
    VLMS::Core::Error err;
    err.key = "error.sql";
    presenter.show(&parent, err);
    ASSERT_EQ(presenter.calls.size(), 1u);
    EXPECT_EQ(presenter.calls[0].parent, &parent);
    EXPECT_EQ(presenter.calls[0].key, "error.sql");
}

TEST(QtErrorPresenter, IsErrorPresenter)
{
    VLMS::QtErrorPresenter presenter;
    VLMS::ErrorPresenter* base = &presenter;
    EXPECT_NE(base, nullptr);
}
