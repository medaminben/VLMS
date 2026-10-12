#pragma once

#include <QIcon>
#include <QString>

namespace VLMS {

/// The flag for a UI language code -- "ar" (Tunisia), "fr" (France), "en" (the
/// United Kingdom) -- drawn rather than shipped as artwork, the way
/// themeModeIcon draws the sun and crescent. The application links only
/// Core/Gui/Widgets, so an SVG asset would pull the qsvg plugin into every
/// deployment; the geometry is transcribed from the Wikimedia Commons file for
/// each flag instead, all of which are public domain.
///
/// Returns a null icon for any other code.
/// \param pixelSize the LOGICAL size of the icon; the pixmap actually painted
///     is pixelSize * ratio on a side.
/// \param ratio the target widget's device pixel ratio.
/// \param dimmed paints at reduced opacity, for a language that is not in use.
QIcon languageFlagIcon(const QString& code, int pixelSize, qreal ratio = 1.0,
                       bool dimmed = false);

}  // namespace VLMS
