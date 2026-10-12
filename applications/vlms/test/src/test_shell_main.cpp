// The shell suite runs the real composition root. Application opens the
// database under Paths::projectRoot(), which defaults to the source tree, so
// the root moves to a temporary folder before Application exists.

#include "ui/shell/Application.h"

#include <VLMS/Core/Paths.h>

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdio>

int main(int argc, char** argv)
{
    QTemporaryDir root;
    if (!root.isValid()) {
        std::fprintf(stderr, "no temporary folder for the shell suite\n");
        return 1;
    }
    VLMS::Core::Paths::setProjectRoot(root.path().toStdString());
    QStandardPaths::setTestModeEnabled(true);
    const QString settings = root.filePath(QStringLiteral("settings"));
    QDir().mkpath(settings);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings);
    QSettings::setDefaultFormat(QSettings::IniFormat);

    Application app(argc, argv);
    if (!app.isDatabaseReady()) {
        std::fprintf(stderr, "the shell suite could not open its database\n");
        return 1;
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
