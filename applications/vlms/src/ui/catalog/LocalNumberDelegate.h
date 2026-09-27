#pragma once

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
    /// Item role holding the row's numbers as a QStringList.
    static constexpr int kNumbersRole = Qt::UserRole + 1;

    /// Item role holding the one number the cell shows, as a QString: the copy
    /// the librarian picked from the drop-down, or the one a number search
    /// matched. Empty, or absent from kNumbersRole, means the first number.
    static constexpr int kSelectedRole = Qt::UserRole + 2;

    /// Item role holding the subset of kNumbersRole whose copy is out on an
    /// unreturned loan, as a QStringList. A subset rather than a flag per
    /// number: the numbers are unique within a book, so membership answers the
    /// question and there is no index to keep aligned with kNumbersRole.
    /// Absent or empty means every copy is on the shelf.
    static constexpr int kOnLoanRole = Qt::UserRole + 3;

    explicit LocalNumberDelegate(QObject* parent = nullptr);

    /// What the closed cell reads: an em dash for none, the number itself for
    /// one, and "<selected> (+<rest>)" for more — where the selected number is
    /// `selected` when the row holds it, and the first otherwise.
    [[nodiscard]] static QString cellText(const QStringList& numbers,
                                          const QString& selected = {});

    /// The part of `cellText` that names one copy, and so the only part
    /// coloured by that copy's loan state: the em dash for none, otherwise the
    /// selected number when the row holds it and the first otherwise.
    [[nodiscard]] static QString leadNumber(const QStringList& numbers,
                                            const QString& selected = {});

    /// The rest of `cellText` — " (+<n>)" when the book has more copies than
    /// the one named, empty otherwise. Stays in the table's own text colour:
    /// it counts copies whose states are not the lead's to speak for.
    [[nodiscard]] static QString restSuffix(const QStringList& numbers);

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
