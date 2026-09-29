#include "Application.h"

#include <QDir>
#include <QFont>
#include <QSettings>
#include <QStandardPaths>

#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    // Application is the QApplication: setUiLocale broadcasts LanguageChange
    // through the real composition root, which a plain QApplication cannot.
    Application app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);

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
