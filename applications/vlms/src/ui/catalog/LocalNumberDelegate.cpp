#include "ui/catalog/LocalNumberDelegate.h"

#include "ui/Theme.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QApplication>
#include <QBrush>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QStyleOptionViewItem>
#include <QWidget>

namespace {

/// Shown when a book has no copies at all. 1,749 of the live catalogue's rows.
const QString kNoNumbers = QString::fromUtf8("—");

/// Frame margins plus the drop-down arrow, added to the text width so the
/// closed combo has room to draw both without clipping the number(s).
constexpr int kComboChromeWidth = 36;

/// Width of the arrow's own box on the right of the chrome, mirrored to the
/// left in RTL. Matches roughly what QComboBox::drop-down reserves in the
/// stylesheet (24px) plus a little breathing room.
constexpr int kArrowBoxWidth = 20;

/// Paints the open drop-down's entries by hand, for the same reason the closed
/// cell is painted by hand.
///
/// Setting Qt::ForegroundRole on the items is not enough: the theme's
/// "QComboBox QAbstractItemView { color: ... }" rule sends the popup's items
/// through QStyleSheetStyle::drawControl, which reconfigures the option's
/// palette from the sheet and so overwrites the brush the item carries. Every
/// entry then renders in the sheet's own text colour, and the list says
/// nothing about which copies are out. The roles are still set -- they are
/// what this delegate reads -- but the drawing has to be ours.
///
/// The state colour is kept under the highlight too, exactly as the closed
/// cell keeps it under a selected row: both pairs are picked to clear the
/// selection background in their mode.
class ComboItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem entry = option;
        initStyleOption(&entry, index);
        const QString text = entry.text;
        entry.text.clear();
        QStyle* style = entry.widget != nullptr ? entry.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &entry, painter, entry.widget);

        painter->save();
        const QVariant font = index.data(Qt::FontRole);
        painter->setFont(font.isValid() ? font.value<QFont>() : entry.font);
        const QVariant ink = index.data(Qt::ForegroundRole);
        painter->setPen(ink.canConvert<QBrush>()
                            ? ink.value<QBrush>().color()
                            : entry.palette.color(entry.state.testFlag(QStyle::State_Selected)
                                                      ? QPalette::HighlightedText
                                                      : QPalette::Text));
        painter->drawText(style->subElementRect(QStyle::SE_ItemViewItemText, &entry, entry.widget),
                          Qt::AlignVCenter | Qt::AlignLeading,
                          text);
        painter->restore();
    }
};

}  // namespace

LocalNumberDelegate::LocalNumberDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QStringList LocalNumberDelegate::numbersOf(const QModelIndex& index)
{
    return index.data(kNumbersRole).toStringList();
}

QString LocalNumberDelegate::selectedOf(const QModelIndex& index)
{
    return index.data(kSelectedRole).toString();
}

QStringList LocalNumberDelegate::onLoanOf(const QModelIndex& index)
{
    return index.data(kOnLoanRole).toStringList();
}

QString LocalNumberDelegate::leadNumber(const QStringList& numbers, const QString& selected)
{
    if (numbers.isEmpty()) {
        return kNoNumbers;
    }
    // A row whose selected number is not one of its own — a stale pick left on a
    // reused item, say — falls back to the first rather than showing a number
    // this book does not hold.
    return numbers.contains(selected) ? selected : numbers.first();
}

QString LocalNumberDelegate::restSuffix(const QStringList& numbers)
{
    if (numbers.size() < 2) {
        return {};
    }
    // Same untranslated " (+%1)" shape the filter-list counts already use.
    return QStringLiteral(" (+%1)").arg(numbers.size() - 1);
}

QString LocalNumberDelegate::cellText(const QStringList& numbers, const QString& selected)
{
    return leadNumber(numbers, selected) + restSuffix(numbers);
}

void LocalNumberDelegate::drawNumberRuns(QPainter* painter,
                                         const QRectF& rect,
                                         const Qt::LayoutDirection direction,
                                         const QStringList& numbers,
                                         const QString& selected,
                                         const QStringList& onLoan,
                                         const QColor& neutral)
{
    const QString lead = leadNumber(numbers, selected);
    const QString rest = restSuffix(numbers);

    QFont leadFont = painter->font();
    QColor leadColour = neutral;
    if (!numbers.isEmpty()) {
        // The em dash names no copy, so it has no state to report and keeps the
        // table's own colour.
        const bool out = onLoan.contains(lead);
        leadColour = out ? VLMS::copyOnLoanColor() : VLMS::copyAvailableColor();
        leadFont.setItalic(out);
    }

    // Laid out as two runs rather than one string so the colour break lands
    // exactly at the end of the number in either direction: a single bidi run
    // would reorder "505 (+12)" under an RTL paragraph and leave the tint on
    // whatever glyphs happened to come first.
    const int leadWidth = QFontMetrics(leadFont).horizontalAdvance(lead);
    QRectF leadRect = rect;
    QRectF restRect = rect;
    if (direction == Qt::RightToLeft) {
        leadRect.setLeft(rect.right() - leadWidth);
        restRect.setRight(leadRect.left());
    } else {
        leadRect.setRight(rect.left() + leadWidth);
        restRect.setLeft(leadRect.right());
    }

    const QFont restFont = painter->font();
    painter->setFont(leadFont);
    painter->setPen(leadColour);
    painter->drawText(leadRect,
                      Qt::AlignVCenter | (direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft),
                      lead);
    if (!rest.isEmpty()) {
        painter->setFont(restFont);
        painter->setPen(neutral);
        painter->drawText(restRect,
                          Qt::AlignVCenter
                              | (direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft),
                          rest);
    }
    painter->setFont(restFont);
}

void LocalNumberDelegate::paint(QPainter* painter,
                                const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    const QStringList numbers = numbersOf(index);

    QStyleOptionViewItem background = option;
    initStyleOption(&background, index);
    background.text.clear();
    QStyle* style = background.widget != nullptr ? background.widget->style()
                                                 : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &background, painter, background.widget);

    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const QColor textColor = option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text);

    if (numbers.size() < 2) {
        // One number or none: plain text, no frame, no arrow — there is nothing
        // to drop down to.
        painter->save();
        drawNumberRuns(painter,
                       QRectF(option.rect.adjusted(4, 0, -4, 0)),
                       option.direction,
                       numbers,
                       selectedOf(index),
                       onLoanOf(index),
                       textColor);
        painter->restore();
        return;
    }

    // Deliberately not QStyle::CC_ComboBox: that control is drawn against
    // background.widget (the table view, not a real QComboBox), so the
    // theme's "QComboBox { ... }" stylesheet rules -- border radius, the
    // pre-tinted drop-down arrow, the drop-down's own background -- never
    // match it, and the active style is left to fall back to whatever
    // default chrome it draws for an unstyled complex control. That default
    // is style-dependent and not guaranteed to read the roles this item view
    // already themes correctly. Painting the chrome by hand with exactly
    // those roles (the same ones CE_ItemViewItem above just used) keeps the
    // cell reading as part of the table in both themes, on every style.
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF chromeRect = QRectF(option.rect.adjusted(2, 2, -2, -2)).adjusted(0.5, 0.5, -0.5, -0.5);
    if (selected) {
        // The selection background is already painted by CE_ItemViewItem
        // above; only a faint outline in the highlighted-text colour marks
        // the chrome, so the fill does not double up on top of it.
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(textColor, 1));
    } else {
        painter->setBrush(option.palette.color(QPalette::Button));
        painter->setPen(QPen(option.palette.color(QPalette::Mid), 1));
    }
    painter->drawRoundedRect(chromeRect, 4, 4);

    // Drop-down arrow, right-aligned (left in RTL), matching the direction
    // the real QComboBox in the pager below draws its own arrow.
    QRectF arrowBox = chromeRect;
    if (option.direction == Qt::RightToLeft) {
        arrowBox.setWidth(kArrowBoxWidth);
    } else {
        arrowBox.setLeft(chromeRect.right() - kArrowBoxWidth);
    }
    const QPointF arrowCenter = arrowBox.center();
    QPolygonF arrow;
    arrow << QPointF(arrowCenter.x() - 4, arrowCenter.y() - 2)
          << QPointF(arrowCenter.x() + 4, arrowCenter.y() - 2)
          << QPointF(arrowCenter.x(), arrowCenter.y() + 3);
    painter->setPen(Qt::NoPen);
    painter->setBrush(textColor);
    painter->drawPolygon(arrow);

    // The number(s), left-aligned (right in RTL) between the frame and the
    // arrow box.
    QRectF textRect = chromeRect;
    if (option.direction == Qt::RightToLeft) {
        textRect.setLeft(arrowBox.right() + 2);
        textRect.adjust(0, 0, -6, 0);
    } else {
        textRect.setRight(arrowBox.left() - 2);
        textRect.adjust(6, 0, 0, 0);
    }
    drawNumberRuns(painter,
                   textRect,
                   option.direction,
                   numbers,
                   selectedOf(index),
                   onLoanOf(index),
                   textColor);

    painter->restore();
}

QSize LocalNumberDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const QStringList numbers = numbersOf(index);
    // The lead is measured italic whether or not the copy is out, so the column
    // keeps one width as loans come and go over the day.
    QFont italic = option.font;
    italic.setItalic(true);
    size.setWidth(
        QFontMetrics(italic).horizontalAdvance(leadNumber(numbers, selectedOf(index)))
        + option.fontMetrics.horizontalAdvance(restSuffix(numbers))
        + kComboChromeWidth);
    return size;
}

bool LocalNumberDelegate::editorEvent(QEvent* event,
                                      QAbstractItemModel* model,
                                      const QStyleOptionViewItem& option,
                                      const QModelIndex& index)
{
    if (event->type() != QEvent::MouseButtonRelease) {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    const QStringList numbers = numbersOf(index);
    if (numbers.size() < 2) {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != Qt::LeftButton || !option.rect.contains(mouse->position().toPoint())) {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }

    auto* view = qobject_cast<QAbstractItemView*>(const_cast<QWidget*>(option.widget));
    QWidget* host = view != nullptr ? view->viewport() : const_cast<QWidget*>(option.widget);
    if (host == nullptr) {
        return true;
    }

    // One combo at a time, alive only while its popup is. What it writes back is
    // the pick alone (kSelectedRole): the numbers themselves belong to the book
    // editor, and stay in accession order for the next drop-down.
    teardownOpenCombo();
    auto* combo = new QComboBox(host);
    combo->addItems(numbers);
    // The same red/green the closed cell uses, on every entry, so the list can
    // be read for what is in before a number is picked. ComboItemDelegate is
    // what puts these roles on screen; the theme's sheet would otherwise paint
    // over them.
    combo->setItemDelegate(new ComboItemDelegate(combo));
    const QStringList onLoan = onLoanOf(index);
    QFont outFont = combo->font();
    outFont.setItalic(true);
    for (int item = 0; item < numbers.size(); ++item) {
        const bool out = onLoan.contains(numbers.at(item));
        combo->setItemData(item,
                           QBrush(out ? VLMS::copyOnLoanColor()
                                      : VLMS::copyAvailableColor()),
                           Qt::ForegroundRole);
        if (out) {
            combo->setItemData(item, outFont, Qt::FontRole);
        }
    }
    if (const int current = numbers.indexOf(selectedOf(index)); current >= 0) {
        combo->setCurrentIndex(current);
    }
    // Connected only once the combo holds the row's current pick, so filling it
    // is never mistaken for the librarian making one.
    connect(combo,
            &QComboBox::currentIndexChanged,
            this,
            [model, row = QPersistentModelIndex(index), numbers](const int picked) {
                if (row.isValid() && picked >= 0 && picked < numbers.size()) {
                    model->setData(row, numbers.at(picked), kSelectedRole);
                }
            });
    combo->setGeometry(option.rect);
    combo->show();
    m_openCombo = combo;
    // The popup is a separate window; when it hides, the combo has done its
    // job. Remember which window so eventFilter can tell this combo's own
    // Hide apart from one belonging to an unrelated watched object.
    m_openComboWindow = combo->view()->window();
    m_openComboWindow->installEventFilter(this);
    combo->showPopup();
    return true;
}

bool LocalNumberDelegate::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Hide && watched == m_openComboWindow && !m_openComboWindow.isNull()) {
        teardownOpenCombo();
    }
    return QStyledItemDelegate::eventFilter(watched, event);
}

void LocalNumberDelegate::teardownOpenCombo()
{
    if (!m_openComboWindow.isNull()) {
        m_openComboWindow->removeEventFilter(this);
        m_openComboWindow.clear();
    }
    if (!m_openCombo.isNull()) {
        m_openCombo->deleteLater();
        m_openCombo.clear();
    }
}
