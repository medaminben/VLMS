#pragma once

#include "ui/workflows/DataEvents.h"
#include "ui/workflows/Navigator.h"

#include <QList>
#include <memory>
#include <QMainWindow>

class Application;
class MetricsPage;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace VLMS {
class ArchiveWorkflow;
class ArchivedBookSpec;
class ArchivedCopySpec;
class ArchivedLoanSpec;
class ArchivedMemberSpec;
class BookFacetFilters;
class BookListSpec;
class BookWorkflow;
class BulkRunner;
class ListPage;
class ListSpec;
class LoanListSpec;
class LoanWorkflow;
class MemberFacetFilters;
class MemberListSpec;
class MemberWorkflow;
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
    void refreshLoanFilterList();
    void applyCirculationFacets();
    void selectLoanFilterAll();
    void refreshArchiveTypeList();
    void sizeArchiveTypeList();
    void applyArchiveFilterVisibility();
    void applyArchiveFacets();
    void onArchiveTypeChanged();
    void selectArchiveType(VLMS::ArchiveType type);
    [[nodiscard]] VLMS::ArchiveType currentArchiveType() const;
    [[nodiscard]] VLMS::ListSpec* archiveSpecFor(VLMS::ArchiveType type) const;
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

    VLMS::ListPage* m_membersPage = nullptr;
    VLMS::ListPage* m_catalogPage = nullptr;
    VLMS::ListPage* m_circulationPage = nullptr;
    VLMS::ListPage* m_archivePage = nullptr;
    MetricsPage* m_metricsPage = nullptr;

    Page m_activePage = Page::Catalog;

    std::unique_ptr<VLMS::BulkRunner> m_bulk;
    std::unique_ptr<VLMS::LoanWorkflow> m_loanWorkflow;
    std::unique_ptr<VLMS::BookWorkflow> m_bookWorkflow;
    std::unique_ptr<VLMS::MemberWorkflow> m_memberWorkflow;
    std::unique_ptr<VLMS::ArchiveWorkflow> m_archiveWorkflow;
    std::unique_ptr<VLMS::MemberListSpec> m_memberListSpec;
    std::unique_ptr<VLMS::BookListSpec> m_bookListSpec;
    std::unique_ptr<VLMS::LoanListSpec> m_loanListSpec;
    std::unique_ptr<VLMS::LoanListSpec> m_loanHistorySpec;
    std::unique_ptr<VLMS::ArchivedMemberSpec> m_archivedMemberSpec;
    std::unique_ptr<VLMS::ArchivedBookSpec> m_archivedBookSpec;
    std::unique_ptr<VLMS::ArchivedCopySpec> m_archivedCopySpec;
    std::unique_ptr<VLMS::ArchivedLoanSpec> m_archivedLoanSpec;
    VLMS::MemberFacetFilters* m_memberFilters = nullptr;
    VLMS::BookFacetFilters* m_bookFilters = nullptr;
    VLMS::MemberFacetFilters* m_loanMemberFilters = nullptr;
    QListWidget* m_loanFilter = nullptr;
    QListWidget* m_archiveTypeList = nullptr;
    VLMS::MemberFacetFilters* m_archiveMemberFilters = nullptr;
    VLMS::BookFacetFilters* m_archiveBookFilters = nullptr;
    VLMS::ArchiveType m_archiveType = VLMS::ArchiveType::Members;
};
