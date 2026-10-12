#pragma once

#include "ui/presentation/LocalNumberCell.h"

#include <QColor>
#include <QPointer>
#include <QStringList>
#include <QStyledItemDelegate>

class QComboBox;
class QPainter;
class QWidget;

/// Paints a catalogue cell that looks like a closed combo box over a book's copy
/// local numbers, and opens a real one only when the cell is clicked.
///
/// The catalogue list opens on ALL — 18,468 rows — and is repopulated on every
/// search keystroke, so a QComboBox per row is out of the question. The numbers
/// live in the item's data under kNumbersRole; at most one combo widget exists
/// at a time, and picking a number records it under kSelectedRole so the closed
/// cell shows that copy.
class LocalNumberDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    /// Roles and closed-cell text live in presentation (`LocalNumberCell`);
    /// aliases keep existing call sites compiling.
    static constexpr int kNumbersRole = VLMS::LocalNumberCell::kNumbersRole;
    static constexpr int kSelectedRole = VLMS::LocalNumberCell::kSelectedRole;
    static constexpr int kOnLoanRole = VLMS::LocalNumberCell::kOnLoanRole;

    explicit LocalNumberDelegate(QObject* parent = nullptr);

    [[nodiscard]] static QString cellText(const QStringList& numbers,
                                          const QString& selected = {})
    {
        return VLMS::LocalNumberCell::cellText(numbers, selected);
    }

    [[nodiscard]] static QString leadNumber(const QStringList& numbers,
                                            const QString& selected = {})
    {
        return VLMS::LocalNumberCell::leadNumber(numbers, selected);
    }

    [[nodiscard]] static QString restSuffix(const QStringList& numbers)
    {
        return VLMS::LocalNumberCell::restSuffix(numbers);
    }

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

protected:
    bool editorEvent(QEvent* event,
                     QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

    /// Watches the open popup so the combo dies with it.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] static QStringList numbersOf(const QModelIndex& index);
    [[nodiscard]] static QString selectedOf(const QModelIndex& index);
    [[nodiscard]] static QStringList onLoanOf(const QModelIndex& index);

    /// Draws the lead number in its loan-state colour — italic when the copy
    /// is out, so the state survives a colour-blind reader and a greyscale
    /// print — followed by the neutral " (+<n>)" in `neutral`. Lays the two
    /// runs out from the leading edge of `rect`, mirrored in RTL.
    static void drawNumberRuns(QPainter* painter,
                               const QRectF& rect,
                               Qt::LayoutDirection direction,
                               const QStringList& numbers,
                               const QString& selected,
                               const QStringList& onLoan,
                               const QColor& neutral);

    /// Drops the event filter installed for the currently open combo's popup
    /// window (if any) and forgets it. Safe to call with nothing open.
    void teardownOpenCombo();

    /// The one combo that may exist at a time. QPointer so a combo destroyed by
    /// its own deleteLater leaves nothing dangling here.
    QPointer<QComboBox> m_openCombo;

    /// The popup window `eventFilter` was installed on for `m_openCombo`, so a
    /// `QEvent::Hide` from an unrelated watched object -- or one belonging to
    /// a popup already torn down -- is never mistaken for this combo closing.
    QPointer<QWidget> m_openComboWindow;
};
