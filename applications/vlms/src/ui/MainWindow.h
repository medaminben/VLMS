#pragma once

#include <QMainWindow>

class Application;
class ArchivePage;
class CatalogPage;
class CirculationPage;
class MembersPage;
class MetricsPage;
class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace VLMS {
class ClickableLabel;
class LanguageSelector;
}

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onLanguageChanged(const QString& code);
    void onThemeChanged();
    void retranslateUi();

private:
    enum class Page { Catalog, Members, Circulation, Archive, Metrics };

    void buildUi();
    void showPage(Page page);
    QWidget* buildPlaceholderPage(const QString& titleKey, const QString& bodyKey);
    void updateNavigation(Page active);
    void updateBrandBanner();
    void updateThemeToggle();
    void updateManualButton();

    QWidget* m_centralRoot = nullptr;
    QStackedWidget* m_stack = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    VLMS::ClickableLabel* m_footerLabel = nullptr;
    QLabel* m_brandBanner = nullptr;
    VLMS::LanguageSelector* m_languageSelector = nullptr;
    QPushButton* m_themeToggle = nullptr;
    QPushButton* m_manualButton = nullptr;

    QPushButton* m_membersNav = nullptr;
    QPushButton* m_catalogNav = nullptr;
    QPushButton* m_circulationNav = nullptr;
    QPushButton* m_archiveNav = nullptr;
    QPushButton* m_metricsNav = nullptr;

    MembersPage* m_membersPage = nullptr;
    CatalogPage* m_catalogPage = nullptr;
    CirculationPage* m_circulationPage = nullptr;
    ArchivePage* m_archivePage = nullptr;
    MetricsPage* m_metricsPage = nullptr;

    Page m_activePage = Page::Catalog;
};
