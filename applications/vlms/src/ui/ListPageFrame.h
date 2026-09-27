#pragma once

#include "ui/UiHelpers.h"

#include <QList>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QVBoxLayout;

namespace VLMS {

class TablePager;

/**
 * Shared shell for the four application pages.
 *
 * List mode is LTR: FilterColumn (controller), then Search + ButtonPad,
 * Table + Pager (model), Viewer (image + details).
 *
 * Dashboard mode (Metrics) is the same machine with an empty filter column,
 * no search, a one-button pad, and an overloaded viewer.
 */
class ListPageFrame final : public QWidget {
    Q_OBJECT

public:
    struct ListConfig {
        QString subtitle;
        int tableColumnCount = 0;
        QList<int> tableColumnWidths;
        QString imageObjectName;
        PreviewImageBounds imageBounds;
        /// Set for a viewer with two images side by side (a loan's cover and
        /// its borrower's photo); empty for one.
        QString secondImageObjectName;
        PreviewImageBounds secondImageBounds;
        QString detailsScrollObjectName;
        int previewPanelMinWidth = 200;
    };

    explicit ListPageFrame(QWidget* parent = nullptr);

    void buildList(const ListConfig& config);
    void buildDashboard(const QString& subtitle);

    [[nodiscard]] QWidget* filterColumn() const { return m_filterColumn; }
    [[nodiscard]] QVBoxLayout* filterLayout() const;
    [[nodiscard]] QLineEdit* searchEdit() const { return m_searchEdit; }
    [[nodiscard]] QWidget* buttonPad() const { return m_buttonPad; }
    [[nodiscard]] QTableWidget* table() const { return m_table; }
    [[nodiscard]] TablePager* pager() const { return m_pager; }
    [[nodiscard]] QLabel* imageLabel() const { return m_imageLabel; }
    /// Null unless ListConfig::secondImageObjectName was set.
    [[nodiscard]] QLabel* secondImageLabel() const { return m_secondImageLabel; }
    [[nodiscard]] QWidget* previewPanel() const { return m_previewPanel; }
    [[nodiscard]] QWidget* detailsPanel() const { return m_detailsPanel; }
    [[nodiscard]] QWidget* viewerHost() const { return m_viewerHost; }

    void addFilter(QWidget* widget, int stretch = 0);
    void addButton(QPushButton* button);

private:
    void buildShell(const QString& subtitle, bool showFilters, bool showSearch);

    bool m_built = false;
    QWidget* m_filterColumn = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QWidget* m_buttonPad = nullptr;
    QWidget* m_bodyHost = nullptr;
    QTableWidget* m_table = nullptr;
    TablePager* m_pager = nullptr;
    QLabel* m_imageLabel = nullptr;
    QLabel* m_secondImageLabel = nullptr;
    QWidget* m_previewPanel = nullptr;
    QWidget* m_detailsPanel = nullptr;
    QWidget* m_viewerHost = nullptr;
};

}  // namespace VLMS
