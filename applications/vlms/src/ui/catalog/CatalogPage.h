#pragma once

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Repositories/CirculationRepository.h>

#include "ui/TableHeaderSort.h"

#include <QString>
#include <QVector>
#include <QWidget>

class QLineEdit;
class QEvent;
class QPushButton;
class QTableWidget;
class TableRowChecks;
class QLabel;
class QListWidget;

namespace VLMS {
class BookFacetFilters;
class TablePager;
}

class CatalogPage final : public QWidget {
    Q_OBJECT

public:
    CatalogPage(CatalogRepository& repository,
                CirculationRepository& circulation,
                QWidget* parent = nullptr);

    void retranslateUi();

private slots:
    void refreshBooks();
    void onSearchChanged();
    void addBook();
    void editBook();
    void deleteBook();
    void showLoanHistory();
    void onSelectionChanged();
    void onSortChanged(int column, bool ascending);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct BookDetailRow {
        QString labelKey;
        QLabel* label = nullptr;
        QWidget* value = nullptr;
    };

    void buildUi();
    void resetPagerAndRefresh();
    BookQuery currentBookQuery() const;
    qint64 selectedBookId() const;
    void selectBookId(qint64 id);
    void updateCoverPreview(const BookRecord& book);
    void renderCoverPreview();
    void clearBookDetails();
    void refreshSelectedBookPreview();

    CatalogRepository& m_repository;
    CirculationRepository& m_circulation;

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    QPushButton* m_loansButton = nullptr;
    VLMS::BookFacetFilters* m_filters = nullptr;
    QTableWidget* m_booksTable = nullptr;
    TableRowChecks* m_checks = nullptr;
    VLMS::TableHeaderSort* m_sort = nullptr;
    VLMS::TablePager* m_pager = nullptr;
    QLabel* m_coverPreview = nullptr;
    QWidget* m_previewPanel = nullptr;
    QWidget* m_detailsPanel = nullptr;
    QVector<BookDetailRow> m_detailRows;
    QString m_previewCoverPath;
};
