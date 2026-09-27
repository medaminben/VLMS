#pragma once

#include "QtBridge.h"

#include <VLMS/Core/Result.h>
#include <VLMS/Core/Strings.h>

#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QSize>

class QDateEdit;
class QDialogButtonBox;
class QFormLayout;
class QFrame;
class QLayout;
class QEvent;
class QInputDialog;
class QMessageBox;
class QTableView;
class QTableWidget;
class QVBoxLayout;
class QWidget;

namespace VLMS {

/** Shared width of the left-hand filter column on the catalog/members/loans pages. */
inline constexpr int kFilterColumnWidth = 220;

struct PreviewImageBounds {
    QSize maxSize;
    QSize minSize;
    int aspectWidth = 2;
    int aspectHeight = 3;
    int reservedDetailsHeight = 140;
    int horizontalPadding = 16;
};

PreviewImageBounds bookCoverPreviewBounds();
PreviewImageBounds memberPhotoPreviewBounds();
/// Space between two preview images that share the viewer's width.
inline constexpr int kPreviewImageGap = 8;
/// `columns` is how many images share the panel's width side by side.
QSize adaptivePreviewImageSize(const QWidget* panel,
                               const PreviewImageBounds& bounds,
                               int columns = 1);
void applyPreviewLabelGeometry(QLabel* label, const QSize& size);
bool shouldSyncPreviewPanel(QObject* watched, QWidget* previewPanel, QEvent* event);

QPushButton* makeNavButton(const QString& text);
QPushButton* makePrimaryButton(const QString& text);
QPushButton* makeSecondaryButton(const QString& text);
QPushButton* makeGhostButton(const QString& text);
QFrame* makeCard(QWidget* parent = nullptr);
QLabel* makePageTitle(const QString& text, QWidget* parent = nullptr);
QLabel* makePageSubtitle(const QString& text, QWidget* parent = nullptr);
QWidget* makePageHeader(const QString& title, const QString& subtitle, QWidget* parent = nullptr);
void clearLayout(QLayout* layout);
void configurePageLayout(QVBoxLayout* layout);
void configureFormLayout(QFormLayout* form);
QWidget* makeFilterColumn(QWidget* parent);
void configureResizableColumns(QTableWidget* table, const QList<int>& widths);
void refreshWidgetStyle(QWidget* widget);

/// Shows a date edit as 2026-09-23 in every language. Qt lays the sections of
/// a right-to-left date edit out in reverse, so yyyy-MM-dd read 23-09-2026 in
/// Arabic while every table and label beside it read 2026-09-23. Call it after
/// the edit has its layout direction, which it inherits from the application.
void setIsoDateFormat(QDateEdit* edit);

/// Selects a whole row, as QTableView::selectRow means to. selectRow finds
/// its column from the header section at the viewport's leading edge; right to
/// left that edge is the far right, and while the sections do not fill the
/// width there is no section there, so nothing was selected. In Arabic that
/// lost the selection on every refresh, and the preview kept whatever it had
/// last painted -- a light-mode placeholder after a switch to dark.
void selectTableRow(QTableView* table, int row);
QWidget* makeDetailValueWidget(QWidget* parent, bool scrollable = false);
void setDetailValueText(QWidget* widget, const QString& text);
void clearDetailValueText(QWidget* widget);

[[nodiscard]] inline QString dashIfEmpty(const QString& value)
{
    return value.isEmpty() ? T("common.emDash") : value;
}

[[nodiscard]] inline QString dashIfEmpty(std::string_view value)
{
    return value.empty() ? T("common.emDash") : qs(value);
}

[[nodiscard]] inline QString errorText(const Error& error)
{
    if (error.key.empty()) {
        return T("error.sql");
    }
    if (!error.detail.empty()) {
        return T(error.key, "detail", error.detail);
    }
    return T(error.key);
}

void showRepoError(QWidget* parent, const Error& error);

/**
 * Standard buttons -- Ok, Cancel, Yes, No, Close -- are labelled by Qt, not by
 * this application. Qt reads those labels out of its own qtbase_<lang>.qm
 * catalogue, which nothing here ever loads or ships, so a QDialogButtonBox in
 * an Arabic window still says "OK" and "Cancel". The two functions below
 * relabel them from the application's own string table.
 *
 * Every dialog that owns a button box must call localizeButtonBox on it, and
 * message boxes must go through the four helpers rather than the static
 * QMessageBox::warning/information/question/critical, which build and run the
 * box in one call and leave no window to relabel.
 */
void localizeButtonBox(QDialogButtonBox* box);
void localizeMessageBox(QMessageBox* box);

/// Re-relabels a button box's standard buttons on a live language switch;
/// localizeButtonBox only runs at construction, and Qt keeps translating
/// standard buttons against the system locale rather than Locale::code().
void retranslateStandardButtons(QDialogButtonBox* buttons);

void showWarning(QWidget* parent, const QString& title, const QString& text);
void showInformation(QWidget* parent, const QString& title, const QString& text);
void showCritical(QWidget* parent, const QString& title, const QString& text);
/// A Yes/No question. True when the librarian chose Yes; No is the default,
/// so Escape and Return both decline.
[[nodiscard]] bool askYesNo(QWidget* parent, const QString& title, const QString& text);

/**
 * A warning that offers somewhere to go about it, next to Ok.
 *
 * True when the librarian chose the action rather than dismissing the box. Ok
 * stays the default, so Escape and Return both just close it -- the action is
 * navigation, and navigation should never be what a stray Return does.
 */
[[nodiscard]] bool showWarningWithAction(QWidget* parent,
                                         const QString& title,
                                         const QString& text,
                                         const QString& actionText);

/**
 * A Yes/No question carrying one checkbox, for a confirmation whose answer has
 * a second dimension: not only whether to go ahead, but how.
 *
 * True when the librarian chose Yes, in which case `*checked` reports the box.
 * `*checked` is also the initial state, so the caller picks the default answer
 * and the safer one belongs there. No is the default button, as in askYesNo.
 */
[[nodiscard]] bool askYesNoWithCheckBox(QWidget* parent,
                                        const QString& title,
                                        const QString& text,
                                        const QString& checkBoxText,
                                        bool* checked);

/// Relabels a QInputDialog's Ok and Cancel. Separate from localizeButtonBox
/// because QInputDialog has no button box until its layout is built.
void localizeInputDialog(QInputDialog* dialog);

/**
 * The "choose an image" picker, for book covers, member photos, identity
 * cards and the page handed to OCR.
 *
 * Not QFileDialog::getOpenFileName. That static call asks the platform for
 * its own file picker -- the Windows Explorer one -- and a picker owned by the
 * operating system is neither painted in this application's colours nor
 * written in the language the librarian chose: it stayed white in dark mode
 * and French on an Arabic desktop. This builds Qt's own dialog instead, which
 * follows the palette, the stylesheet and the layout direction like every
 * other window here.
 *
 * Returns an empty string when the picker was cancelled.
 */
[[nodiscard]] QString askForImageFile(QWidget* parent,
                                      const QString& title,
                                      const QString& nameFilter);

/// QInputDialog::getText with its Ok/Cancel relabelled, for the same reason.
/// `accepted` is false when the dialog was cancelled.
[[nodiscard]] QString askForText(QWidget* parent,
                                 const QString& title,
                                 const QString& label,
                                 const QString& initialValue,
                                 bool* accepted);

}  // namespace VLMS
