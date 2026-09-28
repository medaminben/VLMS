#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <VLMS/Core/Strings.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLayoutItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStyle>
#include <QItemSelectionModel>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>

#include <functional>
#include <utility>

using VLMS::T;
using VLMS::cd;
using VLMS::qd;
using VLMS::qs;
using VLMS::qsl;
using VLMS::ss;
using VLMS::svl;

namespace VLMS {

namespace {

constexpr int kDetailScrollableMaxHeight = 96;
constexpr int kDetailScrollableMinHeight = 48;

/// One Qt standard button and the string-table key that replaces its label.
struct StandardButtonLabel {
    QDialogButtonBox::StandardButton button;
    const char* key;
};

// Only the buttons this application actually raises. Anything absent keeps
// Qt's own label, which is the right answer for a button nobody translated.
constexpr StandardButtonLabel kStandardButtonLabels[] = {
    {QDialogButtonBox::Ok, "common.ok"},
    {QDialogButtonBox::Cancel, "common.cancel"},
    {QDialogButtonBox::Yes, "common.yes"},
    {QDialogButtonBox::No, "common.no"},
    {QDialogButtonBox::Close, "common.close"},
};

void relabelStandardButtons(QDialogButtonBox* box)
{
    for (const StandardButtonLabel& entry : kStandardButtonLabels) {
        if (QPushButton* button = box->button(entry.button)) {
            button->setText(T(entry.key));
        }
    }
}

// Qt answers QEvent::LanguageChange by relabelling a button box's standard
// buttons from its own catalogue, over ours: Qt's Arabic "OK" is not the one
// this application uses, and with no catalogue loaded it is plain English. Qt
// sends one to the first dialog a process shows, so the first warning after
// start-up was the one that changed. The box never needs Qt's labels, so the
// event stops here and ours go back on, in whatever the language now is.
class ButtonLabelKeeper final : public QObject {
public:
    ButtonLabelKeeper(QDialogButtonBox* box, std::function<void()> relabel)
        : QObject(box),
          m_relabel(std::move(relabel))
    {
        setObjectName(QString::fromLatin1(kObjectName));
        box->installEventFilter(this);
    }

    static constexpr const char* kObjectName = "buttonLabelKeeper";

protected:
    bool eventFilter(QObject* /*watched*/, QEvent* event) override
    {
        if (event->type() != QEvent::LanguageChange) {
            return false;
        }
        m_relabel();
        return true;
    }

private:
    std::function<void()> m_relabel;
};

// Idempotent: retranslateUi relocalizes the same button box on every switch.
void keepButtonLabels(QDialogButtonBox* box, std::function<void()> relabel)
{
    if (box != nullptr
        && box->findChild<QObject*>(QString::fromLatin1(ButtonLabelKeeper::kObjectName),
                                    Qt::FindDirectChildrenOnly)
               == nullptr) {
        new ButtonLabelKeeper(box, std::move(relabel));
    }
}

void keepStandardButtonLabels(QDialogButtonBox* box)
{
    keepButtonLabels(box, [box] { relabelStandardButtons(box); });
}

QMessageBox::StandardButton runMessageBox(QWidget* parent,
                                          QMessageBox::Icon icon,
                                          const QString& title,
                                          const QString& text,
                                          QMessageBox::StandardButtons buttons,
                                          QMessageBox::StandardButton defaultButton)
{
    QMessageBox box(icon, title, text, buttons, parent);
    box.setDefaultButton(defaultButton);
    localizeMessageBox(&box);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

}  // namespace

QPushButton* makeNavButton(const QString& text)
{
    auto* button = new QPushButton(text);
    button->setObjectName(QStringLiteral("navLink"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    return button;
}

QPushButton* makePrimaryButton(const QString& text)
{
    auto* button = new QPushButton(text);
    button->setObjectName(QStringLiteral("btnPrimary"));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QPushButton* makeSecondaryButton(const QString& text)
{
    auto* button = new QPushButton(text);
    button->setObjectName(QStringLiteral("btnSecondary"));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QPushButton* makeGhostButton(const QString& text)
{
    auto* button = new QPushButton(text);
    button->setObjectName(QStringLiteral("btnGhost"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFlat(true);
    return button;
}

QFrame* makeCard(QWidget* parent)
{
    auto* frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("card"));
    frame->setFrameShape(QFrame::StyledPanel);
    return frame;
}

QLabel* makePageTitle(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("pageTitle"));
    return label;
}

QLabel* makePageSubtitle(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("pageSubtitle"));
    label->setWordWrap(true);
    return label;
}

QWidget* makePageHeader(const QString& title, const QString& subtitle, QWidget* parent)
{
    auto* header = new QWidget(parent);
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    layout->addWidget(makePageTitle(title, header), 0, Qt::AlignVCenter);
    auto* subtitleLabel = makePageSubtitle(subtitle, header);
    subtitleLabel->setWordWrap(true);
    layout->addWidget(subtitleLabel, 1, Qt::AlignVCenter);
    return header;
}

void clearLayout(QLayout* layout)
{
    if (layout == nullptr) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void configurePageLayout(QVBoxLayout* layout)
{
    if (layout == nullptr) {
        return;
    }
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
}

void configureFormLayout(QFormLayout* form)
{
    if (form == nullptr) {
        return;
    }
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeading | Qt::AlignVCenter);
}

QWidget* makeFilterColumn(QWidget* parent)
{
    auto* column = new QWidget(parent);
    column->setFixedWidth(kFilterColumnWidth);
    column->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    return column;
}

void configureResizableColumns(QTableWidget* table, const QList<int>& widths)
{
    if (table == nullptr) {
        return;
    }
    QHeaderView* header = table->horizontalHeader();
    // Interactive is the only mode the user can drag; Stretch/ResizeToContents
    // lock the section width.
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(true);
    header->setCascadingSectionResizes(false);
    header->setMinimumSectionSize(60);
    for (int column = 0; column < widths.size() && column < table->columnCount(); ++column) {
        header->resizeSection(column, widths.at(column));
    }
}

void retranslateStandardButtons(QDialogButtonBox* buttons)
{
    localizeButtonBox(buttons);
}

void setIsoDateFormat(QDateEdit* edit)
{
    edit->setDisplayFormat(edit->layoutDirection() == Qt::RightToLeft
                               ? QStringLiteral("dd-MM-yyyy")
                               : QStringLiteral("yyyy-MM-dd"));
}

void selectTableRow(QTableView* table, const int row)
{
    QAbstractItemModel* model = table->model();
    if (model == nullptr || row < 0 || row >= model->rowCount()) {
        return;
    }
    table->selectionModel()->setCurrentIndex(
        model->index(row, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

void refreshWidgetStyle(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }
    if (QStyle* style = widget->style()) {
        style->unpolish(widget);
        style->polish(widget);
    }
    widget->update();
}

QWidget* makeDetailValueWidget(QWidget* parent, const bool scrollable)
{
    if (scrollable) {
        auto* edit = new QPlainTextEdit(parent);
        edit->setObjectName(QStringLiteral("bookDetailValue"));
        edit->setReadOnly(true);
        edit->setFrameShape(QFrame::NoFrame);
        edit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        edit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        edit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        edit->setMaximumHeight(kDetailScrollableMaxHeight);
        edit->setMinimumHeight(kDetailScrollableMinHeight);
        edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        edit->setTextInteractionFlags(Qt::TextSelectableByMouse);
        edit->setTabChangesFocus(false);
        return edit;
    }

    auto* label = new QLabel(parent);
    label->setObjectName(QStringLiteral("bookDetailValue"));
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignTop | Qt::AlignLeading);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

void setDetailValueText(QWidget* widget, const QString& text)
{
    if (widget == nullptr) {
        return;
    }
    if (auto* label = qobject_cast<QLabel*>(widget)) {
        label->setText(text);
        return;
    }
    if (auto* edit = qobject_cast<QPlainTextEdit*>(widget)) {
        edit->setPlainText(text);
    }
}

void clearDetailValueText(QWidget* widget)
{
    setDetailValueText(widget, {});
}

PreviewImageBounds bookCoverPreviewBounds()
{
    return PreviewImageBounds{
        QSize(220, 330),
        QSize(88, 132),
        2,
        3,
        140,
        16,
    };
}

PreviewImageBounds memberPhotoPreviewBounds()
{
    return PreviewImageBounds{
        QSize(220, 280),
        QSize(88, 112),
        11,
        14,
        140,
        16,
    };
}

QSize adaptivePreviewImageSize(const QWidget* panel,
                               const PreviewImageBounds& bounds,
                               const int columns)
{
    if (panel == nullptr) {
        return bounds.maxSize;
    }

    const int shared = qMax(1, columns);
    const int availableWidth = qMax(
        bounds.minSize.width(),
        (panel->width() - bounds.horizontalPadding - (shared - 1) * kPreviewImageGap) / shared);
    const int availableHeight = qMax(
        bounds.minSize.height(),
        panel->height() - bounds.reservedDetailsHeight - bounds.horizontalPadding / 2);

    int width = qMin(bounds.maxSize.width(), availableWidth);
    int height = (width * bounds.aspectHeight) / qMax(1, bounds.aspectWidth);

    if (height > bounds.maxSize.height()) {
        height = bounds.maxSize.height();
        width = (height * bounds.aspectWidth) / qMax(1, bounds.aspectHeight);
    }
    if (height > availableHeight) {
        height = qMax(bounds.minSize.height(), availableHeight);
        width = (height * bounds.aspectWidth) / qMax(1, bounds.aspectHeight);
    }

    width = qBound(bounds.minSize.width(), width, bounds.maxSize.width());
    height = qBound(bounds.minSize.height(), height, bounds.maxSize.height());
    return QSize(width, height);
}

void applyPreviewLabelGeometry(QLabel* label, const QSize& size)
{
    if (label == nullptr || !size.isValid()) {
        return;
    }
    label->setFixedSize(size);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

bool shouldSyncPreviewPanel(QObject* watched, QWidget* previewPanel, QEvent* event)
{
    return previewPanel != nullptr && watched == previewPanel
           && event != nullptr && event->type() == QEvent::Resize;
}

void localizeButtonBox(QDialogButtonBox* box)
{
    if (box == nullptr) {
        return;
    }

    relabelStandardButtons(box);
    keepStandardButtonLabels(box);
}

void localizeMessageBox(QMessageBox* box)
{
    if (box == nullptr) {
        return;
    }

    // QMessageBox::StandardButton mirrors QDialogButtonBox::StandardButton
    // value for value, which is why one table serves both.
    for (const StandardButtonLabel& entry : kStandardButtonLabels) {
        const auto which = static_cast<QMessageBox::StandardButton>(entry.button);
        if (QAbstractButton* button = box->button(which)) {
            button->setText(T(entry.key));
        }
    }
    keepStandardButtonLabels(box->findChild<QDialogButtonBox*>(QString(), Qt::FindDirectChildrenOnly));
}

void showWarning(QWidget* parent, const QString& title, const QString& text)
{
    runMessageBox(parent, QMessageBox::Warning, title, text, QMessageBox::Ok, QMessageBox::Ok);
}

void showRepoError(QWidget* parent, const Core::Error& error)
{
    showWarning(parent, T("common.error"), errorText(error));
}

void showInformation(QWidget* parent, const QString& title, const QString& text)
{
    runMessageBox(parent, QMessageBox::Information, title, text, QMessageBox::Ok, QMessageBox::Ok);
}

void showCritical(QWidget* parent, const QString& title, const QString& text)
{
    runMessageBox(parent, QMessageBox::Critical, title, text, QMessageBox::Ok, QMessageBox::Ok);
}

bool askYesNo(QWidget* parent, const QString& title, const QString& text)
{
    const QMessageBox::StandardButton answer = runMessageBox(parent,
                                                             QMessageBox::Question,
                                                             title,
                                                             text,
                                                             QMessageBox::Yes | QMessageBox::No,
                                                             QMessageBox::No);
    return answer == QMessageBox::Yes;
}

bool showWarningWithAction(QWidget* parent,
                           const QString& title,
                           const QString& text,
                           const QString& actionText)
{
    QMessageBox box(QMessageBox::Warning, title, text, QMessageBox::Ok, parent);
    // ActionRole so the button sits beside Ok and carries its own label, which
    // localizeMessageBox leaves alone -- it relabels Qt's standard buttons, and
    // this one is already in the librarian's language.
    QPushButton* action = box.addButton(actionText, QMessageBox::ActionRole);
    box.setDefaultButton(QMessageBox::Ok);
    localizeMessageBox(&box);
    box.exec();
    return box.clickedButton() == action;
}

bool askYesNoWithCheckBox(QWidget* parent,
                          const QString& title,
                          const QString& text,
                          const QString& checkBoxText,
                          bool* checked)
{
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, parent);

    // Parented to the box, which deletes it; reading it after exec is fine
    // because the box outlives the call and is destroyed on the way out.
    auto* checkBox = new QCheckBox(checkBoxText, &box);
    checkBox->setChecked(checked != nullptr && *checked);
    box.setCheckBox(checkBox);

    box.setDefaultButton(QMessageBox::No);
    localizeMessageBox(&box);
    const auto answer = static_cast<QMessageBox::StandardButton>(box.exec());

    if (checked != nullptr) {
        *checked = checkBox->isChecked();
    }
    return answer == QMessageBox::Yes;
}

void localizeInputDialog(QInputDialog* dialog)
{
    if (dialog == nullptr) {
        return;
    }

    // Not localizeButtonBox: QInputDialog builds its layout lazily, so
    // findChild<QDialogButtonBox*> comes back null on a dialog that has not
    // been shown yet and the relabelling would silently do nothing. These two
    // setters are what force the layout into existence.
    dialog->setOkButtonText(T("common.ok"));
    dialog->setCancelButtonText(T("common.cancel"));
    keepStandardButtonLabels(dialog->findChild<QDialogButtonBox*>(QString(), Qt::FindDirectChildrenOnly));
}

QString askForImageFile(QWidget* parent, const QString& title, const QString& nameFilter)
{
    QFileDialog dialog(parent, title);
    // Qt's own dialog, never the platform's: see the header for why.
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setViewMode(QFileDialog::Detail);
    if (!nameFilter.isEmpty()) {
        dialog.setNameFilter(nameFilter);
    }

    // The labels Qt exposes as settable. The rest of the dialog's own text --
    // the toolbar tooltips, the column headers -- comes from the qtbase
    // catalogue the application now ships; see Application::applyLocale.
    const auto applyLabels = [&dialog] {
        dialog.setLabelText(QFileDialog::Accept, T("file.open"));
        dialog.setLabelText(QFileDialog::Reject, T("common.cancel"));
        dialog.setLabelText(QFileDialog::FileName, T("file.fileName"));
        dialog.setLabelText(QFileDialog::FileType, T("file.fileType"));
        dialog.setLabelText(QFileDialog::LookIn, T("file.lookIn"));
    };
    applyLabels();
    // Its button box sits inside Qt's own form, not directly under the dialog.
    keepButtonLabels(dialog.findChild<QDialogButtonBox*>(), applyLabels);

    if (dialog.exec() != QDialog::Accepted) {
        return {};
    }
    const QStringList chosen = dialog.selectedFiles();
    return chosen.isEmpty() ? QString() : chosen.first();
}

QString askForText(QWidget* parent,
                   const QString& title,
                   const QString& label,
                   const QString& initialValue,
                   bool* accepted)
{
    QInputDialog dialog(parent);
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setTextValue(initialValue);
    localizeInputDialog(&dialog);

    const bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted != nullptr) {
        *accepted = ok;
    }
    return ok ? dialog.textValue() : QString();
}

}  // namespace VLMS
