#include "Application.h"
#include "Capture.h"
#include "Shots.h"
#include "ui/FlagIcons.h"
#include "ui/MainWindow.h"

#include <VLMS/Core/Paths.h>

#include "QtBridge.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QSettings>
#include <QTest>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace {

constexpr const char* kMarker = ".vlms-manual-sandbox";

/// Everything here runs before QApplication exists, so it uses plain Qt core only.
int refuse(const QString& why)
{
    std::fprintf(stderr, "manual_capture: refusing: %s\n", qPrintable(why));
    return 4;
}

QString argValue(int argc, char** argv, const char* name)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (qstrcmp(argv[i], name) == 0) {
            return QString::fromLocal8Bit(argv[i + 1]);
        }
    }
    return {};
}

}  // namespace

int main(int argc, char** argv)
{
    if (qEnvironmentVariable("QT_QPA_PLATFORM") != QStringLiteral("offscreen")) {
        return refuse(QStringLiteral("QT_QPA_PLATFORM must be offscreen; this tool never drives a desktop"));
    }
    const QString sandboxArg = argValue(argc, argv, "--sandbox");
    if (sandboxArg.isEmpty()) {
        return refuse(QStringLiteral("--sandbox <dir> is required"));
    }
    const QString sandbox = QFileInfo(sandboxArg).canonicalFilePath();
    if (sandbox.isEmpty() || !QFile::exists(sandbox + QLatin1Char('/') + QLatin1String(kMarker))) {
        return refuse(QStringLiteral("%1 has no %2 marker; run scripts/manual/prepare_sandbox.py")
                          .arg(sandboxArg, QLatin1String(kMarker)));
    }
    const QString sandboxDb = QFileInfo(sandbox + QStringLiteral("/database/vlms.db")).canonicalFilePath();
#ifdef VLMS_PROJECT_ROOT
    const QString liveDb = QFileInfo(QStringLiteral(VLMS_PROJECT_ROOT "/database/vlms.db")).canonicalFilePath();
    if (!liveDb.isEmpty() && sandboxDb == liveDb) {
        return refuse(QStringLiteral("the sandbox database is the live database"));
    }
#endif
    // Before Application: its constructor keeps a non-empty root and opens <root>/database.
    VLMS::Paths::setProjectRoot(sandbox.toStdString());
    // Settings (language, theme) go into the sandbox, never the user's own profile.
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, sandbox + QStringLiteral("/config"));
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, sandbox + QStringLiteral("/config"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    Application app(argc, argv);
    if (!app.isDatabaseReady()) {
        return refuse(QStringLiteral("the sandbox database did not open"));
    }
    if (VLMS::qs(VLMS::Paths::databaseDirectory()) != sandbox + QStringLiteral("/database")) {
        return refuse(QStringLiteral("the project root moved away from the sandbox"));
    }

    QCommandLineParser parser;
    parser.addOption(QCommandLineOption(QStringLiteral("sandbox"), QString(), QStringLiteral("dir")));
    parser.addOption(QCommandLineOption(QStringLiteral("out"), QString(), QStringLiteral("dir")));
    parser.addOption(QCommandLineOption(QStringLiteral("lang"), QString(), QStringLiteral("lang")));
    parser.addOption(QCommandLineOption(QStringLiteral("only"), QString(), QStringLiteral("ids")));
    parser.addOption(QCommandLineOption(QStringLiteral("list")));
    parser.process(app);

    ManualCapture::ShotRegistry registry;
    ManualCapture::registerGeneralShots(registry);
    ManualCapture::registerScreenShots(registry);
    ManualCapture::registerTaskShots(registry);

    if (parser.isSet(QStringLiteral("list"))) {
        for (auto it = registry.cbegin(); it != registry.cend(); ++it) {
            std::fprintf(stdout, "%s\n", qPrintable(it->first));
        }
        return 0;
    }

    const QString out = parser.value(QStringLiteral("out"));
    const QString lang = parser.value(QStringLiteral("lang"));
    if (out.isEmpty() || (lang != QLatin1String("ar") && lang != QLatin1String("en")
                          && lang != QLatin1String("fr"))) {
        std::fprintf(stderr, "manual_capture: --out and --lang ar|en|fr are required\n");
        return 1;
    }

    QStringList only;
    if (parser.isSet(QStringLiteral("only"))) {
        only = parser.value(QStringLiteral("only")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString& id : only) {
            if (!registry.contains(id)) {
                std::fprintf(stderr, "unknown shot %s\n", qPrintable(id));
                return 1;
            }
        }
    }

    QFont font = app.font();
    font.setStyleStrategy(QFont::StyleStrategy(font.styleStrategy() | QFont::NoSubpixelAntialias));
    app.setFont(font);
    app.setUiLocale(lang);
    app.setDarkTheme(false);

    MainWindow window;
    window.resize(1440, 900);
    window.show();
    if (!QTest::qWaitForWindowExposed(&window)) {
        std::fprintf(stderr, "manual_capture: the window was not exposed\n");
        return 3;
    }
    QTest::qWait(800);

    ManualCapture::Capture capture(window, out, lang);
    QStringList failures;
    for (auto it = registry.cbegin(); it != registry.cend(); ++it) {
        if (!only.isEmpty() && !only.contains(it->first)) {
            continue;
        }
        try {
            it->second(capture);
        } catch (const std::exception& exception) {
            failures.append(it->first + QStringLiteral(": ") + QString::fromUtf8(exception.what()));
        }
        try {
            capture.reset();
        } catch (const std::exception& exception) {
            failures.append(it->first + QStringLiteral(" reset: ") + QString::fromUtf8(exception.what()));
        }
    }

    if (!QDir().mkpath(out + QStringLiteral("/flags"))) {
        failures.append(QStringLiteral("could not create the flags directory"));
    } else {
        for (const char* code : {"ar", "en", "fr"}) {
            const QString path = out + QStringLiteral("/flags/") + QLatin1String(code) + QStringLiteral(".png");
            if (!VLMS::languageFlagIcon(QLatin1String(code), 64).pixmap(64, 64).save(path)) {
                failures.append(QStringLiteral("could not save ") + path);
            }
        }
    }

    for (const QString& failure : failures) {
        std::fprintf(stderr, "%s\n", qPrintable(failure));
    }
    return failures.isEmpty() ? 0 : 3;
}
