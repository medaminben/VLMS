#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace VLMS {

class TablePager final : public QWidget {
    Q_OBJECT

public:
    static constexpr int kDefaultPageSize = 50;
    static constexpr int kMinVisibleCount = 20;
    static constexpr int kShowAllSize = 0;

    explicit TablePager(QWidget* parent = nullptr);

    void setPageSize(int pageSize);
    [[nodiscard]] int pageSize() const;

    void setTotalCount(int totalCount);
    [[nodiscard]] int totalCount() const { return m_totalCount; }

    [[nodiscard]] int currentPage() const { return m_currentPage; }
    [[nodiscard]] int pageCount() const;
    [[nodiscard]] int offset() const;

    void resetToFirstPage();
    void setCurrentPage(int page); // clamp 1..pageCount; no pageChanged signal
    void retranslateUi();

signals:
    void pageChanged(int page);

private slots:
    void goFirst();
    void goPrevious();
    void goNext();
    void goLast();
    void goToPage(int page);
    void onPageSizeChosen(int index);

private:
    void updateControls();
    void fillPageSizeCombo();
    void syncPageSizeCombo();

    int m_pageSize = kDefaultPageSize;
    int m_totalCount = 0;
    int m_currentPage = 1;
    // ALL until the librarian says otherwise: these lists are read by
    // searching and scrolling, and a page boundary only hides the row that was
    // being looked for. kDefaultPageSize is what a size chosen and then left
    // falls back to, not what the pager opens on.
    bool m_showAll = true;

    QPushButton* m_firstButton = nullptr;
    QPushButton* m_previousButton = nullptr;
    QPushButton* m_nextButton = nullptr;
    QPushButton* m_lastButton = nullptr;
    QComboBox* m_pageSizeCombo = nullptr;
    QLabel* m_rangeLabel = nullptr;
    QLabel* m_pageLabel = nullptr;
    QSpinBox* m_pageSpin = nullptr;
    QLabel* m_pagesLabel = nullptr;
};

}  // namespace VLMS
