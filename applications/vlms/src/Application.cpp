#include "Application.h"

#include "QtBridge.h"
#include "ui/Theme.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Database/Database.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MetricsRepository.h>
#include <VLMS/Core/Paths.h>

#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QPixmap>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>

using VLMS::T;
using VLMS::cd;
using VLMS::qd;
using VLMS::qs;
using VLMS::qsl;
using VLMS::ss;
using VLMS::svl;

#ifndef VLMS_VERSION
// Set by the build from project(VLMS VERSION ...). The fallback only
// applies to a build that compiles this file outside CMake.
#define VLMS_VERSION "0.0.0-dev"
#endif

namespace {

void loadLocale()
{
    QSettings settings;
    const QString stored = settings.value(QStringLiteral("ui/locale"),
                                          QString::fromLatin1(VLMS::Locale::kDefaultCode))
                               .toString();
    VLMS::Locale::setCode(VLMS::ss(stored));
}

void saveLocale()
{
    QSettings settings;
    settings.setValue(QStringLiteral("ui/locale"), VLMS::qs(VLMS::Locale::code()));
}

void applyLocale(QGuiApplication* app)
{
    app->setLayoutDirection(VLMS::Locale::isRtl() ? Qt::RightToLeft : Qt::LeftToRight);

    // A widget takes its QLocale from the default when it is built, and the
    // calendar pop-up names its months and weekdays from that. Left alone, the
    // default is the operating system's, so an Arabic session showed a German
    // or French calendar. Tunisian Arabic, not plain Arabic: it writes Latin
    // digits, as everything else in the application does.
    const std::string code = VLMS::Locale::code();
    if (code == "fr") {
        QLocale::setDefault(QLocale(QLocale::French, QLocale::France));
    } else if (code == "en") {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    } else {
        QLocale::setDefault(QLocale(QLocale::Arabic, QLocale::Tunisia));
    }
}

}  // namespace

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv) {
    setApplicationName(QStringLiteral("VLMS"));
    setOrganizationName(QStringLiteral("VLMS"));
    setApplicationVersion(QStringLiteral(VLMS_VERSION));

    // Every size the mark was rendered at, in one icon: the window manager
    // picks the title bar one, the task bar and the alt-tab switcher pick a
    // larger one, and none of them has to rescale.
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/brand/icon-%1.png").arg(size), QSize(size, size));
    }
    setWindowIcon(icon);
    // Wayland has no per-window icon protocol: the shell matches the window to
    // a .desktop file by this name and takes the icon from there.
    setDesktopFileName(QStringLiteral("vlms"));

    // Fusion, not the platform default. The Windows style draws a spin box,
    // a tab, a check box and a scroll bar with the operating system's own
    // light chrome and ignores most of what a stylesheet asks for, so dark
    // mode there came out as light controls on a dark window. Fusion honours
    // the palette and the sheet, and looks the same on both platforms.
    // Guarded: setStyle takes ownership of what it is given, and create()
    // returns null for a style this Qt was built without.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        setStyle(fusion);
    }

    if (VLMS::Paths::projectRoot().empty()) {
        VLMS::Paths::setProjectRoot(applicationDirPath().toStdString());
    }

    loadLocale();
    applyLocale(this);
    applyQtCatalogue();
    VLMS::setupFonts(*this);
    VLMS::Theme::loadSaved();
    applyTheme();

    VLMS::Paths::ensureLayout();

    m_database = new Database(VLMS::Paths::databaseDirectory());
    // The result was discarded here until D2 gave open() a reason to refuse on
    // purpose -- a database written by a newer build. Carrying on regardless
    // means every page renders empty and every action fails with a blank
    // dialog, which is a worse way to learn about it than being told.
    m_databaseReady = m_database->open();
    if (!m_databaseReady) {
        // session() is unset until open() succeeds. Building repositories here
        // would dereference a null unique_ptr before main() can show the
        // "database unavailable" dialog.
        return;
    }

    m_catalogRepository = new VLMS::Repositories::CatalogRepository(m_database->session(),
                                                VLMS::Paths::resourcesDirectory());
    m_memberRepository = new VLMS::Repositories::MemberRepository(m_database->session(),
                                              VLMS::Paths::resourcesDirectory());
    m_circulationRepository = new VLMS::Repositories::CirculationRepository(m_database->session());
    m_metricsRepository = new VLMS::Repositories::MetricsRepository(m_database->session());
}

Application::~Application() {
    // Repositories go first: they hold a session reference and must not
    // outlive Database. Database itself is no longer a QObject child.
    delete m_metricsRepository;
    m_metricsRepository = nullptr;
    delete m_circulationRepository;
    m_circulationRepository = nullptr;
    delete m_memberRepository;
    m_memberRepository = nullptr;
    delete m_catalogRepository;
    m_catalogRepository = nullptr;

    delete m_database;
    m_database = nullptr;
}

QString Application::uiLocale() const
{
    return VLMS::qs(VLMS::Locale::code());
}

void Application::setUiLocale(const QString& code)
{
    if (code == uiLocale()) {
        return;
    }

    VLMS::Locale::setCode(VLMS::ss(code));
    saveLocale();
    applyLocale(this);
    applyQtCatalogue();
    emit languageChanged();
}

void Application::applyQtCatalogue()
{
    // Not every string on screen is one this application wrote. A stock Qt
    // widget -- the file picker's toolbar and column headers above all --
    // speaks out of Qt's own catalogue, which is embedded as a resource by
    // cmake/QtCatalogue.cmake. Without this the picker was an English window
    // in the middle of an Arabic application.
    removeTranslator(&m_qtTranslator);

    // English is what Qt's sources are written in, so there is nothing to load
    // for it, and no catalogue is shipped.
    const QString locale = uiLocale();
    if (locale == QLatin1String("en")) {
        return;
    }

    if (m_qtTranslator.load(QStringLiteral(":/translations/qtbase_%1.qm").arg(locale))) {
        installTranslator(&m_qtTranslator);
    }
}

bool Application::isDarkTheme() const
{
    return VLMS::Theme::isDark();
}

void Application::setDarkTheme(bool dark)
{
    const auto mode = dark ? VLMS::ThemeMode::Dark : VLMS::ThemeMode::Light;
    if (mode == VLMS::Theme::mode()) {
        return;
    }

    VLMS::Theme::setMode(mode);
    VLMS::Theme::save();
    applyTheme();
    emit themeChanged();
}

void Application::applyTheme()
{
    // The palette first: setStyleSheet repolishes every widget already built,
    // and a widget polished against the old palette would keep the old
    // viewport and pop-up colours until something else touched it.
    setPalette(VLMS::applicationPalette());
    setStyleSheet(VLMS::applicationStylesheet());
}

void Application::toggleTheme()
{
    setDarkTheme(!isDarkTheme());
}
