#include "ui/ManualLocation.h"

#include <QDir>
#include <QFileInfo>
#include <QRect>
#include <QStringList>
#include <QUrl>

namespace VLMS {

QString manualIndexPath(const QString& projectRoot, const QString& appDir, const QString& locale)
{
    QStringList candidates;
    for (const QString& lang : {locale, QStringLiteral("ar")}) {
        candidates << projectRoot + QStringLiteral("/docs/manual/") + lang + QStringLiteral("/index.html")
                   << appDir + QStringLiteral("/manual/") + lang + QStringLiteral("/index.html");
    }
    candidates << projectRoot + QStringLiteral("/docs/manual/index.html")
               << appDir + QStringLiteral("/manual/index.html");
    for (const QString& path : candidates) {
        if (!projectRoot.isEmpty() || !path.startsWith(QStringLiteral("/docs/"))) {
            if (QFileInfo::exists(path)) {
                return path;
            }
        }
    }
    return {};
}

QStringList manualBrowserCandidates(bool windows,
                                    const QString& programFiles,
                                    const QString& programFilesX86)
{
    if (!windows) {
        return {QStringLiteral("chromium"),
                QStringLiteral("chromium-browser"),
                QStringLiteral("google-chrome"),
                QStringLiteral("google-chrome-stable")};
    }
    return {QDir(programFiles).filePath(QStringLiteral("Microsoft/Edge/Application/msedge.exe")),
            QDir(programFilesX86).filePath(QStringLiteral("Microsoft/Edge/Application/msedge.exe")),
            QStringLiteral("msedge")};
}

QRect manualWindowRect(const QRect& available)
{
    // Reading size on a normal desktop. The manual's two columns start at 820px.
    constexpr int kPreferredWidth = 1200;
    constexpr int kPreferredHeight = 800;
    // `--window-size` is the content box of an `--app` window. The frame,
    // mostly the title bar, is drawn outside that box, so the inset has to
    // leave room for it or a 720px-tall screen clips the window.
    constexpr int kMargin = 32;
    if (!available.isValid() || available.width() <= 0 || available.height() <= 0) {
        return {};
    }
    if (available.width() <= 2 * kMargin || available.height() <= 2 * kMargin) {
        return available;
    }
    const int width = qMin(kPreferredWidth, available.width() - 2 * kMargin);
    const int height = qMin(kPreferredHeight, available.height() - 2 * kMargin);
    const int x = available.x() + (available.width() - width) / 2;
    const int y = available.y() + (available.height() - height) / 2;
    return QRect(x, y, width, height);
}

ManualBrowserCommand manualBrowserLaunch(const QStringList& browsers,
                                         const QString& indexPath,
                                         const QString& profileDir,
                                         const QRect& window)
{
    if (browsers.isEmpty()) {
        return {};
    }
    const QString url = QUrl::fromLocalFile(indexPath).toString(QUrl::FullyEncoded);
    ManualBrowserCommand command;
    command.program = browsers.first();
    command.arguments = {QStringLiteral("--user-data-dir=") + profileDir,
                         QStringLiteral("--app=") + url,
                         QStringLiteral("--no-first-run"),
                         QStringLiteral("--disable-extensions")};
    if (window.isValid() && window.width() > 0 && window.height() > 0) {
        command.arguments << QStringLiteral("--window-position=%1,%2").arg(window.x()).arg(window.y())
                          << QStringLiteral("--window-size=%1,%2").arg(window.width()).arg(window.height());
    }
    return command;
}

}  // namespace VLMS
