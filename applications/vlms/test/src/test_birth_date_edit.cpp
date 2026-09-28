#include "ui/members/BirthDateEdit.h"

#include "ui/Theme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QPoint>
#include <QScrollBar>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionComboBox>
#include <QVBoxLayout>

#include <gtest/gtest.h>

#include <algorithm>

using namespace VLMS;

namespace {

void showWindow(QWidget& window)
{
    window.resize(420, 80);
    window.show();
    if (window.layout() != nullptr) {
        window.layout()->activate();
    }
    QApplication::processEvents();
}

QStringList texts(const QComboBox* box)
{
    QStringList values;
    for (int i = 0; i < box->count(); ++i) {
        values.append(box->itemText(i));
    }
    return values;
}

int editFieldWidth(const QComboBox* box)
{
    QStyleOptionComboBox option;
    option.initFrom(box);
    option.rect = box->rect();
    option.editable = false;
    option.subControls = QStyle::SC_All;
    return box->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, box).width();
}

}  // namespace

TEST(test_ui_BirthDateEdit, ListsAreCanonicalAndFiveRowsAreVisible)
{
    BirthDateEdit edit;
    EXPECT_EQ(texts(edit.dayCombo()).size(), 31);
    EXPECT_EQ(texts(edit.dayCombo()).first(), QStringLiteral("01"));
    EXPECT_EQ(texts(edit.dayCombo()).last(), QStringLiteral("31"));
    EXPECT_EQ(texts(edit.monthCombo()).size(), 12);
    EXPECT_EQ(texts(edit.monthCombo()).first(), QStringLiteral("01"));
    EXPECT_EQ(texts(edit.monthCombo()).last(), QStringLiteral("12"));
    EXPECT_EQ(texts(edit.yearCombo()).size(), 101);
    EXPECT_EQ(texts(edit.yearCombo()).first(), QStringLiteral("1916"));
    EXPECT_EQ(texts(edit.yearCombo()).last(), QStringLiteral("2016"));
    EXPECT_EQ(edit.dayCombo()->maxVisibleItems(), 5);
    EXPECT_EQ(edit.monthCombo()->maxVisibleItems(), 5);
    EXPECT_EQ(edit.yearCombo()->maxVisibleItems(), 5);
    EXPECT_EQ(edit.dayCombo()->currentIndex(), -1);
    EXPECT_TRUE(edit.dayCombo()->currentText().isEmpty());
    EXPECT_EQ(edit.monthCombo()->currentIndex(), -1);
    EXPECT_EQ(edit.yearCombo()->currentIndex(), -1);
    EXPECT_TRUE(edit.isoDate().isEmpty());
    EXPECT_TRUE(edit.findChildren<QLabel*>().isEmpty());
    EXPECT_EQ(edit.dayCombo()->layoutDirection(), Qt::LeftToRight);
    EXPECT_EQ(edit.monthCombo()->layoutDirection(), Qt::LeftToRight);
    EXPECT_EQ(edit.yearCombo()->layoutDirection(), Qt::LeftToRight);
}

TEST(test_ui_BirthDateEdit, CombosAreOnlyAsWideAsTheirSamples)
{
    QWidget window;
    window.setStyleSheet(QStringLiteral("QComboBox { border: 1px solid black; padding: 8px 12px; }"));
    auto* edit = new BirthDateEdit(&window);
    auto* layout = new QVBoxLayout(&window);
    layout->addWidget(edit);
    showWindow(window);

    EXPECT_EQ(edit->dayCombo()->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    EXPECT_EQ(edit->monthCombo()->width(), edit->dayCombo()->width());
    EXPECT_GT(edit->yearCombo()->width(), edit->dayCombo()->width());
    EXPECT_GE(editFieldWidth(edit->dayCombo()), edit->dayCombo()->fontMetrics().horizontalAdvance(QStringLiteral("00")));
    EXPECT_GE(editFieldWidth(edit->monthCombo()), edit->monthCombo()->fontMetrics().horizontalAdvance(QStringLiteral("00")));
    EXPECT_GE(editFieldWidth(edit->yearCombo()), edit->yearCombo()->fontMetrics().horizontalAdvance(QStringLiteral("0000")));
    EXPECT_EQ(edit->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
}

TEST(test_ui_BirthDateEdit, SetIsoDateSelectsPartsAndDoesNotMoveFocus)
{
    BirthDateEdit edit;
    showWindow(edit);
    edit.monthCombo()->setFocus();
    ASSERT_EQ(QApplication::focusWidget(), edit.monthCombo());

    edit.setIsoDate(QStringLiteral("1990-05-12"));
    EXPECT_EQ(edit.dayCombo()->currentText(), QStringLiteral("12"));
    EXPECT_EQ(edit.monthCombo()->currentText(), QStringLiteral("05"));
    EXPECT_EQ(edit.yearCombo()->currentText(), QStringLiteral("1990"));
    EXPECT_EQ(edit.isoDate(), QStringLiteral("1990-05-12"));
    EXPECT_EQ(QApplication::focusWidget(), edit.monthCombo());
    EXPECT_EQ(edit.yearCombo()->count(), 101);

    edit.setIsoDate(QStringLiteral("1890-05-12"));
    EXPECT_EQ(edit.yearCombo()->currentText(), QStringLiteral("1890"));
    EXPECT_GE(edit.yearCombo()->findText(QStringLiteral("1890")), 0);
    EXPECT_EQ(edit.isoDate(), QStringLiteral("1890-05-12"));

    edit.setIsoDate(QStringLiteral("not a date"));
    EXPECT_EQ(edit.dayCombo()->currentIndex(), -1);
    EXPECT_EQ(edit.monthCombo()->currentIndex(), -1);
    EXPECT_EQ(edit.yearCombo()->currentIndex(), -1);
    EXPECT_TRUE(edit.isoDate().isEmpty());
    EXPECT_EQ(QApplication::focusWidget(), edit.monthCombo());
}

TEST(test_ui_BirthDateEdit, UnderARightToLeftParentTheDayIsOnTheRight)
{
    QWidget window;
    window.setLayoutDirection(Qt::RightToLeft);
    auto* layout = new QVBoxLayout(&window);
    auto* edit = new BirthDateEdit(&window);
    layout->addWidget(edit);
    showWindow(window);

    EXPECT_EQ(edit->layoutDirection(), Qt::RightToLeft);
    const int dayX = edit->dayCombo()->mapTo(&window, QPoint(0, 0)).x();
    const int yearX = edit->yearCombo()->mapTo(&window, QPoint(0, 0)).x();
    EXPECT_GT(dayX, yearX);
}

TEST(test_ui_BirthDateEdit, ThePopupScrollbarStaysOnTheRightAndShowsTheSelection)
{
    QWidget window;
    window.setLayoutDirection(Qt::RightToLeft);
    auto* layout = new QVBoxLayout(&window);
    auto* edit = new BirthDateEdit(&window);
    layout->addWidget(edit);
    showWindow(window);

    edit->yearCombo()->setCurrentText(QStringLiteral("2016"));
    edit->yearCombo()->showPopup();
    QApplication::processEvents();
    QAbstractItemView* view = edit->yearCombo()->view();
    ASSERT_NE(view, nullptr);
    QWidget* popup = view->window();
    ASSERT_NE(popup, nullptr);
    QScrollBar* bar = view->verticalScrollBar();
    ASSERT_NE(bar, nullptr);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    const QRect barGlobal(bar->mapToGlobal(QPoint(0, 0)), bar->size());
    const QRect popupGlobal(popup->mapToGlobal(QPoint(0, 0)), popup->size());
    EXPECT_TRUE(popupGlobal.contains(barGlobal));
    EXPECT_GT(barGlobal.center().x(), view->viewport()->mapToGlobal(view->viewport()->rect().center()).x());
    const QRect visual = view->visualRect(view->model()->index(edit->yearCombo()->currentIndex(), 0));
    EXPECT_TRUE(view->viewport()->rect().intersects(visual));
    EXPECT_EQ(edit->yearCombo()->currentText(), QStringLiteral("2016"));
    edit->yearCombo()->hidePopup();
}

namespace {

int gapBetween(const QWidget* left, const QWidget* right)
{
    const int leftEdge = std::min(left->x(), right->x());
    const int rightEdge = leftEdge == left->x() ? left->x() + left->width() : right->x() + right->width();
    const int next = leftEdge == left->x() ? right->x() : left->x();
    return next - rightEdge;
}

void applyAppTheme()
{
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        QApplication::setStyle(fusion);
    }
    VLMS::setupFonts(*qApp);
    qApp->setPalette(VLMS::applicationPalette());
    qApp->setStyleSheet(VLMS::applicationStylesheet());
}

}  // namespace

TEST(test_ui_BirthDateEdit, BoxesStayApartInBothDirections)
{
    applyAppTheme();
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        QWidget window;
        window.setLayoutDirection(direction);
        auto* edit = new BirthDateEdit(&window);
        auto* layout = new QVBoxLayout(&window);
        layout->addWidget(edit);
        window.resize(900, 200);
        showWindow(window);

        const QRect day = edit->dayCombo()->geometry();
        const QRect month = edit->monthCombo()->geometry();
        const QRect year = edit->yearCombo()->geometry();
        EXPECT_FALSE(day.intersects(month));
        EXPECT_FALSE(month.intersects(year));
        EXPECT_FALSE(day.intersects(year));
        EXPECT_GE(gapBetween(edit->dayCombo(), edit->monthCombo()), 8);
        EXPECT_GE(gapBetween(edit->monthCombo(), edit->yearCombo()), 8);
        EXPECT_EQ(edit->dayCombo()->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
        EXPECT_LE(edit->dayCombo()->height(), edit->dayCombo()->sizeHint().height() + 2);
        EXPECT_GE(edit->dayCombo()->width(), edit->dayCombo()->sizeHint().width());
        EXPECT_GE(edit->yearCombo()->width(), edit->yearCombo()->sizeHint().width());
        EXPECT_GE(edit->width(), day.width() + month.width() + year.width() + 16);
        EXPECT_TRUE(day.intersected(month).isEmpty());
        EXPECT_TRUE(month.intersected(year).isEmpty());
    }
}

TEST(test_ui_BirthDateEdit, PopupIsFiveRowsAndScrollsToTheYear)
{
    applyAppTheme();
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        QWidget window;
        window.setLayoutDirection(direction);
        auto* edit = new BirthDateEdit(&window);
        auto* layout = new QVBoxLayout(&window);
        layout->addWidget(edit);
        showWindow(window);

        QComboBox* year = edit->yearCombo();
        year->setCurrentText(QStringLiteral("1990"));
        year->showPopup();
        QApplication::processEvents();

        QAbstractItemView* view = year->view();
        ASSERT_NE(view, nullptr);
        QWidget* popup = view->window();
        ASSERT_NE(popup, nullptr);
        ASSERT_NE(popup, &window);
        const int rowHeight = view->visualRect(view->model()->index(0, 0)).height();
        ASSERT_GT(rowHeight, 0);
        EXPECT_EQ(year->maxVisibleItems(), 5);
        EXPECT_LE(popup->height(), rowHeight * 5 + 48);
        EXPECT_GE(popup->height(), rowHeight * 4);
        EXPECT_LE(view->viewport()->height(), rowHeight * 5 + rowHeight / 2);
        EXPECT_GE(view->viewport()->height(), rowHeight * 4);
        const QRect visual = view->visualRect(view->model()->index(year->currentIndex(), 0));
        EXPECT_TRUE(view->viewport()->rect().intersects(visual));
        EXPECT_GT(view->verticalScrollBar()->maximum(), view->verticalScrollBar()->minimum());

        const QPoint comboAt = year->mapToGlobal(QPoint(0, 0));
        const QPoint popupAt = popup->mapToGlobal(QPoint(0, 0));
        const int alignedX = direction == Qt::RightToLeft ? comboAt.x() + year->width() - popup->width()
                                                           : comboAt.x();
        EXPECT_NEAR(popupAt.x(), alignedX, 4);
        const int under = comboAt.y() + year->height();
        const int above = comboAt.y() - popup->height();
        EXPECT_TRUE(std::abs(popupAt.y() - under) <= 4 || std::abs(popupAt.y() - above) <= 4);
        year->hidePopup();
    }
}
