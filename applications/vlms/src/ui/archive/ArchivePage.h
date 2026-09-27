#pragma once

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/MemberRepository.h>

#include <QVector>
#include <QWidget>

#include <vector>

class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class TableRowChecks;

namespace VLMS {
class BookFacetFilters;
class MemberFacetFilters;
class TableHeaderSort;
class TablePager;
}  // namespace VLMS

/// Fifth list page: archived members, books, copies, and loans, one type at a
/// time. Restore sends a row back to its live page; Reuse (copies only) hands
/// an archived copy's local number to a new copy through the book editor.
class ArchivePage final : public QWidget {
    Q_OBJECT

public:
    enum class Type { Members, Books, Copies, Loans };

    ArchivePage(MemberRepository& members,
                CatalogRepository& catalog,
                CirculationRepository& circulation,
                QWidget* parent = nullptr);

    void retranslateUi();
    /// Re-reads the current type; MainWindow calls it whenever the page is shown.
    void refresh();
    [[nodiscard]] Type currentType() const { return m_type; }

signals:
    /// Something went back to a live page, which should re-read its list.
    void recordRestored();
    /// Reuse was pressed on an archived copy that still holds its number.
    void reuseNumberRequested(qint64 copyId);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onTypeChanged();
    void onFacetsChanged();
    void onSearchChanged();
    void onSelectionChanged();
    void onSortChanged(int column, bool ascending);
    void restoreSelected();
    void showLoanHistory();
    void reuseSelected();
    void purgeSelected();

private:
    void buildUi();
    void sizeTypeList();
    void applyColumns();
    void refreshRows();
    [[nodiscard]] bool fillRows();
    void showDetails(qint64 id);
    void clearDetails();
    /// `scrollable` gives the value a scrolling box, as Circulation's notes have.
    void addDetail(const char* labelKey, const QString& value, bool scrollable = false);
    /// Shows the filters that apply to the current type and hides the rest.
    void applyFilters();
    /// Paints the viewer's images for the current type from the stored paths:
    /// a photo for members, a cover for titles and copies, both for loans.
    void renderImages();
    [[nodiscard]] qint64 selectedId() const;
    [[nodiscard]] const BookCopyRecord* selectedCopy() const;
    /// True when nothing holds the selected row any more: no loan names a
    /// member or a copy, no copy belongs to a title. Read from the row already
    /// on screen, so the Loans (or Copies) column always agrees with the button.
    [[nodiscard]] bool selectedMayBePurged() const;
    /// True when the highlighted member, title or copy has ever been lent.
    [[nodiscard]] bool selectedHasLoans() const;

    MemberRepository& m_members;
    CatalogRepository& m_catalog;
    CirculationRepository& m_circulation;
    Type m_type = Type::Members;

    QListWidget* m_typeList = nullptr;
    VLMS::MemberFacetFilters* m_memberFilters = nullptr;
    VLMS::BookFacetFilters* m_bookFilters = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_loansButton = nullptr;
    QPushButton* m_restoreButton = nullptr;
    QPushButton* m_reuseButton = nullptr;
    QPushButton* m_purgeButton = nullptr;
    QTableWidget* m_table = nullptr;
    TableRowChecks* m_checks = nullptr;
    VLMS::TableHeaderSort* m_sort = nullptr;
    VLMS::TablePager* m_pager = nullptr;
    QLabel* m_image = nullptr;
    QLabel* m_photo = nullptr;
    QString m_coverPath;
    QString m_photoPath;
    QWidget* m_previewPanel = nullptr;
    QWidget* m_detailsPanel = nullptr;
    QVector<QWidget*> m_detailWidgets;

    std::vector<MemberRecord> m_memberRows;
    std::vector<BookRecord> m_bookRows;
    std::vector<BookCopyRecord> m_copyRows;
    std::vector<LoanRecord> m_loanRows;
};
