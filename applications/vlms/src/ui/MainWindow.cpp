#include "ui/MainWindow.h"

#include "Application.h"
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Paths.h>
#include <VLMS/Core/Strings.h>
#include "ui/Theme.h"
#include "ui/ClickableLabel.h"
#include "ui/LicenceDialog.h"
#include "ui/ManualLocation.h"
#include "ui/UiHelpers.h"
#include "QtBridge.h"
#include "ui/archive/ArchivePage.h"
#include "ui/catalog/CatalogPage.h"
#include "ui/LanguageSelector.h"
#include "ui/circulation/CirculationPage.h"
#include "ui/members/MembersPage.h"
#include "ui/metrics/MetricsPage.h"

#include <QApplication>
#include <QDate>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

using VLMS::T;
using VLMS::cd;
using VLMS::qd;
using VLMS::qs;
using VLMS::qsl;
using VLMS::ss;
using VLMS::svl;

namespace {
// Breathing room above and below the tallest thing in the header row, which is
// the brand banner.
constexpr int kHeaderPadding = 8;

QStringList existingBrowsers(const QStringList& candidates)
{
    QStringList kept;
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isAbsolute()) {
            if (info.exists()) {
                kept << candidate;
            }
        } else {
            const QString found = QStandardPaths::findExecutable(candidate);
            if (!found.isEmpty()) {
                kept << found;
            }
        }
    }
    return kept;
}
}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    buildUi();

    if (auto* app = qobject_cast<Application*>(QApplication::instance())) {
        connect(app, &Application::languageChanged, this, &MainWindow::retranslateUi);
        connect(app, &Application::themeChanged, this, &MainWindow::onThemeChanged);
    }

    showPage(Page::Catalog);
    retranslateUi();
}

void MainWindow::buildUi() {
    setMinimumSize(1000, 640);
    resize(1280, 720);

    m_centralRoot = new QWidget(this);
    m_centralRoot->setObjectName(QStringLiteral("centralRoot"));
    setCentralWidget(m_centralRoot);

    auto* rootLayout = new QVBoxLayout(m_centralRoot);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* header = new QWidget(m_centralRoot);
    header->setObjectName(QStringLiteral("appHeader"));
    // Fixed vertically, so the row is always exactly as tall as what it holds.
    // Left to Preferred it is the first thing the column shrinks when the pages
    // below ask for more height than the window has -- which is most of the
    // time -- and the brand banner, the tallest thing in the row, gets clipped.
    header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, kHeaderPadding, 16, kHeaderPadding);
    headerLayout->setSpacing(8);

    m_brandBanner = new QLabel(header);
    m_brandBanner->setObjectName(QStringLiteral("brandBanner"));
    m_brandBanner->setAttribute(Qt::WA_TransparentForMouseEvents);
    headerLayout->addWidget(m_brandBanner);
    headerLayout->addSpacing(16);

    m_catalogNav = VLMS::makeNavButton({});
    m_membersNav = VLMS::makeNavButton({});
    m_circulationNav = VLMS::makeNavButton({});
    m_archiveNav = VLMS::makeNavButton({});
    // makeNavButton names every nav button navLink, which Theme.cpp styles
    // (QPushButton#navLink); a property identifies this one without unstyling it.
    m_archiveNav->setProperty("navKey", QStringLiteral("archive"));
    m_metricsNav = VLMS::makeNavButton({});

    for (QPushButton* button :
         {m_catalogNav, m_membersNav, m_circulationNav, m_archiveNav, m_metricsNav}) {
        headerLayout->addWidget(button);
    }

    headerLayout->addStretch();

    m_languageSelector = new VLMS::LanguageSelector(header);
    headerLayout->addWidget(m_languageSelector);

    // Next to the language selector, because both are "how the application is
    // presented" rather than anything to do with the library's records.
    m_themeToggle = new QPushButton(header);
    m_themeToggle->setObjectName(QStringLiteral("themeToggle"));
    m_themeToggle->setCursor(Qt::PointingHandCursor);
    m_themeToggle->setFocusPolicy(Qt::StrongFocus);
    headerLayout->addWidget(m_themeToggle);

    m_manualButton = new QPushButton(header);
    m_manualButton->setObjectName(QStringLiteral("manualButton"));
    m_manualButton->setCursor(Qt::PointingHandCursor);
    m_manualButton->setFocusPolicy(Qt::StrongFocus);
    m_manualButton->setText({});
    headerLayout->addWidget(m_manualButton);

    connect(m_themeToggle, &QPushButton::clicked, this, []() {
        if (auto* app = qobject_cast<Application*>(QApplication::instance())) {
            app->toggleTheme();
        }
    });

    connect(m_membersNav, &QPushButton::clicked, this, [this]() { showPage(Page::Members); });
    connect(m_catalogNav, &QPushButton::clicked, this, [this]() { showPage(Page::Catalog); });
    connect(m_circulationNav, &QPushButton::clicked, this, [this]() { showPage(Page::Circulation); });
    connect(m_archiveNav, &QPushButton::clicked, this, [this]() { showPage(Page::Archive); });
    connect(m_metricsNav, &QPushButton::clicked, this, [this]() { showPage(Page::Metrics); });
    connect(m_manualButton, &QPushButton::clicked, this, [this]() {
        const QString path = VLMS::manualIndexPath(
            VLMS::qs(VLMS::Core::Paths::projectRoot()),
            QCoreApplication::applicationDirPath(),
            VLMS::qs(VLMS::Core::Locale::code()));
        if (path.isEmpty()) {
            VLMS::showWarning(this, T("nav.help"), T("help.notFound"));
            return;
        }
#if defined(Q_OS_WIN)
        constexpr bool kWindows = true;
#else
        constexpr bool kWindows = false;
#endif
        const QStringList browsers = existingBrowsers(VLMS::manualBrowserCandidates(
            kWindows,
            qEnvironmentVariable("ProgramFiles"),
            qEnvironmentVariable("ProgramFiles(x86)")));
        const QString profile = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
            + QStringLiteral("/manual-browser");
        QScreen* screen = this->screen();
        if (screen == nullptr) {
            screen = QApplication::primaryScreen();
        }
        const QRect available = screen != nullptr ? screen->availableGeometry() : QRect();
        const VLMS::ManualBrowserCommand command = VLMS::manualBrowserLaunch(
            browsers, path, profile, VLMS::manualWindowRect(available));
        if (command.program.isEmpty()
            || !QProcess::startDetached(command.program, command.arguments)) {
            VLMS::showWarning(this, T("nav.help"), T("help.noBrowser"));
        }
    });
    connect(m_languageSelector, &VLMS::LanguageSelector::languageSelected,
            this, &MainWindow::onLanguageChanged);

    rootLayout->addWidget(header);

    auto* mainHost = new QWidget(m_centralRoot);
    m_contentLayout = new QVBoxLayout(mainHost);
    m_contentLayout->setContentsMargins(24, 16, 24, 16);
    m_contentLayout->setSpacing(0);

    m_stack = new QStackedWidget(mainHost);
    if (auto* app = qobject_cast<Application*>(QApplication::instance())) {
        m_membersPage = new MembersPage(app->members(), app->circulation(), m_stack);
        m_catalogPage = new CatalogPage(app->catalog(), app->circulation(), m_stack);
        m_circulationPage =
            new CirculationPage(app->circulation(), app->catalog(), app->members(), m_stack);
        m_archivePage = new ArchivePage(app->members(), app->catalog(), app->circulation(), m_stack);
        m_metricsPage = new MetricsPage(app->metrics(), m_stack);
    } else {
        m_membersPage = nullptr;
        m_catalogPage = nullptr;
        m_circulationPage = nullptr;
        m_archivePage = nullptr;
        m_metricsPage = nullptr;
    }

    // Both pages have to exist for this: the members page sends the librarian
    // to the circulation page when a member cannot be removed until their books
    // come back. Under the placeholder branch above there is nowhere to send
    // them, and the members page will not be raising it either.
    if (m_membersPage != nullptr && m_circulationPage != nullptr) {
        connect(m_membersPage,
                &MembersPage::memberLoansRequested,
                this,
                [this](const QString& membershipNumber) {
                    showPage(Page::Circulation);
                    m_circulationPage->focusMemberLoans(membershipNumber);
                });
    }

    if (m_catalogPage != nullptr) {
        m_stack->addWidget(m_catalogPage);
    } else {
        m_stack->addWidget(buildPlaceholderPage(
            QStringLiteral("page.catalog.title"),
            QStringLiteral("page.catalog.body")));
    }
    if (m_membersPage != nullptr) {
        m_stack->addWidget(m_membersPage);
    } else {
        m_stack->addWidget(buildPlaceholderPage(
            QStringLiteral("page.members.title"),
            QStringLiteral("page.members.body")));
    }
    if (m_circulationPage != nullptr) {
        m_stack->addWidget(m_circulationPage);
    } else {
        m_stack->addWidget(buildPlaceholderPage(
            QStringLiteral("page.circulation.title"),
            QStringLiteral("page.circulation.body")));
    }
    if (m_archivePage != nullptr) {
        m_stack->addWidget(m_archivePage);
        // A restore changes what the live pages list; each page's
        // retranslateUi re-reads its list, which is the refresh they expose.
        connect(m_archivePage, &ArchivePage::recordRestored, this, [this]() {
            if (m_catalogPage != nullptr) {
                m_catalogPage->retranslateUi();
            }
            if (m_membersPage != nullptr) {
                m_membersPage->retranslateUi();
            }
            if (m_circulationPage != nullptr) {
                m_circulationPage->retranslateUi();
            }
        });
    } else {
        m_stack->addWidget(buildPlaceholderPage(
            QStringLiteral("page.archive.title"),
            QStringLiteral("page.archive.body")));
    }
    if (m_metricsPage != nullptr) {
        m_stack->addWidget(m_metricsPage);
    } else {
        m_stack->addWidget(buildPlaceholderPage(
            QStringLiteral("page.metrics.title"),
            QStringLiteral("page.metrics.body")));
    }

    m_contentLayout->addWidget(m_stack, 1);
    rootLayout->addWidget(mainHost, 1);

    auto* footer = new QWidget(m_centralRoot);
    footer->setObjectName(QStringLiteral("appFooter"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 12, 16, 12);
    m_footerLabel = new VLMS::ClickableLabel(footer);
    connect(m_footerLabel, &VLMS::ClickableLabel::clicked, this, [this]() {
        LicenceDialog(this).exec();
    });
    footerLayout->addWidget(m_footerLabel);
    // Without this the label is the only item in the row and takes the whole
    // bar, so the pointing hand and the licence click land anywhere along an
    // otherwise empty footer. The label is meant to be the target, not the bar.
    footerLayout->addStretch(1);
    rootLayout->addWidget(footer);
}

void MainWindow::updateBrandBanner()
{
    // One banner per UI language, drawn as artwork rather than set as text:
    // the wordmark is the library's, not something a font substitution should
    // be allowed to reinterpret.
    // Sized so the wordmark reads at the same size as a page title: 26 px of
    // bold Cairo, which is 18 px of cap height and a 26 px ink band in Arabic.
    // The lettering is only about a sixth of the artwork's height -- the rest
    // is the mark and the padding around it -- so matching a 26 px title takes
    // a banner this tall, and the header grows to suit.
    static constexpr int kBannerHeight = 105;

    const QString code = qs(VLMS::Core::Locale::code());
    const QString language =
        (code == QLatin1String("ar") || code == QLatin1String("fr")) ? code
                                                                    : QStringLiteral("en");

    // The @2x master is the one that gets scaled: downsampling keeps the thin
    // strokes of the mark intact where upscaling the 1x would smear them.
    const QString variant = VLMS::Theme::isDark() ? QStringLiteral("-dark") : QString();
    const QPixmap source(
        QStringLiteral(":/brand/banner-%1%2@2x.png").arg(language, variant));
    if (source.isNull()) {
        return;
    }

    const qreal ratio = m_brandBanner->devicePixelRatioF();
    QPixmap scaled = source.scaledToHeight(
        qRound(kBannerHeight * ratio), Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(ratio);
    m_brandBanner->setPixmap(scaled);
    m_brandBanner->setAccessibleName(VLMS::T("app.title"));

    // A QLabel does not scale a pixmap to fit -- it clips it. Pinning the label
    // to the artwork's exact size puts the artwork into the header's layout
    // hint, which the header's Fixed vertical policy then holds on to. Asking
    // the header for a minimum height instead does not survive: the sheet's
    // "QWidget#appHeader { min-height }" is written back onto the widget on
    // every repolish, and the row went back to clipping the mark.
    m_brandBanner->setFixedSize(scaled.deviceIndependentSize().toSize());
}

QWidget* MainWindow::buildPlaceholderPage(const QString& titleKey, const QString& bodyKey)
{
    auto* page = new QWidget(m_stack);
    auto* layout = new QVBoxLayout(page);
    VLMS::configurePageLayout(layout);

    auto* header = VLMS::makePageHeader({}, {}, page);
    if (auto* title = header->findChild<QLabel*>(QStringLiteral("pageTitle"))) {
        title->setProperty("i18nKey", titleKey);
    }
    if (auto* body = header->findChild<QLabel*>(QStringLiteral("pageSubtitle"))) {
        body->setProperty("i18nKey", bodyKey);
    }

    layout->addWidget(header);
    layout->addStretch();
    return page;
}

void MainWindow::showPage(Page page)
{
    m_activePage = page;
    int index = 0;
    switch (page) {
    case Page::Catalog:
        index = 0;
        break;
    case Page::Members:
        index = 1;
        break;
    case Page::Circulation:
        index = 2;
        break;
    case Page::Archive:
        index = 3;
        break;
    case Page::Metrics:
        index = 4;
        break;
    }
    m_stack->setCurrentIndex(index);
    if (page == Page::Archive && m_archivePage != nullptr) {
        m_archivePage->refresh();
    }
    if (page == Page::Metrics && m_metricsPage != nullptr) {
        m_metricsPage->refreshMetrics();
    }
    updateNavigation(page);
}

void MainWindow::updateNavigation(Page active)
{
    auto setActive = [](QPushButton* button, bool isActive) {
        button->setProperty("active", isActive);
        VLMS::refreshWidgetStyle(button);
    };

    setActive(m_membersNav, active == Page::Members);
    setActive(m_catalogNav, active == Page::Catalog);
    setActive(m_circulationNav, active == Page::Circulation);
    setActive(m_archiveNav, active == Page::Archive);
    setActive(m_metricsNav, active == Page::Metrics);
}

void MainWindow::onLanguageChanged(const QString& code)
{
    auto* app = qobject_cast<Application*>(QApplication::instance());
    if (app == nullptr) {
        return;
    }

    if (code.isEmpty() || code == app->uiLocale()) {
        return;
    }

    app->setUiLocale(code);
}

void MainWindow::onThemeChanged()
{
    // The stylesheet has already been swapped application-wide. What is left is
    // everything the stylesheet cannot reach: the brand banner, the cover
    // placeholders the pages painted from light-mode artwork, and the label
    // naming the mode the button switches to. retranslateUi refreshes all
    // three, because a language switch has exactly the same problem.
    retranslateUi();
}

void MainWindow::updateThemeToggle()
{
    if (m_themeToggle == nullptr) {
        return;
    }

    // The round button carries no text, so the symbol names the mode in use --
    // sun while the window is light, crescent while it is dark. What clicking
    // does is left to the tooltip, which is the only part that can say it in
    // words, and to the accessible name, which is all a screen reader gets.
    static constexpr int kIconSize = 18;

    const bool dark = VLMS::Theme::isDark();
    m_themeToggle->setText({});
    m_themeToggle->setIcon(VLMS::themeModeIcon(
        VLMS::Theme::mode(), kIconSize, m_themeToggle->devicePixelRatioF()));
    m_themeToggle->setIconSize(QSize(kIconSize, kIconSize));

    const QString action = T(dark ? "theme.toLight" : "theme.toDark");
    m_themeToggle->setToolTip(action);
    m_themeToggle->setAccessibleName(T("theme.label"));
    m_themeToggle->setAccessibleDescription(action);
}

void MainWindow::updateManualButton()
{
    if (m_manualButton == nullptr) {
        return;
    }

    static constexpr int kIconSize = 18;
    m_manualButton->setText({});
    m_manualButton->setIcon(VLMS::manualButtonIcon(
        VLMS::Theme::mode(), kIconSize, m_manualButton->devicePixelRatioF(),
        VLMS::Core::Locale::isRtl() ? Qt::RightToLeft : Qt::LeftToRight));
    m_manualButton->setIconSize(QSize(kIconSize, kIconSize));
    m_manualButton->setToolTip(T("help.tooltip"));
    m_manualButton->setAccessibleName(T("nav.help"));
}

void MainWindow::retranslateUi()
{
    setWindowTitle(T("app.title"));

    updateBrandBanner();

    m_membersNav->setText(T("nav.members"));
    m_catalogNav->setText(T("nav.catalog"));
    m_circulationNav->setText(T("nav.circulation"));
    m_archiveNav->setText(T("nav.archive"));
    m_metricsNav->setText(T("nav.metrics"));

    updateThemeToggle();
    updateManualButton();

    m_languageSelector->setCurrentLanguage(VLMS::qs(VLMS::Core::Locale::code()));
    m_languageSelector->retranslateUi();

    m_footerLabel->setText(T("footer.copyright", "year",
                             std::to_string(VLMS::Core::Clock::today().year())));
    // The label keeps its appearance -- no underline, no hover change. The
    // pointing hand (set by ClickableLabel) and this tooltip are the only
    // cues that it opens anything.
    m_footerLabel->setToolTip(T("licence.tooltip"));

    const auto retranslatePlaceholder = [&](QWidget* page) {
        if (page == nullptr) {
            return;
        }
        for (QLabel* label : page->findChildren<QLabel*>()) {
            const QString key = label->property("i18nKey").toString();
            if (!key.isEmpty()) {
                label->setText(T(ss(key)));
            }
        }
    };

    if (m_metricsPage != nullptr) {
        m_metricsPage->retranslateUi();
    } else {
        retranslatePlaceholder(m_stack->widget(4));
    }

    if (m_membersPage != nullptr) {
        m_membersPage->retranslateUi();
    }
    if (m_catalogPage != nullptr) {
        m_catalogPage->retranslateUi();
    }
    if (m_circulationPage != nullptr) {
        m_circulationPage->retranslateUi();
    }
    if (m_archivePage != nullptr) {
        m_archivePage->retranslateUi();
    } else {
        retranslatePlaceholder(m_stack->widget(3));
    }

    updateNavigation(m_activePage);
}
