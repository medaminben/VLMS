#pragma once

#include <QStyledItemDelegate>

class QPainter;

/// Editor for a **new** copy row's local number. The cell opens on the next
/// incremented number; the stock's free numbers sit behind it in an editable
/// combo, so reusing a gap is a deliberate pick and never a default. Rows
/// loaded from the database carry no numbers in the role and fall back to the
/// plain line edit -- changing an existing copy's number is a different act.
class FreeLocalNumberDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    /// QStringList: the number the cell opens on, then the free ones. Roles
    /// +1..+3 are already taken by the copies table.
    static constexpr int kFreeNumbersRole = Qt::UserRole + 4;

    explicit FreeLocalNumberDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

    [[nodiscard]] QWidget* createEditor(QWidget* parent,
                                        const QStyleOptionViewItem& option,
                                        const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor,
                      QAbstractItemModel* model,
                      const QModelIndex& index) const override;
};
