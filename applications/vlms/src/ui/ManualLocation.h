#pragma once

#include <QRect>
#include <QString>
#include <QStringList>

namespace VLMS {

/// The manual page to open for `locale`: the first of
/// <projectRoot>/docs/manual/<locale>/index.html, <appDir>/manual/<locale>/index.html,
/// the same two for "ar", then <projectRoot>/docs/manual/index.html and
/// <appDir>/manual/index.html. Empty when none exists.
[[nodiscard]] QString manualIndexPath(const QString& projectRoot,
                                      const QString& appDir,
                                      const QString& locale);

struct ManualBrowserCommand {
    QString program;
    QStringList arguments;
};

/// Ordered browsers for one platform. `windows` false is the Linux list and
/// ignores the two directories. `windows` true is Edge under each Program Files
/// directory, then the bare name `msedge`. Existence is not checked here.
[[nodiscard]] QStringList manualBrowserCandidates(bool windows,
                                                  const QString& programFiles,
                                                  const QString& programFilesX86);

/// Where the manual window sits inside `available`, the usable rectangle of
/// the screen the application is on (`QScreen::availableGeometry`). A large
/// screen keeps a 1200×800 reading size, centred. A shorter or narrower
/// screen shrinks it so the window stays 32px clear of each edge. That inset
/// covers the browser frame: `--window-size` on an `--app` window is the
/// content box, and the title bar sits outside it. An area too small for the
/// inset returns `available` itself. An invalid rectangle returns a null rect.
[[nodiscard]] QRect manualWindowRect(const QRect& available);

/// The first entry of `browsers`, and the arguments that open `indexPath`
/// in the private profile. An empty list returns an empty program. A valid
/// `window` appends `--window-position` and `--window-size`; a null rect
/// leaves the four profile arguments alone.
[[nodiscard]] ManualBrowserCommand manualBrowserLaunch(const QStringList& browsers,
                                                       const QString& indexPath,
                                                       const QString& profileDir,
                                                       const QRect& window = {});

}  // namespace VLMS
