#pragma once

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QString>

class QApplication;

namespace VLMS {

enum class ThemeMode { Light, Dark };

/// The colour scheme the whole application is painted in. Held here rather
/// than on Application so the stylesheet builder and the widgets that pick a
/// light or dark asset can both read it without reaching for the QApplication.
class Theme {
public:
    static constexpr ThemeMode kDefaultMode = ThemeMode::Light;

    static ThemeMode mode();
    static void setMode(ThemeMode mode);
    static bool isDark();
    static void loadSaved();
    static void save();

private:
    static ThemeMode s_mode;
};

/// The sun (light) or crescent (dark) drawn for the mode toggle, painted
/// rather than typed: neither glyph is in Cairo, and a font substitution for
/// a single character is not something to leave to the system.
/// \param ratio the target widget's device pixel ratio.
QIcon themeModeIcon(ThemeMode mode, int pixelSize, qreal ratio = 1.0);

/// The "?" drawn on the manual button, in the same muted ink as themeModeIcon.
/// Painted rather than set as button text, so the circle's stylesheet never
/// has to centre a glyph. \param ratio the target widget's device pixel ratio.
/// Right to left it is the Arabic question mark "؟", which faces the other way.
QIcon manualButtonIcon(ThemeMode mode,
                       int pixelSize,
                       qreal ratio = 1.0,
                       Qt::LayoutDirection direction = Qt::LeftToRight);

/// The placeholder artwork drawn for a book with no cover, and anything else
/// shipped as light-mode slate, recoloured for the mode in use. Returns the
/// pixmap untouched in light mode.
QPixmap themedArtwork(const QPixmap& lightArtwork);

/// The colour a copy's local accession number is written in: green for a copy
/// on the shelf, red for one out on an unreturned loan. Both pairs clear 4.5:1
/// against the surface and against the selection background in their mode, so
/// the number keeps its colour on a selected row rather than falling back to
/// HighlightedText -- a number that turned slate the moment the row was picked
/// would say nothing.
QColor copyAvailableColor();
QColor copyAvailableColor(ThemeMode mode);
QColor copyOnLoanColor();
QColor copyOnLoanColor(ThemeMode mode);

void setupFonts(QApplication& app);
/// The stylesheet for the mode currently set on Theme.
QString applicationStylesheet();
QString applicationStylesheet(ThemeMode mode);

/// The colours for everything the stylesheet cannot name: scroll-area
/// viewports, combo-box pop-ups, the file dialog's own widgets. Applied
/// alongside the stylesheet, never instead of it.
QPalette applicationPalette();
QPalette applicationPalette(ThemeMode mode);

}  // namespace VLMS
