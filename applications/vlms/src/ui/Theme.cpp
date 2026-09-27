#include "ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSettings>
#include <QString>

namespace VLMS {

namespace {

constexpr const char* kFontFamily = "Cairo";

bool isTrueTypeFont(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray header = file.read(4);
    return header.size() == 4
        && ((static_cast<unsigned char>(header[0]) == 0x00 && static_cast<unsigned char>(header[1]) == 0x01
             && static_cast<unsigned char>(header[2]) == 0x00 && static_cast<unsigned char>(header[3]) == 0x00)
            || header.startsWith("OTTO") || header.startsWith("ttcf") || header.startsWith("wOFF"));
}

void loadFontResource(const char* path)
{
    const QString resourcePath = QString::fromUtf8(path);
    if (!isTrueTypeFont(resourcePath)) {
        return;
    }
    const int id = QFontDatabase::addApplicationFont(resourcePath);
    if (id < 0) {
        return;
    }
    Q_UNUSED(id);
}

// One name per role, not per colour: the sheet below says what a colour is for,
// and the two palettes say what that role looks like in each mode. Adding a
// literal hex value to the sheet would make it light-only again.
struct Palette {
    const char* windowBg;
    const char* surface;
    const char* surfaceMuted;   // headers, drop-downs, stepper buttons
    const char* placeholderBg;  // image wells and other empty slots
    const char* alternateBg;    // every second row of a striped table
    const char* border;
    const char* gridLine;
    const char* textPrimary;
    const char* textStrong;  // section titles, table headers
    const char* textMuted;   // subtitles, footer, secondary values
    const char* textDisabled;
    const char* hoverBg;
    const char* pressedBg;
    const char* selectionBg;
    const char* disabledBg;
    const char* focusBorder;
    const char* primaryBg;
    const char* primaryFg;
    const char* primaryHoverBg;
    const char* alertBg;
    const char* alertFg;
    const char* alertBorder;
    // The catalogue's local-number column, and nothing else so far: a copy
    // sitting on the shelf against one that is out on loan. Kept out of the
    // sheet -- the delegate paints those digits itself.
    const char* copyAvailable;
    const char* copyOnLoan;
    const char* scrollHandle;
    const char* scrollHandleHover;
    // Palette-only, so it never reaches the sheet: QPalette::Link is what a
    // QLabel with rich text paints an <a> in, and nothing here styles that.
    const char* link;
    // Not a colour but a file-name stem: the arrow and checkmark images are
    // pre-tinted, so the sheet picks a file rather than a value. See the
    // ::down-arrow and ::indicator:checked rules.
    const char* arrowTint;
};

constexpr Palette kLightPalette{
    /* windowBg      */ "#f1f5f9",
    /* surface       */ "#ffffff",
    /* surfaceMuted  */ "#f8fafc",
    /* placeholderBg */ "#f1f5f9",
    /* alternateBg   */ "#f8fafc",
    /* border        */ "#cbd5e1",
    /* gridLine      */ "#e2e8f0",
    /* textPrimary   */ "#0f172a",
    /* textStrong    */ "#475569",
    /* textMuted     */ "#64748b",
    /* textDisabled  */ "#94a3b8",
    /* hoverBg       */ "#f1f5f9",
    /* pressedBg     */ "#cbd5e1",
    /* selectionBg   */ "#e2e8f0",
    /* disabledBg    */ "#f1f5f9",
    /* focusBorder   */ "#475569",
    /* primaryBg     */ "#1e293b",
    /* primaryFg     */ "#ffffff",
    /* primaryHoverBg*/ "#0f172a",
    /* alertBg       */ "#fef2f2",
    /* alertFg       */ "#991b1b",
    /* alertBorder   */ "#fecaca",
    /* copyAvailable */ "#15803d",
    /* copyOnLoan    */ "#b91c1c",
    /* scrollHandle  */ "#cbd5e1",
    /* scrollHandleH */ "#94a3b8",
    /* link          */ "#1d4ed8",
    /* arrowTint     */ "light",
};

// The same slate ramp read from the other end. The primary button inverts --
// light fill on a dark window -- because a dark fill would disappear into it.
constexpr Palette kDarkPalette{
    /* windowBg      */ "#0b1220",
    /* surface       */ "#111c2e",
    /* surfaceMuted  */ "#16223a",
    /* placeholderBg */ "#16223a",
    /* alternateBg   */ "#16223a",
    /* border        */ "#334155",
    /* gridLine      */ "#1e293b",
    /* textPrimary   */ "#e2e8f0",
    /* textStrong    */ "#cbd5e1",
    /* textMuted     */ "#94a3b8",
    /* textDisabled  */ "#64748b",
    /* hoverBg       */ "#1e293b",
    /* pressedBg     */ "#334155",
    /* selectionBg   */ "#334155",
    /* disabledBg    */ "#16223a",
    /* focusBorder   */ "#94a3b8",
    /* primaryBg     */ "#e2e8f0",
    /* primaryFg     */ "#0f172a",
    /* primaryHoverBg*/ "#f8fafc",
    /* alertBg       */ "#3b1d1d",
    /* alertFg       */ "#fca5a5",
    /* alertBorder   */ "#7f1d1d",
    /* copyAvailable */ "#86efac",
    /* copyOnLoan    */ "#fca5a5",
    /* scrollHandle  */ "#334155",
    /* scrollHandleH */ "#475569",
    /* link          */ "#93c5fd",
    /* arrowTint     */ "dark",
};

QString styleSheetTemplate()
{
    return QStringLiteral(R"(
QMainWindow, QWidget#centralRoot {
  background: @windowBg;
  color: @textPrimary;
  font-family: "Cairo", "Segoe UI", Tahoma, "Arial Unicode MS", sans-serif;
  font-size: 15px;
}

/* No min-height: the row is as tall as the brand banner in it, and MainWindow
   sizes that. A min-height here would not just be ignored -- Qt writes it onto
   the widget as an explicit minimum on every repolish, and an explicit minimum
   is what a layout shrinks to when the window is short, which cropped the
   banner. */
QWidget#appHeader {
  background: @surface;
  color: @textPrimary;
  border-bottom: 1px solid @border;
}

QPushButton#navLink {
  background: transparent;
  color: @textMuted;
  border: none;
  border-radius: 4px;
  padding: 8px 12px;
  font-size: 14px;
  font-weight: 500;
}

QPushButton#navLink:hover {
  color: @textPrimary;
  background: @hoverBg;
}

QPushButton#navLink[active="true"] {
  color: @textPrimary;
  background: @hoverBg;
}

/* Round, and fixed at the diameter the radius assumes: a border-radius is half
   the height only as long as nothing stretches the button, and an icon-only
   button with no width bounds grows with the header's spare space. */
QPushButton#themeToggle,
QPushButton#manualButton {
  background: transparent;
  border: 1px solid @border;
  border-radius: 17px;
  min-width: 34px;
  max-width: 34px;
  min-height: 34px;
  max-height: 34px;
  padding: 0px;
}

QPushButton#themeToggle:hover,
QPushButton#manualButton:hover {
  color: @textPrimary;
  background: @hoverBg;
  border-color: @focusBorder;
}

/* The language flags, sized and rounded like the theme toggle beside them. The
   border is transparent rather than absent so that marking the active one does
   not move the icon by a pixel. */
QPushButton#languageButton {
  background: transparent;
  border: 1px solid transparent;
  border-radius: 17px;
  min-width: 34px;
  max-width: 34px;
  min-height: 34px;
  max-height: 34px;
  padding: 0px;
}

QPushButton#languageButton:hover {
  background: @hoverBg;
}

QPushButton#languageButton[active="true"] {
  background: @pressedBg;
  border-color: @focusBorder;
}

QPushButton#languageButton:focus {
  border: 2px solid @focusBorder;
}

QWidget#appFooter {
  background: @surface;
  border-top: 1px solid @border;
  color: @textMuted;
  font-size: 13px;
}

QLabel#pageTitle {
  font-size: 26px;
  font-weight: 700;
  color: @textPrimary;
}

QLabel#pageSubtitle {
  color: @textMuted;
  font-size: 14px;
}

QLineEdit, QComboBox, QTextEdit, QPlainTextEdit, QSpinBox,
QDateEdit, QTimeEdit, QDateTimeEdit {
  background: @surface;
  border: 1px solid @border;
  border-radius: 4px;
  padding: 8px 12px;
  color: @textPrimary;
  selection-background-color: @selectionBg;
  selection-color: @textPrimary;
  min-height: 20px;
}

QLineEdit:disabled, QComboBox:disabled, QTextEdit:disabled, QPlainTextEdit:disabled,
QSpinBox:disabled, QDateEdit:disabled, QTimeEdit:disabled, QDateTimeEdit:disabled {
  background: @disabledBg;
  color: @textDisabled;
}

/* Qt paints the placeholder from QPalette::PlaceholderText, which the sheet
   cannot reach -- but it accepts this property, and setting it here keeps the
   hint legible on a dark field instead of near-black on near-black. */
QLineEdit, QTextEdit, QPlainTextEdit {
  placeholder-text-color: @textDisabled;
}

QComboBox {
  padding-right: 28px;
}

QComboBox::drop-down {
  subcontrol-origin: padding;
  subcontrol-position: top right;
  width: 24px;
  border-left: 1px solid @border;
  border-top-right-radius: 4px;
  border-bottom-right-radius: 4px;
  background: @surfaceMuted;
}

QComboBox::drop-down:hover {
  background: @hoverBg;
}

/* An image, not the CSS three-borders triangle: Qt renders that trick as a
   plain grey rectangle, which is what every drop-down in the application was
   actually showing. */
QComboBox::down-arrow {
  image: url(:/ui/arrow-down-@arrowTint.png);
  width: 9px;
  height: 6px;
}

QComboBox QAbstractItemView {
  background: @surface;
  color: @textPrimary;
  border: 1px solid @border;
  selection-background-color: @selectionBg;
  selection-color: @textPrimary;
  outline: none;
}

QComboBox:disabled {
  color: @textDisabled;
  background: @disabledBg;
}

QLineEdit:focus, QComboBox:focus, QTextEdit:focus, QPlainTextEdit:focus,
QSpinBox:focus, QDateEdit:focus, QTimeEdit:focus, QDateTimeEdit:focus {
  border-color: @focusBorder;
}

/* The steppers on a spin box and the drop-down on a date field. Left to the
   platform style these are painted from the system palette, which is the one
   thing dark mode cannot change. */
QSpinBox::up-button, QSpinBox::down-button,
QDateEdit::up-button, QDateEdit::down-button,
QTimeEdit::up-button, QTimeEdit::down-button,
QDateTimeEdit::up-button, QDateTimeEdit::down-button {
  background: @surfaceMuted;
  border: none;
  width: 18px;
}

QSpinBox::up-button:hover, QSpinBox::down-button:hover,
QDateEdit::up-button:hover, QDateEdit::down-button:hover,
QTimeEdit::up-button:hover, QTimeEdit::down-button:hover,
QDateTimeEdit::up-button:hover, QDateTimeEdit::down-button:hover {
  background: @hoverBg;
}

QSpinBox::up-arrow, QDateEdit::up-arrow, QTimeEdit::up-arrow, QDateTimeEdit::up-arrow {
  image: url(:/ui/arrow-up-@arrowTint.png);
  width: 9px;
  height: 6px;
}

QSpinBox::down-arrow, QDateEdit::down-arrow, QTimeEdit::down-arrow, QDateTimeEdit::down-arrow {
  image: url(:/ui/arrow-down-@arrowTint.png);
  width: 9px;
  height: 6px;
}

QSpinBox::up-arrow:disabled, QSpinBox::down-arrow:disabled,
QDateEdit::up-arrow:disabled, QDateEdit::down-arrow:disabled {
  image: none;
}

QDateEdit::drop-down, QTimeEdit::drop-down, QDateTimeEdit::drop-down {
  subcontrol-origin: padding;
  subcontrol-position: top right;
  width: 22px;
  border-left: 1px solid @border;
  background: @surfaceMuted;
}

/* The pop-up behind a date field's calendar button. It is a window of its own,
   so nothing above reaches it. */
QCalendarWidget QWidget {
  background: @surface;
  color: @textPrimary;
  alternate-background-color: @alternateBg;
}

QCalendarWidget QAbstractItemView {
  background: @surface;
  color: @textPrimary;
  selection-background-color: @selectionBg;
  selection-color: @textPrimary;
  outline: none;
}

QCalendarWidget QWidget#qt_calendar_navigationbar {
  background: @surfaceMuted;
  border-bottom: 1px solid @border;
}

QCalendarWidget QToolButton {
  background: transparent;
  color: @textPrimary;
  border: none;
  padding: 4px 8px;
}

QCalendarWidget QToolButton:hover {
  background: @hoverBg;
}

QCalendarWidget QMenu {
  background: @surface;
  color: @textPrimary;
}

QCalendarWidget QSpinBox {
  background: @surface;
  color: @textPrimary;
}

QFrame#copiesStepper {
  background: @surface;
  border: 1px solid @border;
  border-radius: 4px;
}

QSpinBox#copiesSpin {
  background: transparent;
  border: none;
  padding: 0px;
  margin: 0px;
  min-height: 32px;
  max-height: 32px;
}

QToolButton#copiesStepBtn {
  background: @surfaceMuted;
  color: @textPrimary;
  border: none;
  border-radius: 0px;
  padding: 0px;
  margin: 0px;
  font-size: 16px;
  font-weight: 700;
}

QToolButton#copiesStepBtn:hover {
  background: @selectionBg;
}

QToolButton#copiesStepBtn:pressed {
  background: @pressedBg;
}

QToolButton#copiesStepBtn:disabled {
  color: @textDisabled;
  background: @disabledBg;
}

QPushButton#btnPrimary {
  background: @primaryBg;
  color: @primaryFg;
  border: 1px solid @primaryBg;
  border-radius: 4px;
  padding: 5px 14px;
  font-weight: 400;
}

QPushButton#btnPrimary:hover { background: @primaryHoverBg; border-color: @primaryHoverBg; }
QPushButton#btnPrimary:disabled { opacity: 0.55; }

QPushButton#btnSecondary {
  background: @surface;
  color: @textPrimary;
  border: 1px solid @border;
  border-radius: 4px;
  padding: 5px 12px;
  font-weight: 400;
}

QPushButton#btnSecondary:hover {
  background: @hoverBg;
  border-color: @focusBorder;
  color: @textPrimary;
}

QPushButton#pagerFirst, QPushButton#pagerPrevious,
QPushButton#pagerNext, QPushButton#pagerLast {
  background: @surface;
  color: @textPrimary;
  border: 1px solid @border;
  border-radius: 4px;
  padding: 4px 10px;
  min-width: 0;
  max-height: 28px;
  font-weight: 400;
}

QPushButton#pagerFirst:hover, QPushButton#pagerPrevious:hover,
QPushButton#pagerNext:hover, QPushButton#pagerLast:hover {
  background: @hoverBg;
  border-color: @focusBorder;
  color: @textPrimary;
}

QPushButton#pagerFirst:disabled, QPushButton#pagerPrevious:disabled,
QPushButton#pagerNext:disabled, QPushButton#pagerLast:disabled {
  background: @disabledBg;
  color: @textDisabled;
  border-color: @border;
}

/* The three birth-date boxes hold "00" or "0000". The general rule's 12px left
   padding plus a 28px right padding that the 24px arrow then sits inside
   leaves each box 66px of chrome, which pushed the row over the Sex label. */
QComboBox#birthDatePart {
  padding-left: 6px;
  padding-right: 4px;
}

QComboBox#pagerPageSize {
  padding: 1px 20px 1px 6px;
  min-height: 0;
  max-height: 28px;
}

QSpinBox#pagerPageSpin {
  padding: 1px 2px;
  min-height: 0;
  max-height: 28px;
  max-width: 64px;
}

QPushButton#btnGhost {
  background: @surface;
  color: @textPrimary;
  border: 1px solid @border;
  border-radius: 4px;
  padding: 4px 10px;
  font-weight: 400;
}

QPushButton#btnGhost:hover {
  background: @hoverBg;
  border-color: @focusBorder;
  color: @textPrimary;
}

/* The buttons nobody named. A QDialogButtonBox builds its own Ok and Cancel,
   and until this rule existed they were the only two controls in the whole
   application still painted by the platform style -- white on white in dark
   mode. The object-name rules above outrank this one, so the named buttons
   keep their own look. */
QPushButton {
  background: @surface;
  color: @textPrimary;
  border: 1px solid @border;
  border-radius: 4px;
  padding: 5px 14px;
  min-width: 76px;
  font-weight: 400;
}

QPushButton:hover {
  background: @hoverBg;
  border-color: @focusBorder;
}

QPushButton:pressed {
  background: @pressedBg;
}

QPushButton:disabled {
  background: @disabledBg;
  color: @textDisabled;
  border-color: @border;
}

/* The accept button of a dialog, which Qt marks as the default. It reads as
   the primary action in both modes for the same reason btnPrimary does. */
QPushButton:default {
  background: @primaryBg;
  color: @primaryFg;
  border-color: @primaryBg;
}

QPushButton:default:hover {
  background: @primaryHoverBg;
  border-color: @primaryHoverBg;
}

QFrame#card {
  background: @surface;
  border: 1px solid @border;
  border-radius: 6px;
}

QLabel#sectionTitle {
  color: @textStrong;
  font-size: 14px;
  font-weight: 600;
}

QLabel#metricLabel {
  color: @textMuted;
  font-size: 11px;
}

QLabel#metricValue {
  color: @textPrimary;
  font-size: 20px;
  font-weight: 600;
}

QTableWidget#metricsActivityTable,
QTableWidget#metricsCategoriesTable {
  border: none;
  background: transparent;
}

QTableWidget#metricsActivityTable QHeaderView::section,
QTableWidget#metricsCategoriesTable QHeaderView::section {
  padding: 4px 8px;
  font-size: 12px;
}

QLabel#bookCover,
QLabel#memberPhoto,
QLabel#loanMemberPhoto,
QLabel#archiveImage,
QLabel#archiveMemberPhoto,
QLabel#memberIdImage {
  background: @placeholderBg;
  border: 1px solid @border;
  border-radius: 6px;
}

QLabel#alertError {
  background: @alertBg;
  color: @alertFg;
  border: 1px solid @alertBorder;
  border-radius: 4px;
  padding: 10px 12px;
}

QLabel#emptyState {
  color: @textMuted;
  font-size: 16px;
  padding: 24px;
}

/* QTableView/QListView/QTreeView, not only the QWidget convenience classes:
   the file dialog builds the plain views, and a rule naming QTableWidget alone
   leaves them white. */
QTableWidget, QListWidget, QTreeWidget,
QTableView, QListView, QTreeView {
  background: @surface;
  border: 1px solid @border;
  border-radius: 6px;
  gridline-color: @gridLine;
  color: @textPrimary;
  alternate-background-color: @alternateBg;
  selection-background-color: @selectionBg;
  selection-color: @textPrimary;
  outline: none;
}

QTableView::item, QListView::item, QTreeView::item {
  color: @textPrimary;
  padding: 2px 4px;
}

QTableView::item:selected, QListView::item:selected, QTreeView::item:selected {
  background: @selectionBg;
  color: @textPrimary;
}

QTableView::item:hover, QListView::item:hover, QTreeView::item:hover {
  background: @hoverBg;
}

QTreeView::branch {
  background: @surface;
}

/* A scroll area paints its own frame, but the scrolling happens on a child
   viewport widget that keeps QPalette::Base -- the white rectangle that showed
   through the catalogue, member and loan detail panels. */
QAbstractScrollArea {
  background: @surface;
  color: @textPrimary;
}

QScrollArea, QScrollArea > QWidget > QWidget {
  background: transparent;
}

QScrollBar:vertical {
  background: transparent;
  width: 10px;
  margin: 0px;
}

QScrollBar:horizontal {
  background: transparent;
  height: 10px;
  margin: 0px;
}

QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
  background: @scrollHandle;
  border-radius: 5px;
  min-height: 24px;
  min-width: 24px;
}

QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover {
  background: @scrollHandleHover;
}

/* Zero-sized rather than hidden: a QScrollBar line-step button with no rule
   is drawn by the platform style, arrow and all. */
QScrollBar::add-line, QScrollBar::sub-line {
  height: 0px;
  width: 0px;
  background: transparent;
  border: none;
}

QScrollBar::add-page, QScrollBar::sub-page {
  background: transparent;
}

/* The tab strip over the book editor's Book/Copies panes. */
QTabWidget::pane {
  background: @surface;
  border: 1px solid @border;
  border-radius: 6px;
  top: -1px;
}

QTabBar {
  background: transparent;
}

QTabBar::tab {
  background: @surfaceMuted;
  color: @textMuted;
  border: 1px solid @border;
  border-bottom: none;
  border-top-left-radius: 6px;
  border-top-right-radius: 6px;
  padding: 8px 16px;
  margin-right: 2px;
}

QTabBar::tab:selected {
  background: @surface;
  color: @textPrimary;
}

QTabBar::tab:hover:!selected {
  background: @hoverBg;
  color: @textPrimary;
}

QCheckBox, QRadioButton, QGroupBox {
  color: @textPrimary;
  background: transparent;
}

QGroupBox {
  border: 1px solid @border;
  border-radius: 6px;
  margin-top: 10px;
  padding-top: 8px;
}

QGroupBox::title {
  subcontrol-origin: margin;
  subcontrol-position: top left;
  padding: 0px 6px;
  color: @textStrong;
}

QCheckBox::indicator, QRadioButton::indicator {
  width: 16px;
  height: 16px;
  background: @surface;
  border: 1px solid @border;
  border-radius: 3px;
}

QRadioButton::indicator {
  border-radius: 8px;
}

QCheckBox::indicator:checked, QRadioButton::indicator:checked {
  background: @primaryBg;
  border-color: @primaryBg;
}

QCheckBox::indicator:checked {
  image: url(:/ui/check-@arrowTint.png);
}

QCheckBox::indicator:disabled, QRadioButton::indicator:disabled {
  background: @disabledBg;
  border-color: @border;
}

QToolButton {
  background: transparent;
  color: @textPrimary;
  border: 1px solid transparent;
  border-radius: 4px;
  padding: 4px;
}

QToolButton:hover {
  background: @hoverBg;
  border-color: @border;
}

/* The OCR button is a QToolButton wearing the btnSecondary name, and the
   button rules above are all QPushButton, which never matched it. */
QToolButton#btnSecondary {
  background: @surface;
  color: @textPrimary;
  border: 1px solid @border;
  border-radius: 4px;
  padding: 4px 12px;
  font-weight: 400;
}

QToolButton#btnSecondary:hover {
  background: @hoverBg;
  border-color: @focusBorder;
}

/* The arrow half of a menu button. Styling a QToolButton at all makes Qt stop
   drawing this section itself, and what was left was a black slab. */
QToolButton::menu-button {
  background: @surfaceMuted;
  border-left: 1px solid @border;
  border-top-right-radius: 4px;
  border-bottom-right-radius: 4px;
  width: 18px;
}

QToolButton::menu-button:hover {
  background: @hoverBg;
}

QToolButton::menu-arrow, QToolButton::down-arrow {
  image: url(:/ui/arrow-down-@arrowTint.png);
  width: 9px;
  height: 6px;
}

QProgressBar {
  background: @surfaceMuted;
  border: 1px solid @border;
  border-radius: 4px;
  color: @textPrimary;
  text-align: center;
}

QProgressBar::chunk {
  background: @primaryBg;
  border-radius: 3px;
}

QLabel#bookDetailLabel {
  color: @textMuted;
  font-size: 13px;
  font-weight: 600;
  padding: 0;
  margin: 0;
}

QLabel#bookDetailValue {
  color: @textPrimary;
  font-size: 13px;
  padding: 0;
  margin: 0;
}

/* border:none is load-bearing now that plain QPlainTextEdit has a rule of its
   own: the type rule's 1px border would otherwise box in a read-only detail
   field that is meant to read as text on the panel. */
QPlainTextEdit#bookDetailValue {
  color: @textPrimary;
  font-size: 13px;
  padding: 0;
  margin: 0;
  border: none;
  background: transparent;
}

/* Same reasoning one step further out: a type selector matches subclasses, so
   the QTextEdit rule above boxes in a QTextBrowser too. setFrameShape(NoFrame)
   does not win against a stylesheet, and the licence is a document to be read,
   not a field to be typed into. */
QTextBrowser#licenceBody {
  border: none;
  background: transparent;
  padding: 0;
}

QHeaderView::section {
  background: @surfaceMuted;
  color: @textStrong;
  border: none;
  border-bottom: 1px solid @border;
  padding: 8px 10px;
  font-weight: 600;
}

QSplitter::handle {
  background: @gridLine;
}

QScrollArea { border: none; background: transparent; }

QMenu, QToolTip {
  background: @surface;
  color: @textPrimary;
  border: 1px solid @border;
}

QMenu::item:selected {
  background: @selectionBg;
  color: @textPrimary;
}

QDialog, QMessageBox {
  background: @windowBg;
  color: @textPrimary;
}

/* Last, and deliberately so: a bare QLabel takes its colour from the palette,
   and a dialog is full of bare labels -- form captions, the text of a message
   box, the "no image" placeholder. The named labels above outrank this. */
QLabel {
  background: transparent;
  color: @textPrimary;
}

QLabel:disabled {
  color: @textDisabled;
}

/* The non-native file dialog. Its own widgets are unnamed views and combo
   boxes covered above; these are the two pieces with an object name Qt gives
   them, and the side bar is a QListView that must not read as a table. */
QFileDialog {
  background: @windowBg;
  color: @textPrimary;
}

QFileDialog QListView#sidebar {
  background: @surfaceMuted;
  border: 1px solid @border;
}

QFileDialog QLabel {
  color: @textPrimary;
}
)");
}

QString paint(const QString& sheet, const Palette& palette)
{
    // Order matters where one role name is a prefix of another: @surfaceMuted
    // has to be substituted before @surface, or the tail is left behind as
    // literal "Muted". Keep any new prefix pair in the same order.
    struct Token {
        const char* name;
        const char* value;
    };
    const Token tokens[] = {
        {"@windowBg", palette.windowBg},
        {"@surfaceMuted", palette.surfaceMuted},
        {"@surface", palette.surface},
        {"@placeholderBg", palette.placeholderBg},
        {"@alternateBg", palette.alternateBg},
        {"@border", palette.border},
        {"@gridLine", palette.gridLine},
        {"@textPrimary", palette.textPrimary},
        {"@textStrong", palette.textStrong},
        {"@textMuted", palette.textMuted},
        {"@textDisabled", palette.textDisabled},
        {"@hoverBg", palette.hoverBg},
        {"@pressedBg", palette.pressedBg},
        {"@selectionBg", palette.selectionBg},
        {"@disabledBg", palette.disabledBg},
        {"@focusBorder", palette.focusBorder},
        {"@primaryHoverBg", palette.primaryHoverBg},
        {"@primaryBg", palette.primaryBg},
        {"@primaryFg", palette.primaryFg},
        {"@alertBorder", palette.alertBorder},
        {"@alertBg", palette.alertBg},
        {"@alertFg", palette.alertFg},
        {"@scrollHandleHover", palette.scrollHandleHover},
        {"@scrollHandle", palette.scrollHandle},
        {"@arrowTint", palette.arrowTint},
    };

    QString painted = sheet;
    for (const Token& token : tokens) {
        painted.replace(QLatin1String(token.name), QLatin1String(token.value));
    }
    return painted;
}

}  // namespace

ThemeMode Theme::s_mode = Theme::kDefaultMode;

ThemeMode Theme::mode()
{
    return s_mode;
}

void Theme::setMode(ThemeMode mode)
{
    s_mode = mode;
}

bool Theme::isDark()
{
    return s_mode == ThemeMode::Dark;
}

void Theme::loadSaved()
{
    QSettings settings;
    const QString stored = settings.value(QStringLiteral("ui/theme"), QStringLiteral("light")).toString();
    s_mode = (stored == QLatin1String("dark")) ? ThemeMode::Dark : ThemeMode::Light;
}

void Theme::save()
{
    QSettings settings;
    settings.setValue(
        QStringLiteral("ui/theme"),
        isDark() ? QStringLiteral("dark") : QStringLiteral("light"));
}

QIcon themeModeIcon(ThemeMode mode, int pixelSize, qreal ratio)
{
    const Palette& palette = (mode == ThemeMode::Dark) ? kDarkPalette : kLightPalette;
    const QColor ink(QString::fromLatin1(palette.textMuted));

    const int side = qMax(1, qRound(pixelSize * ratio));
    QPixmap canvas(side, side);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Drawn in a 24x24 box and scaled, so the proportions hold at any size.
    painter.scale(side / 24.0, side / 24.0);

    if (mode == ThemeMode::Dark) {
        // A crescent, cut rather than drawn: a full disc with a second disc
        // subtracted off-centre, which keeps the horns sharp at small sizes
        // where a stroked arc would blunt them.
        QPainterPath disc;
        disc.addEllipse(QPointF(12.0, 12.0), 8.0, 8.0);
        QPainterPath bite;
        bite.addEllipse(QPointF(15.5, 9.5), 7.5, 7.5);
        painter.fillPath(disc.subtracted(bite), ink);
    } else {
        painter.setBrush(ink);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(12.0, 12.0), 4.6, 4.6);

        QPen rayPen(ink);
        rayPen.setWidthF(1.8);
        rayPen.setCapStyle(Qt::RoundCap);
        painter.setPen(rayPen);
        for (int ray = 0; ray < 8; ++ray) {
            painter.save();
            painter.translate(12.0, 12.0);
            painter.rotate(ray * 45.0);
            painter.drawLine(QPointF(0.0, -7.4), QPointF(0.0, -9.6));
            painter.restore();
        }
    }

    painter.end();
    canvas.setDevicePixelRatio(ratio);
    return QIcon(canvas);
}

QIcon manualButtonIcon(ThemeMode mode, int pixelSize, qreal ratio, Qt::LayoutDirection direction)
{
    const Palette& palette = (mode == ThemeMode::Dark) ? kDarkPalette : kLightPalette;
    const QColor ink(QString::fromLatin1(palette.textMuted));

    const int side = qMax(1, qRound(pixelSize * ratio));
    QPixmap canvas(side, side);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font(QString::fromLatin1(kFontFamily));
    font.setPixelSize(side);
    painter.setFont(font);
    painter.setPen(ink);
    const QString mark = direction == Qt::RightToLeft ? QStringLiteral("\u061F")
                                                      : QStringLiteral("?");
    painter.drawText(QRect(0, 0, side, side), Qt::AlignCenter, mark);
    painter.end();

    canvas.setDevicePixelRatio(ratio);
    return QIcon(canvas);
}

QPixmap themedArtwork(const QPixmap& lightArtwork)
{
    if (!Theme::isDark() || lightArtwork.isNull()) {
        return lightArtwork;
    }

    // The placeholder artwork is three flat slate tones with antialiased edges
    // between them, so it recolours by lightness alone: each source tone has a
    // dark counterpart, and anything in between is interpolated, which keeps
    // the edges smooth instead of banding them.
    struct Anchor {
        int lightness;
        int r;
        int g;
        int b;
    };
    static constexpr Anchor kAnchors[] = {
        {214, 0x47, 0x55, 0x69},  // #cbd5e1, the mark      -> slate-600
        {231, 0x33, 0x41, 0x55},  // #e2e8f0, the text bars -> slate-700
        {244, 0x16, 0x22, 0x3a},  // #f1f5f9, the card      -> placeholderBg
    };
    constexpr int kAnchorCount = sizeof(kAnchors) / sizeof(kAnchors[0]);

    QImage image = lightArtwork.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QColor source = QColor::fromRgba(line[x]);
            const int lightness = source.lightness();

            int index = 0;
            while (index < kAnchorCount - 1 && lightness > kAnchors[index + 1].lightness) {
                ++index;
            }

            int r = kAnchors[index].r;
            int g = kAnchors[index].g;
            int b = kAnchors[index].b;
            if (index < kAnchorCount - 1 && lightness > kAnchors[index].lightness) {
                const Anchor& low = kAnchors[index];
                const Anchor& high = kAnchors[index + 1];
                const int span = high.lightness - low.lightness;
                const int offset = lightness - low.lightness;
                r = low.r + (high.r - low.r) * offset / span;
                g = low.g + (high.g - low.g) * offset / span;
                b = low.b + (high.b - low.b) * offset / span;
            }

            line[x] = qRgba(r, g, b, qAlpha(line[x]));
        }
    }

    QPixmap recoloured = QPixmap::fromImage(image);
    recoloured.setDevicePixelRatio(lightArtwork.devicePixelRatio());
    return recoloured;
}

void setupFonts(QApplication& app)
{
    loadFontResource(":/fonts/Cairo-Variable.ttf");

    QFont defaultFont(kFontFamily);
    defaultFont.setPixelSize(15);
    defaultFont.setWeight(QFont::Normal);
    app.setFont(defaultFont);
}

QColor copyAvailableColor()
{
    return copyAvailableColor(Theme::mode());
}

QColor copyAvailableColor(const ThemeMode mode)
{
    return QColor(QString::fromLatin1(
        (mode == ThemeMode::Dark ? kDarkPalette : kLightPalette).copyAvailable));
}

QColor copyOnLoanColor()
{
    return copyOnLoanColor(Theme::mode());
}

QColor copyOnLoanColor(const ThemeMode mode)
{
    return QColor(QString::fromLatin1(
        (mode == ThemeMode::Dark ? kDarkPalette : kLightPalette).copyOnLoan));
}

QString applicationStylesheet()
{
    return applicationStylesheet(Theme::mode());
}

QString applicationStylesheet(ThemeMode mode)
{
    return paint(styleSheetTemplate(), mode == ThemeMode::Dark ? kDarkPalette : kLightPalette);
}

QPalette applicationPalette()
{
    return applicationPalette(Theme::mode());
}

QPalette applicationPalette(ThemeMode mode)
{
    // The stylesheet is not the whole story. A stylesheet only reaches what a
    // rule names, and a widget Qt builds for itself -- the viewport inside a
    // scroll area, the pop-up list of a combo box, the file dialog's plumbing
    // -- is drawn from the palette. Left at the system default that palette is
    // light on every platform the librarians run, which is why dark mode had
    // white rectangles in the middle of it.
    const Palette& colours = (mode == ThemeMode::Dark) ? kDarkPalette : kLightPalette;
    const auto colour = [](const char* value) { return QColor(QString::fromLatin1(value)); };

    QPalette palette;
    palette.setColor(QPalette::Window, colour(colours.windowBg));
    palette.setColor(QPalette::WindowText, colour(colours.textPrimary));
    palette.setColor(QPalette::Base, colour(colours.surface));
    palette.setColor(QPalette::AlternateBase, colour(colours.alternateBg));
    palette.setColor(QPalette::Text, colour(colours.textPrimary));
    palette.setColor(QPalette::PlaceholderText, colour(colours.textDisabled));
    palette.setColor(QPalette::Button, colour(colours.surfaceMuted));
    palette.setColor(QPalette::ButtonText, colour(colours.textPrimary));
    palette.setColor(QPalette::BrightText, colour(colours.alertFg));
    palette.setColor(QPalette::ToolTipBase, colour(colours.surface));
    palette.setColor(QPalette::ToolTipText, colour(colours.textPrimary));
    palette.setColor(QPalette::Highlight, colour(colours.selectionBg));
    palette.setColor(QPalette::HighlightedText, colour(colours.textPrimary));
    palette.setColor(QPalette::Link, colour(colours.link));
    palette.setColor(QPalette::LinkVisited, colour(colours.link));
    palette.setColor(QPalette::Light, colour(colours.surfaceMuted));
    palette.setColor(QPalette::Midlight, colour(colours.border));
    palette.setColor(QPalette::Mid, colour(colours.border));
    palette.setColor(QPalette::Dark, colour(colours.gridLine));
    palette.setColor(QPalette::Shadow, colour(colours.windowBg));

    // The disabled group is its own set of colours, not a dimming of the
    // above: leave it out and Qt keeps the system's grey-on-white for every
    // greyed-out field.
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, colour(colours.textDisabled));
    }
    palette.setColor(QPalette::Disabled, QPalette::Base, colour(colours.disabledBg));
    palette.setColor(QPalette::Disabled, QPalette::Button, colour(colours.disabledBg));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, colour(colours.disabledBg));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, colour(colours.textDisabled));

    return palette;
}

}  // namespace VLMS
