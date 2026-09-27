#include "ui/catalog/FreeLocalNumberDelegate.h"

#include <QComboBox>
#include <QPainter>
#include <QStringList>
#include <QStyle>
#include <QStyleOptionButton>

FreeLocalNumberDelegate::FreeLocalNumberDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void FreeLocalNumberDelegate::paint(QPainter* painter,
                                    const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    if ((opt.features & QStyleOptionViewItem::HasCheckIndicator) == 0 || opt.widget == nullptr) {
        return;
    }
    QStyleOptionButton box;
    box.rect = opt.widget->style()->subElementRect(
        QStyle::SE_ItemViewItemCheckIndicator, &opt, opt.widget);
    box.state = QStyle::State_Enabled;
    if (opt.checkState == Qt::Checked) {
        box.state |= QStyle::State_On;
    } else if (opt.checkState == Qt::PartiallyChecked) {
        box.state |= QStyle::State_NoChange;
    } else {
        box.state |= QStyle::State_Off;
    }
    opt.widget->style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &box, painter, opt.widget);
}

QWidget* FreeLocalNumberDelegate::createEditor(QWidget* parent,
                                               const QStyleOptionViewItem& option,
                                               const QModelIndex& index) const
{
    const QStringList numbers = index.data(kFreeNumbersRole).toStringList();
    if (numbers.isEmpty()) {
        return QStyledItemDelegate::createEditor(parent, option, index);
    }

    auto* combo = new QComboBox(parent);
    // Editable: a number typed by hand still works, and is validated by the
    // save exactly as it was before this drop-down existed.
    combo->setEditable(true);
    combo->addItems(numbers);
    combo->setCurrentIndex(0);
    return combo;
}

void FreeLocalNumberDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (combo == nullptr) {
        QStyledItemDelegate::setEditorData(editor, index);
        return;
    }
    combo->setCurrentText(index.data(Qt::EditRole).toString());
}

void FreeLocalNumberDelegate::setModelData(QWidget* editor,
                                           QAbstractItemModel* model,
                                           const QModelIndex& index) const
{
    auto* combo = qobject_cast<QComboBox*>(editor);
    if (combo == nullptr) {
        QStyledItemDelegate::setModelData(editor, model, index);
        return;
    }
    model->setData(index, combo->currentText().trimmed(), Qt::EditRole);
}
