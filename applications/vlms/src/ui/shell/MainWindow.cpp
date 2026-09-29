#include "ui/shell/MainWindow.h"

#include "ui/workflows/DataEvents.h"
#include "ui/workflows/BulkRunner.h"
#include "ui/workflows/ArchiveWorkflow.h"
#include "ui/workflows/BookWorkflow.h"
#include "ui/workflows/LoanWorkflow.h"
#include "ui/workflows/MemberWorkflow.h"

#include "ui/shell/Application.h"
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/Paths.h>
#include <VLMS/Core/Strings.h>
#include "ui/services/Theme.h"
#include "ui/views/ClickableLabel.h"
#include "ui/views/LicenceDialog.h"
#include "ui/shell/ManualLocation.h"
#include "ui/views/UiHelpers.h"
#include "QtBridge.h"
#include "ui/views/BookFacetFilters.h"
#include "ui/views/LocalNumberDelegate.h"
#include "ui/shell/LanguageSelector.h"
#include "ui/views/ListPage.h"
#include "ui/views/MemberFacetFilters.h"
#include "ui/views/MetricsPage.h"
#include "ui/presentation/ArchivedBookSpec.h"
#include "ui/presentation/ArchivedCopySpec.h"
#include "ui/presentation/ArchivedLoanSpec.h"
#include "ui/presentation/ArchivedMemberSpec.h"
#include "ui/presentation/BookListSpec.h"
#include "ui/presentation/LoanListSpec.h"
#include "ui/presentation/MemberListSpec.h"
#include "ui/presentation/QueryState.h"

#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/LoanTypes.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QHash>
#include <QDate>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

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
constexpr int kCatalogLocalNumberColumn = 3;

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

QStringList coverFacetTokens(const VLMS::Repositories::CoverFilter filter)
{
    switch (filter) {
    case VLMS::Repositories::CoverFilter::WithCover:
        return {QStringLiteral("with")};
    case VLMS::Repositories::CoverFilter::WithoutCover:
        return {QStringLiteral("without")};
    case VLMS::Repositories::CoverFilter::All:
        break;
    }
    return {QStringLiteral("all")};
}

struct ArchiveTypeEntry {
    VLMS::ArchiveType type;
    const char* code;
    const char* labelKey;
};

constexpr ArchiveTypeEntry kArchiveTypes[] = {
    {VLMS::ArchiveType::Members, "members", "archive.type.members"},
    {VLMS::ArchiveType::Books, "books", "archive.type.books"},
    {VLMS::ArchiveType::Copies, "copies", "archive.type.copies"},
    {VLMS::ArchiveType::Loans, "loans", "archive.type.loans"},
};
}  // namespace

MainWindow::MainWindow(Application& app, QWidget* parent)
    : QMainWindow(parent)
    , m_app(app) {
    buildUi();

    connect(&m_app, &Application::themeChanged, this, &MainWindow::onThemeChanged);
    connect(&m_app.dataEvents(), &VLMS::DataEvents::changed,
            this, &MainWindow::onDataChanged);

    showPage(Page::Catalog);
    rebuildShellChrome();
}

MainWindow::~MainWindow() = default;

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

    connect(m_themeToggle, &QPushButton::clicked, this, [this]() {
        m_app.toggleTheme();
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
    if (m_app.isDatabaseReady()) {
        m_bulk = std::make_unique<VLMS::BulkRunner>(*m_app.dialogs(), m_app.dataEvents());
        m_loanWorkflow = std::make_unique<VLMS::LoanWorkflow>(
            m_app.circulation(), *m_app.dialogs(), m_app.errors(), m_app.dataEvents(), *this,
            *m_bulk);
        // Dedicated LoanListSpec for history dialogs — never shares QueryState with Circulation.
        m_loanHistorySpec = std::make_unique<VLMS::LoanListSpec>(
            m_app.circulation(), m_app.catalog(), m_app.members(), *m_loanWorkflow);
        m_bookWorkflow = std::make_unique<VLMS::BookWorkflow>(
            m_app.catalog(), *m_app.dialogs(), m_app.errors(), m_app.dataEvents(), *m_bulk,
            *m_loanWorkflow, *m_loanHistorySpec);
        m_memberWorkflow = std::make_unique<VLMS::MemberWorkflow>(
            m_app.members(), *m_app.dialogs(), m_app.errors(), m_app.dataEvents(), *this,
            *m_bulk, *m_loanWorkflow, *m_loanHistorySpec);
        m_archiveWorkflow = std::make_unique<VLMS::ArchiveWorkflow>(
            m_app.members(), m_app.catalog(), m_app.circulation(), *m_app.dialogs(),
            m_app.errors(), m_app.dataEvents(), *m_bulk, *m_loanHistorySpec, *m_loanWorkflow);
        m_memberListSpec =
            std::make_unique<VLMS::MemberListSpec>(m_app.members(), *m_memberWorkflow);
        VLMS::ListPage::Config membersCfg;
        membersCfg.titleKey = QStringLiteral("page.members.title");
        membersCfg.bodyKey = QStringLiteral("page.members.body");
        membersCfg.searchPlaceholderKey = QStringLiteral("members.searchPlaceholder");
        membersCfg.imageObjectName = QStringLiteral("memberPhoto");
        membersCfg.imageBounds = VLMS::memberPhotoPreviewBounds();
        m_membersPage = new VLMS::ListPage(m_memberListSpec.get(), m_app.dataEvents(),
                                           membersCfg, m_stack);
        m_memberFilters = new VLMS::MemberFacetFilters(
            m_app.memberFacets(), VLMS::Repositories::ArchiveScope::Live,
            m_membersPage->filterColumn());
        m_memberFilters->refresh();
        m_membersPage->addFilterWidget(m_memberFilters, 1);
        connect(m_memberFilters, &VLMS::MemberFacetFilters::changed, this, [this]() {
            if (m_membersPage == nullptr || m_memberFilters == nullptr) {
                return;
            }
            const VLMS::Repositories::MemberFacets facets = m_memberFilters->facets();
            QHash<QString, QStringList> mapped;
            mapped.insert(QStringLiteral("status"), qsl(facets.statuses));
            mapped.insert(QStringLiteral("sex"), qsl(facets.sexes));
            mapped.insert(QStringLiteral("year"), qsl(facets.inscriptionYears));
            mapped.insert(QStringLiteral("ageGroup"), qsl(facets.ageGroups));
            mapped.insert(QStringLiteral("city"), qsl(facets.cities));
            m_membersPage->setFacets(mapped);
        });
        m_bookListSpec =
            std::make_unique<VLMS::BookListSpec>(m_app.catalog(), *m_bookWorkflow);
        VLMS::ListPage::Config catalogCfg;
        catalogCfg.titleKey = QStringLiteral("page.catalog.title");
        catalogCfg.bodyKey = QStringLiteral("page.catalog.body");
        catalogCfg.searchPlaceholderKey = QStringLiteral("catalog.searchPlaceholder");
        catalogCfg.imageObjectName = QStringLiteral("bookCover");
        catalogCfg.imageBounds = VLMS::bookCoverPreviewBounds();
        m_catalogPage = new VLMS::ListPage(m_bookListSpec.get(), m_app.dataEvents(),
                                           catalogCfg, m_stack);
        m_bookFilters = new VLMS::BookFacetFilters(
            m_app.bookFacets(), VLMS::Repositories::ArchiveScope::Live,
            m_catalogPage->filterColumn());
        m_bookFilters->refresh();
        m_catalogPage->addFilterWidget(m_bookFilters, 1);
        connect(m_bookFilters, &VLMS::BookFacetFilters::changed, this, [this]() {
            if (m_catalogPage == nullptr || m_bookFilters == nullptr) {
                return;
            }
            QHash<QString, QStringList> mapped;
            mapped.insert(QStringLiteral("language"), m_bookFilters->languages());
            mapped.insert(QStringLiteral("category"), m_bookFilters->categoryCodes());
            mapped.insert(QStringLiteral("cover"), coverFacetTokens(m_bookFilters->coverFilter()));
            m_catalogPage->setFacets(mapped);
        });
        if (QTableView* catalogTable = m_catalogPage->table()) {
            catalogTable->setItemDelegateForColumn(
                kCatalogLocalNumberColumn, new LocalNumberDelegate(catalogTable));
        }
        m_loanListSpec = std::make_unique<VLMS::LoanListSpec>(
            m_app.circulation(), m_app.catalog(), m_app.members(), *m_loanWorkflow);
        VLMS::ListPage::Config circulationCfg;
        circulationCfg.titleKey = QStringLiteral("page.circulation.title");
        circulationCfg.bodyKey = QStringLiteral("page.circulation.body");
        circulationCfg.searchPlaceholderKey = QStringLiteral("circulation.searchPlaceholder");
        circulationCfg.imageObjectName = QStringLiteral("bookCover");
        circulationCfg.imageBounds = VLMS::bookCoverPreviewBounds();
        circulationCfg.secondImageObjectName = QStringLiteral("loanMemberPhoto");
        circulationCfg.secondImageBounds = VLMS::bookCoverPreviewBounds();
        m_circulationPage = new VLMS::ListPage(m_loanListSpec.get(), m_app.dataEvents(),
                                               circulationCfg, m_stack);
        m_loanFilter = new QListWidget(m_circulationPage->filterColumn());
        m_loanFilter->setObjectName(QStringLiteral("loanFilter"));
        m_loanFilter->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(m_loanFilter, &QListWidget::itemSelectionChanged, this, [this]() {
            applyCirculationFacets();
        });
        m_circulationPage->addFilterWidget(m_loanFilter, 0);
        // Loans of members since archived are still listed here, so years and
        // cities include them. The year is the year of the loan, not registration.
        m_loanMemberFilters = new VLMS::MemberFacetFilters(
            m_app.memberFacets(), VLMS::Repositories::ArchiveScope::Any,
            m_circulationPage->filterColumn());
        m_loanMemberFilters->useLoanYears([this]() {
            return m_app.circulation().listLoanYears(VLMS::Repositories::ArchiveScope::Live);
        });
        m_loanMemberFilters->refresh();
        m_circulationPage->addFilterWidget(m_loanMemberFilters, 1);
        connect(m_loanMemberFilters, &VLMS::MemberFacetFilters::changed, this, [this]() {
            applyCirculationFacets();
        });
        refreshLoanFilterList();
        m_archivedMemberSpec = std::make_unique<VLMS::ArchivedMemberSpec>(
            m_app.members(), *m_archiveWorkflow);
        m_archivedBookSpec = std::make_unique<VLMS::ArchivedBookSpec>(
            m_app.catalog(), m_app.circulation(), *m_archiveWorkflow);
        m_archivedCopySpec = std::make_unique<VLMS::ArchivedCopySpec>(
            m_app.catalog(), *m_archiveWorkflow);
        m_archivedLoanSpec = std::make_unique<VLMS::ArchivedLoanSpec>(
            m_app.circulation(), m_app.catalog(), m_app.members(), *m_archiveWorkflow);
        VLMS::ListPage::Config archiveCfg;
        archiveCfg.titleKey = QStringLiteral("page.archive.title");
        archiveCfg.bodyKey = QStringLiteral("page.archive.body");
        archiveCfg.searchPlaceholderKey = QStringLiteral("archive.searchPlaceholder");
        archiveCfg.imageObjectName = QStringLiteral("archiveImage");
        archiveCfg.imageBounds = VLMS::bookCoverPreviewBounds();
        archiveCfg.secondImageObjectName = QStringLiteral("archiveMemberPhoto");
        archiveCfg.secondImageBounds = VLMS::bookCoverPreviewBounds();
        m_archivePage = new VLMS::ListPage(m_archivedMemberSpec.get(), m_app.dataEvents(),
                                           archiveCfg, m_stack);
        m_archiveTypeList = new QListWidget(m_archivePage->filterColumn());
        m_archiveTypeList->setObjectName(QStringLiteral("archiveType"));
        m_archiveTypeList->setSelectionMode(QAbstractItemView::SingleSelection);
        m_archiveTypeList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        for (const ArchiveTypeEntry& entry : kArchiveTypes) {
            auto* item = new QListWidgetItem(m_archiveTypeList);
            item->setData(Qt::UserRole, QString::fromLatin1(entry.code));
        }
        m_archiveTypeList->item(0)->setSelected(true);
        connect(m_archiveTypeList, &QListWidget::itemSelectionChanged, this,
                &MainWindow::onArchiveTypeChanged);
        m_archivePage->addFilterWidget(m_archiveTypeList, 0);
        m_archiveMemberFilters = new VLMS::MemberFacetFilters(
            m_app.memberFacets(), VLMS::Repositories::ArchiveScope::Archived,
            m_archivePage->filterColumn());
        m_archivePage->addFilterWidget(m_archiveMemberFilters, 1);
        connect(m_archiveMemberFilters, &VLMS::MemberFacetFilters::changed, this, [this]() {
            applyArchiveFacets();
        });
        m_archiveBookFilters = new VLMS::BookFacetFilters(
            m_app.bookFacets(), VLMS::Repositories::ArchiveScope::Archived,
            m_archivePage->filterColumn());
        m_archivePage->addFilterWidget(m_archiveBookFilters, 1);
        connect(m_archiveBookFilters, &VLMS::BookFacetFilters::changed, this, [this]() {
            applyArchiveFacets();
        });
        m_archiveType = VLMS::ArchiveType::Members;
        refreshArchiveTypeList();
        applyArchiveFilterVisibility();
        applyArchiveFacets();
        m_metricsPage = new MetricsPage(m_app.metricsSource(), m_stack);
    } else {
        m_membersPage = nullptr;
        m_memberFilters = nullptr;
        m_memberListSpec.reset();
        m_catalogPage = nullptr;
        m_bookFilters = nullptr;
        m_bookListSpec.reset();
        m_circulationPage = nullptr;
        m_loanFilter = nullptr;
        m_loanMemberFilters = nullptr;
        m_loanListSpec.reset();
        m_loanHistorySpec.reset();
        m_archivePage = nullptr;
        m_archiveTypeList = nullptr;
        m_archiveMemberFilters = nullptr;
        m_archiveBookFilters = nullptr;
        m_archivedMemberSpec.reset();
        m_archivedBookSpec.reset();
        m_archivedCopySpec.reset();
        m_archivedLoanSpec.reset();
        m_metricsPage = nullptr;
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
        applyArchiveFilterVisibility();
        if (VLMS::PagedQueryModel* model = m_archivePage->model()) {
            model->reload();
        }
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
    if (code.isEmpty() || code == m_app.uiLocale()) {
        return;
    }

    m_app.setUiLocale(code);
}

void MainWindow::onThemeChanged()
{
    // The stylesheet has already been swapped application-wide. What is left is
    // everything the stylesheet cannot reach: the brand banner, the cover
    // placeholders the pages painted from light-mode artwork, and the label
    // naming the mode the button switches to. LanguageChange refresh covers
    // the same chrome and preview paths.
    rebuildShellChrome();
    forwardLanguageChange();
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        rebuildShellChrome();
        forwardLanguageChange();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::forwardLanguageChange()
{
    const auto send = [](QWidget* w) {
        if (w == nullptr) {
            return;
        }
        QEvent languageChange(QEvent::LanguageChange);
        QCoreApplication::sendEvent(w, &languageChange);
    };

    send(m_languageSelector);
    send(m_metricsPage);
    send(m_membersPage);
    send(m_catalogPage);
    send(m_circulationPage);
    send(m_archivePage);

    if (m_memberFilters != nullptr) {
        m_memberFilters->refresh();
    }
    if (m_bookFilters != nullptr) {
        m_bookFilters->refresh();
    }
    if (m_loanMemberFilters != nullptr) {
        m_loanMemberFilters->refresh();
    }
    if (m_circulationPage != nullptr) {
        refreshLoanFilterList();
    }
    if (m_archivePage != nullptr) {
        refreshArchiveTypeList();
        applyArchiveFilterVisibility();
    }
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

void MainWindow::rebuildShellChrome()
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

    if (m_languageSelector != nullptr) {
        m_languageSelector->setCurrentLanguage(VLMS::qs(VLMS::Core::Locale::code()));
    }

    m_footerLabel->setText(T("footer.copyright", "year",
                             std::to_string(VLMS::Core::Clock::today().year())));
    // The label keeps its appearance -- no underline, no hover change. The
    // pointing hand (set by ClickableLabel) and this tooltip are the only
    // cues that it opens anything.
    m_footerLabel->setToolTip(T("licence.tooltip"));

    const auto refreshPlaceholder = [&](QWidget* page) {
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

    if (m_metricsPage == nullptr) {
        refreshPlaceholder(m_stack->widget(4));
    }
    if (m_archivePage == nullptr) {
        refreshPlaceholder(m_stack->widget(3));
    }

    updateNavigation(m_activePage);
}


MainWindow::Page MainWindow::toPage(VLMS::PageId id)
{
    switch (id) {
    case VLMS::PageId::Catalog:
        return Page::Catalog;
    case VLMS::PageId::Members:
        return Page::Members;
    case VLMS::PageId::Circulation:
        return Page::Circulation;
    case VLMS::PageId::Archive:
        return Page::Archive;
    case VLMS::PageId::Metrics:
        return Page::Metrics;
    }
    return Page::Catalog;
}

void MainWindow::show(VLMS::PageId page)
{
    showPage(toPage(page));
}

void MainWindow::showMemberLoans(const QString& membershipNumber)
{
    showPage(Page::Circulation);
    if (m_circulationPage == nullptr) {
        return;
    }
    // All, not Open: overdue loans also block removal. Reset borrower facets so
    // an earlier sex/city pick cannot hide this member's loans.
    selectLoanFilterAll();
    if (m_loanMemberFilters != nullptr) {
        m_loanMemberFilters->reset();
    }
    applyCirculationFacets();
    // ListPage enables header sorting; clear any active column sort so the
    // repository default (open/overdue before returned) applies for this jump.
    if (QTableView* table = m_circulationPage->table()) {
        table->setSortingEnabled(false);
        if (QHeaderView* header = table->horizontalHeader()) {
            header->setSortIndicator(-1, Qt::AscendingOrder);
        }
    }
    if (VLMS::PagedQueryModel* model = m_circulationPage->model()) {
        VLMS::QueryState state = model->query();
        state.sortKey.clear();
        state.ascending = true;
        model->setQuery(state);
    }
    m_circulationPage->setSearchText(membershipNumber);
    if (QTableView* table = m_circulationPage->table()) {
        table->setSortingEnabled(true);
    }
    m_circulationPage->focusSearch();
}

void MainWindow::showBook(qint64 bookId)
{
    showPage(Page::Catalog);
    if (m_catalogPage != nullptr) {
        m_catalogPage->revealId(bookId);
    }
}

void MainWindow::showArchive(VLMS::ArchiveType type)
{
    showPage(Page::Archive);
    selectArchiveType(type);
}

void MainWindow::onDataChanged(VLMS::EntitySet what, QList<qint64>)
{
    if ((what & VLMS::Entity::Books) || (what & VLMS::Entity::Copies)) {
        if (m_bookFilters != nullptr) {
            m_bookFilters->refresh();
        }
        if (m_archiveBookFilters != nullptr) {
            m_archiveBookFilters->refresh();
        }
    }
    if (what & VLMS::Entity::Members) {
        if (m_memberFilters != nullptr) {
            m_memberFilters->refresh();
        }
        if (m_loanMemberFilters != nullptr) {
            m_loanMemberFilters->refresh();
        }
        if (m_archiveMemberFilters != nullptr) {
            m_archiveMemberFilters->refresh();
        }
    }
    if (what & VLMS::Entity::Loans) {
        if (m_loanMemberFilters != nullptr) {
            m_loanMemberFilters->refresh();
        }
        if (m_archiveMemberFilters != nullptr
            && currentArchiveType() == VLMS::ArchiveType::Loans) {
            m_archiveMemberFilters->refresh();
        }
    }
    if (m_metricsPage != nullptr) {
        m_metricsPage->refreshMetrics();
    }
}

void MainWindow::refreshLoanFilterList()
{
    if (m_loanFilter == nullptr) {
        return;
    }

    QString previouslySelected;
    for (const QListWidgetItem* item : m_loanFilter->selectedItems()) {
        previouslySelected = item->data(Qt::UserRole).toString();
        break;
    }
    if (previouslySelected.isEmpty()) {
        previouslySelected = QString::fromLatin1(VLMS::Repositories::LoanFilter::kAll);
    }

    const QSignalBlocker blocker(m_loanFilter);
    m_loanFilter->clear();

    auto* allItem = new QListWidgetItem(T("circulation.filter.all"));
    allItem->setData(Qt::UserRole, QString::fromLatin1(VLMS::Repositories::LoanFilter::kAll));
    m_loanFilter->addItem(allItem);

    for (const std::string& code : VLMS::Repositories::CirculationRepository::filterCodes()) {
        if (code == VLMS::Repositories::LoanFilter::kAll) {
            continue;
        }
        auto* item = new QListWidgetItem(T("circulation.filter." + code));
        item->setData(Qt::UserRole, qs(code));
        m_loanFilter->addItem(item);
    }

    QListWidgetItem* toSelect = allItem;
    for (int row = 0; row < m_loanFilter->count(); ++row) {
        QListWidgetItem* item = m_loanFilter->item(row);
        if (item->data(Qt::UserRole).toString() == previouslySelected) {
            toSelect = item;
            break;
        }
    }
    toSelect->setSelected(true);
    m_loanFilter->setCurrentItem(toSelect);

    if (m_loanFilter->count() > 0) {
        const int rowHeight = qMax(m_loanFilter->sizeHintForRow(0), 20);
        m_loanFilter->setFixedHeight(rowHeight * m_loanFilter->count() + 4);
        m_loanFilter->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
}

void MainWindow::applyCirculationFacets()
{
    if (m_circulationPage == nullptr) {
        return;
    }

    QHash<QString, QStringList> mapped;
    QStringList loanFilters;
    if (m_loanFilter != nullptr) {
        for (const QListWidgetItem* item : m_loanFilter->selectedItems()) {
            const QString code = item->data(Qt::UserRole).toString();
            loanFilters.append(code.isEmpty()
                                   ? QString::fromLatin1(VLMS::Repositories::LoanFilter::kAll)
                                   : code);
        }
    }
    if (loanFilters.isEmpty()) {
        loanFilters.append(QString::fromLatin1(VLMS::Repositories::LoanFilter::kAll));
    }
    mapped.insert(QStringLiteral("loanFilter"), loanFilters);

    if (m_loanMemberFilters != nullptr) {
        const VLMS::Repositories::MemberFacets facets = m_loanMemberFilters->facets();
        mapped.insert(QStringLiteral("status"), qsl(facets.statuses));
        mapped.insert(QStringLiteral("sex"), qsl(facets.sexes));
        mapped.insert(QStringLiteral("year"), m_loanMemberFilters->loanYears());
        mapped.insert(QStringLiteral("ageGroup"), qsl(facets.ageGroups));
        mapped.insert(QStringLiteral("city"), qsl(facets.cities));
    }
    m_circulationPage->setFacets(mapped);
}

void MainWindow::selectLoanFilterAll()
{
    if (m_loanFilter == nullptr) {
        return;
    }
    const QSignalBlocker blocker(m_loanFilter);
    m_loanFilter->clearSelection();
    for (int row = 0; row < m_loanFilter->count(); ++row) {
        QListWidgetItem* item = m_loanFilter->item(row);
        const QString code = item->data(Qt::UserRole).toString();
        if (code.isEmpty() || code == QLatin1String(VLMS::Repositories::LoanFilter::kAll)) {
            item->setSelected(true);
            m_loanFilter->setCurrentItem(item);
            break;
        }
    }
}

void MainWindow::refreshArchiveTypeList()
{
    if (m_archiveTypeList == nullptr) {
        return;
    }
    for (int row = 0; row < m_archiveTypeList->count()
                      && row < static_cast<int>(std::size(kArchiveTypes));
         ++row) {
        m_archiveTypeList->item(row)->setText(T(kArchiveTypes[row].labelKey));
    }
    sizeArchiveTypeList();
}

void MainWindow::sizeArchiveTypeList()
{
    if (m_archiveTypeList == nullptr) {
        return;
    }
    m_archiveTypeList->ensurePolished();
    int rows = 0;
    for (int row = 0; row < m_archiveTypeList->count(); ++row) {
        rows += std::max(m_archiveTypeList->sizeHintForRow(row), 20);
    }
    const QMargins margins = m_archiveTypeList->contentsMargins();
    m_archiveTypeList->setFixedHeight(rows + 2 * m_archiveTypeList->frameWidth() + margins.top()
                                      + margins.bottom());
}

VLMS::ArchiveType MainWindow::currentArchiveType() const
{
    return m_archiveType;
}

VLMS::ListSpec* MainWindow::archiveSpecFor(VLMS::ArchiveType type) const
{
    switch (type) {
    case VLMS::ArchiveType::Members:
        return m_archivedMemberSpec.get();
    case VLMS::ArchiveType::Books:
        return m_archivedBookSpec.get();
    case VLMS::ArchiveType::Copies:
        return m_archivedCopySpec.get();
    case VLMS::ArchiveType::Loans:
        return m_archivedLoanSpec.get();
    }
    return m_archivedMemberSpec.get();
}

void MainWindow::applyArchiveFilterVisibility()
{
    if (m_archiveMemberFilters == nullptr || m_archiveBookFilters == nullptr) {
        return;
    }
    const bool memberType =
        m_archiveType == VLMS::ArchiveType::Members || m_archiveType == VLMS::ArchiveType::Loans;
    m_archiveMemberFilters->setVisible(memberType);
    m_archiveBookFilters->setVisible(!memberType);
    // A loan's borrower can be live or archived, and an archived copy can
    // belong to a live title, so those two read their values from every row.
    m_archiveMemberFilters->setValueScope(
        m_archiveType == VLMS::ArchiveType::Loans ? VLMS::Repositories::ArchiveScope::Any
                                                  : VLMS::Repositories::ArchiveScope::Archived);
    if (m_archiveType == VLMS::ArchiveType::Loans) {
        m_archiveMemberFilters->useLoanYears([this]() {
            return m_app.circulation().listLoanYears(VLMS::Repositories::ArchiveScope::Archived);
        });
    } else {
        m_archiveMemberFilters->useLoanYears({});
    }
    m_archiveBookFilters->setScope(m_archiveType == VLMS::ArchiveType::Copies
                                       ? VLMS::Repositories::ArchiveScope::Any
                                       : VLMS::Repositories::ArchiveScope::Archived);
    if (memberType) {
        m_archiveMemberFilters->refresh();
    } else {
        m_archiveBookFilters->refresh();
    }
}

void MainWindow::applyArchiveFacets()
{
    if (m_archivePage == nullptr) {
        return;
    }
    QHash<QString, QStringList> mapped;
    const bool memberType =
        m_archiveType == VLMS::ArchiveType::Members || m_archiveType == VLMS::ArchiveType::Loans;
    if (memberType && m_archiveMemberFilters != nullptr) {
        const VLMS::Repositories::MemberFacets facets = m_archiveMemberFilters->facets();
        mapped.insert(QStringLiteral("status"), qsl(facets.statuses));
        mapped.insert(QStringLiteral("sex"), qsl(facets.sexes));
        mapped.insert(QStringLiteral("year"),
                      m_archiveType == VLMS::ArchiveType::Loans
                          ? m_archiveMemberFilters->loanYears()
                          : qsl(facets.inscriptionYears));
        mapped.insert(QStringLiteral("ageGroup"), qsl(facets.ageGroups));
        mapped.insert(QStringLiteral("city"), qsl(facets.cities));
    } else if (!memberType && m_archiveBookFilters != nullptr) {
        mapped.insert(QStringLiteral("language"), m_archiveBookFilters->languages());
        mapped.insert(QStringLiteral("category"), m_archiveBookFilters->categoryCodes());
        mapped.insert(QStringLiteral("cover"),
                      coverFacetTokens(m_archiveBookFilters->coverFilter()));
    }
    m_archivePage->setFacets(mapped);
}

void MainWindow::onArchiveTypeChanged()
{
    if (m_archiveTypeList == nullptr || m_archivePage == nullptr) {
        return;
    }
    const auto selected = m_archiveTypeList->selectedItems();
    if (selected.isEmpty()) {
        const QSignalBlocker blocker(m_archiveTypeList);
        for (int row = 0; row < m_archiveTypeList->count()
                          && row < static_cast<int>(std::size(kArchiveTypes));
             ++row) {
            if (kArchiveTypes[row].type == m_archiveType) {
                m_archiveTypeList->item(row)->setSelected(true);
                break;
            }
        }
        return;
    }
    const int row = m_archiveTypeList->row(selected.first());
    if (row < 0 || row >= static_cast<int>(std::size(kArchiveTypes))) {
        return;
    }
    m_archiveType = kArchiveTypes[row].type;
    if (m_archiveMemberFilters != nullptr) {
        m_archiveMemberFilters->reset();
    }
    if (m_archiveBookFilters != nullptr) {
        m_archiveBookFilters->reset();
    }
    applyArchiveFilterVisibility();
    if (VLMS::ListSpec* spec = archiveSpecFor(m_archiveType)) {
        m_archivePage->setSpec(spec);
    }
    applyArchiveFacets();
}

void MainWindow::selectArchiveType(VLMS::ArchiveType type)
{
    if (m_archivePage == nullptr) {
        m_archiveType = type;
        return;
    }
    if (m_archiveTypeList != nullptr) {
        for (int row = 0; row < m_archiveTypeList->count()
                          && row < static_cast<int>(std::size(kArchiveTypes));
             ++row) {
            if (kArchiveTypes[row].type == type) {
                const QSignalBlocker blocker(m_archiveTypeList);
                m_archiveTypeList->clearSelection();
                m_archiveTypeList->item(row)->setSelected(true);
                break;
            }
        }
    }
    // Drive the same path as a click so columns and filters update.
    m_archiveType = type;
    if (m_archiveMemberFilters != nullptr) {
        m_archiveMemberFilters->reset();
    }
    if (m_archiveBookFilters != nullptr) {
        m_archiveBookFilters->reset();
    }
    applyArchiveFilterVisibility();
    if (VLMS::ListSpec* spec = archiveSpecFor(type)) {
        m_archivePage->setSpec(spec);
    }
    applyArchiveFacets();
}

