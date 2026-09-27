#pragma once

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/LoanTypes.h>
#include <VLMS/Core/MemberRepository.h>

#include "ui/TableHeaderSort.h"

#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QEvent;
class QShowEvent;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class TableRowChecks;

namespace VLMS {
class MemberFacetFilters;
class TablePager;
}

class CirculationPage final : public QWidget {
    Q_OBJECT

public:
    CirculationPage(CirculationRepository& repository,
                    CatalogRepository& catalogRepository,
                    MemberRepository& memberRepository,
                    QWidget* parent = nullptr);

    void retranslateUi();

    /**
     * Narrows the page to one member's loans (All filter: open and overdue
     * first, then returned) and puts the cursor in the search box.
     *
     * Driven from the members page, which sends a librarian here when a member
     * cannot be removed until their books come back. The narrowing is done by
     * filling in the search box rather than by a hidden member filter, so what
     * is on screen explains why the list is short and one Backspace undoes it.
     */
    void focusMemberLoans(const QString& membershipNumber);

private slots:
    void refreshLoans();
    void refreshFilter();
    void onSearchChanged();
    void onFilterChanged();
    void checkoutLoan();
    void extendLoan();
    void returnLoan();
    void archiveLoan();
    void onSelectionChanged();
    void onSortChanged(int column, bool ascending);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    /// Re-reads the member filters' years and cities, which a registration on
    /// the Members page may have added to.
    void showEvent(QShowEvent* event) override;

private:
    struct LoanDetailRow {
        QString labelKey;
        QLabel* label = nullptr;
        QWidget* value = nullptr;
    };

    void buildUi();
    void resetPagerAndRefresh();
    LoanQuery currentLoanQuery() const;
    qint64 selectedLoanId() const;
    void selectLoanId(qint64 id);
    QStringList selectedFilters() const;
    void sizeFilterList();
    void updatePreview(const LoanRecord& loan);
    void renderPreviewImages();
    void clearLoanDetails();
    void refreshSelectedLoanPreview();
    QString loanStatusLabel(const LoanRecord& loan) const;

    CirculationRepository& m_repository;
    CatalogRepository& m_catalogRepository;
    MemberRepository& m_memberRepository;
    bool m_updatingFilter = false;

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_checkoutButton = nullptr;
    QPushButton* m_extendButton = nullptr;
    QPushButton* m_returnButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QListWidget* m_filterList = nullptr;
    VLMS::MemberFacetFilters* m_memberFilters = nullptr;
    QTableWidget* m_loansTable = nullptr;
    TableRowChecks* m_checks = nullptr;
    VLMS::TableHeaderSort* m_sort = nullptr;
    VLMS::TablePager* m_pager = nullptr;
    QLabel* m_coverPreview = nullptr;
    QLabel* m_photoPreview = nullptr;
    QWidget* m_previewPanel = nullptr;
    QWidget* m_detailsPanel = nullptr;
    QVector<LoanDetailRow> m_detailRows;
    QString m_previewCoverPath;
    QString m_previewPhotoPath;
};
