#pragma once

#include "ui/workflows/DataEvents.h"
#include "ui/workflows/Navigator.h"

#include <QList>
#include <memory>
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
class BookWorkflow;
class BulkRunner;
class ClickableLabel;
class LanguageSelector;
}

class MainWindow final : public QMainWindow, public VLMS::Navigator {
    Q_OBJECT

public:
    explicit MainWindow(Application& app, QWidget* parent = nullptr);
    ~MainWindow() override;

    void show(VLMS::PageId page) override;
    void showMemberLoans(const QString& membershipNumber) override;
    void showBook(qint64 bookId) override;
    void showArchive(VLMS::ArchiveType type) override;

private slots:
    void onLanguageChanged(const QString& code);
    void onThemeChanged();
    void retranslateUi();
    void onDataChanged(VLMS::EntitySet what, QList<qint64> ids);

private:
    enum class Page { Catalog, Members, Circulation, Archive, Metrics };

    void buildUi();
    void showPage(Page page);
    QWidget* buildPlaceholderPage(const QString& titleKey, const QString& bodyKey);
    void updateNavigation(Page active);
    void updateBrandBanner();
    void updateThemeToggle();
    void updateManualButton();
    [[nodiscard]] static Page toPage(VLMS::PageId id);

    Application& m_app;

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

    std::unique_ptr<VLMS::BulkRunner> m_bulk;
    std::unique_ptr<VLMS::BookWorkflow> m_bookWorkflow;
};
