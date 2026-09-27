#include <QApplication>
#include <QDir>
#include <QFont>
#include <QSettings>
#include <QStandardPaths>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

    // Pixel-reading UI tests count red/green ink. Ubuntu's default fontconfig
    // (10-sub-pixel-rgb.conf, the GitHub Actions 24.04 runner) paints LCD
    // fringes that those tests would count as a copy's state colour. This
    // machine's 10-sub-pixel-none.conf does not. Pin grayscale AA so the
    // suite measures what the delegate painted, not the runner's FreeType.
    QFont font = app.font();
    font.setStyleStrategy(static_cast<QFont::StyleStrategy>(
        font.styleStrategy() | QFont::NoSubpixelAntialias));
    app.setFont(font);

    const QString sandbox = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!sandbox.isEmpty()) {
        QDir().mkpath(sandbox);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, sandbox);
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, sandbox);
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
