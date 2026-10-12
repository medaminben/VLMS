#include "ui/views/UiHelpers.h"

#include <QLabel>
#include <QMessageBox>
#include <QWidget>

#include <gtest/gtest.h>

// The detail values beside every list (title, author, notes...) come from this
// one factory; the label variant must not render markup.
TEST(PlainText, DetailValueLabelsShowMarkupAsTyped)
{
    QWidget parent;
    QWidget* value = VLMS::makeDetailValueWidget(&parent, false);
    auto* label = qobject_cast<QLabel*>(value);
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->textFormat(), Qt::PlainText);
}

// Every message box passes through localizeMessageBox; confirmations quote
// titles and names.
TEST(PlainText, MessageBoxesShowMarkupAsTyped)
{
    QMessageBox box(QMessageBox::Question, QStringLiteral("t"),
                    QStringLiteral("<b>Delete</b> this?"));
    VLMS::localizeMessageBox(&box);
    EXPECT_EQ(box.textFormat(), Qt::PlainText);
}
