#include "ui/members/BirthDateEdit.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QRect>
#include <QScreen>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QStyleOptionComboBox>

#include <algorithm>

namespace {

constexpr int kBoxSpacing = 8;

int editFieldWidth(const QComboBox* box, int width)
{
    QStyleOptionComboBox option;
    option.initFrom(box);
    option.rect = QRect(0, 0, width, box->sizeHint().height());
    option.editable = false;
    option.subControls = QStyle::SC_All;
    return box->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, box).width();
}

// sizeHint is no guide here: under the application stylesheet it counts the
// drop-down twice (once in padding-right, once as ::drop-down) and sizes for
// the widest item plus QComboBox's own slack, which made each box three times
// wider than "00" and pushed the row over the Sex label. Ask the style where
// the text goes instead, and add only what the frame, padding and arrow take.
void setFixedWidthForSample(QComboBox* box, const QString& sample)
{
    box->ensurePolished();
    constexpr int kProbeWidth = 200;
    const int overhead = kProbeWidth - editFieldWidth(box, kProbeWidth);
    const int text = box->fontMetrics().horizontalAdvance(sample);
    box->setFixedWidth(std::max(overhead + text + 2, box->sizeHint().width()));
    box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

}  // namespace

class BirthDateEdit::Combo final : public QComboBox {
public:
    explicit Combo(QWidget* parent)
        : QComboBox(parent)
    {
    }

    void showPopup() override
    {
        // Fusion plus the application stylesheet makes QComboBox::showPopup
        // size the container to the screen and pin its top to the screen edge.
        // Clamping the height afterwards keeps that origin, so the short list
        // sits at the top of the screen instead of on the combo.
        forceLeftToRight();
        QComboBox::showPopup();
        forceLeftToRight();
        clampToFiveRows();
        placeNextToCombo();
        scrollSelectionIntoView();
    }

private:
    void forceLeftToRight()
    {
        view()->setLayoutDirection(Qt::LeftToRight);
        view()->window()->setLayoutDirection(Qt::LeftToRight);
    }

    void clampToFiveRows()
    {
        QAbstractItemView* list = view();
        const int rows = std::min(maxVisibleItems(), count());
        if (rows <= 0) {
            return;
        }
        int rowHeight = list->sizeHintForRow(0);
        if (rowHeight <= 0) {
            rowHeight = list->visualRect(model()->index(0, 0)).height();
        }
        if (rowHeight <= 0) {
            return;
        }
        QWidget* popup = list->window();
        const int frame = std::max(0, popup->height() - list->height());
        const int listHeight = rowHeight * rows;
        list->setMaximumHeight(listHeight);
        popup->resize(popup->width(), listHeight + frame);
        if (popup->layout() != nullptr) {
            popup->layout()->activate();
        }
    }

    void placeNextToCombo()
    {
        QWidget* popup = view()->window();
        const QPoint topLeft = mapToGlobal(QPoint(0, 0));
        const QRect combo(topLeft, size());
        const QRect screen = this->screen() != nullptr ? this->screen()->availableGeometry()
                                                        : QGuiApplication::primaryScreen()->availableGeometry();

        const QWidget* directionSource = parentWidget() != nullptr ? parentWidget() : this;
        const bool rtl = directionSource->layoutDirection() == Qt::RightToLeft;
        int x = rtl ? combo.right() - popup->width() + 1 : combo.left();
        int y = combo.bottom() + 1;
        if (y + popup->height() - 1 > screen.bottom()) {
            y = combo.top() - popup->height();
        }
        x = std::clamp(x, screen.left(), std::max(screen.left(), screen.right() - popup->width() + 1));
        y = std::clamp(y, screen.top(), std::max(screen.top(), screen.bottom() - popup->height() + 1));
        popup->move(x, y);
    }

    void scrollSelectionIntoView()
    {
        QAbstractItemView* list = view();
        if (currentIndex() >= 0) {
            list->scrollTo(model()->index(currentIndex(), modelColumn()), QAbstractItemView::EnsureVisible);
        } else {
            list->scrollToTop();
        }
    }
};

BirthDateEdit::BirthDateEdit(QWidget* parent)
    : QWidget(parent)
{
    m_day = new Combo(this);
    m_month = new Combo(this);
    m_year = new Combo(this);

    for (QComboBox* box : {m_day, m_month, m_year}) {
        box->setObjectName(QStringLiteral("birthDatePart"));
        box->setLayoutDirection(Qt::LeftToRight);
        box->setMaxVisibleItems(5);
        box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        box->setEditable(false);
    }

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(kBoxSpacing);
    layout->addWidget(m_day);
    layout->addWidget(m_month);
    layout->addWidget(m_year);

    fillCanonical();
    setFixedWidthForSample(m_day, QStringLiteral("00"));
    setFixedWidthForSample(m_month, QStringLiteral("00"));
    setFixedWidthForSample(m_year, QStringLiteral("0000"));
    const int rowWidth = m_day->width() + m_month->width() + m_year->width() + 2 * kBoxSpacing;
    setFixedWidth(rowWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QComboBox* BirthDateEdit::dayCombo() const
{
    return m_day;
}

QComboBox* BirthDateEdit::monthCombo() const
{
    return m_month;
}

QComboBox* BirthDateEdit::yearCombo() const
{
    return m_year;
}

void BirthDateEdit::fillCanonical()
{
    const QSignalBlocker blockDay(m_day);
    const QSignalBlocker blockMonth(m_month);
    const QSignalBlocker blockYear(m_year);

    auto refill = [](QComboBox* box, int first, int last, int pad) {
        box->clear();
        for (int value = first; value <= last; ++value) {
            box->addItem(pad > 0 ? QString::number(value).rightJustified(pad, u'0')
                                 : QString::number(value));
        }
        box->setCurrentIndex(-1);
    };

    refill(m_day, 1, 31, 2);
    refill(m_month, 1, 12, 2);
    refill(m_year, 1916, 2016, 0);
}

bool BirthDateEdit::allDigits(const QString& text)
{
    if (text.isEmpty()) {
        return false;
    }
    for (const QChar ch : text) {
        if (ch < u'0' || ch > u'9') {
            return false;
        }
    }
    return true;
}

void BirthDateEdit::selectOrInsert(QComboBox* box, const QString& text)
{
    const QSignalBlocker blocker(box);
    if (!allDigits(text)) {
        box->setCurrentIndex(-1);
        return;
    }

    const int existing = box->findText(text);
    if (existing >= 0) {
        box->setCurrentIndex(existing);
        return;
    }

    const int numeric = text.toInt();
    int insertAt = box->count();
    for (int i = 0; i < box->count(); ++i) {
        if (box->itemText(i).toInt() > numeric) {
            insertAt = i;
            break;
        }
    }
    box->insertItem(insertAt, text);
    box->setCurrentIndex(insertAt);
}

void BirthDateEdit::setIsoDate(const QString& iso)
{
    const QSignalBlocker blockDay(m_day);
    const QSignalBlocker blockMonth(m_month);
    const QSignalBlocker blockYear(m_year);

    fillCanonical();
    const QStringList parts = iso.split(QLatin1Char('-'));
    if (parts.size() != 3) {
        selectOrInsert(m_day, QString());
        selectOrInsert(m_month, QString());
        selectOrInsert(m_year, QString());
        return;
    }
    selectOrInsert(m_year, parts.at(0));
    selectOrInsert(m_month, parts.at(1));
    selectOrInsert(m_day, parts.at(2));
}

QString BirthDateEdit::isoDate() const
{
    if (m_day->currentIndex() < 0 || m_month->currentIndex() < 0 || m_year->currentIndex() < 0) {
        return {};
    }
    return m_year->currentText() + QLatin1Char('-') + m_month->currentText() + QLatin1Char('-')
        + m_day->currentText();
}
