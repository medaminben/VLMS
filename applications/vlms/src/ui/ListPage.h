#pragma once

#include "ui/UiHelpers.h"
#include "ui/presentation/ListSpec.h"
#include "ui/presentation/PageAction.h"
#include "ui/presentation/PagedQueryModel.h"
#include "ui/presentation/PreviewContent.h"
#include "ui/workflows/DataEvents.h"

#include <VLMS/Core/Result.h>

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QShortcut;
class QTableView;
class QTimer;
class QVBoxLayout;

namespace VLMS {

class ListPagePager;

class ListPage final : public QWidget {
    Q_OBJECT
public:
    struct Config {
        QString titleKey;
        QString bodyKey;
        QString searchPlaceholderKey;
        PreviewImageBounds imageBounds;
        QString imageObjectName;
        QString secondImageObjectName;  // empty = one image
        PreviewImageBounds secondImageBounds;
        int previewPanelMinWidth = 200;
    };

    ListPage(ListSpec* spec, DataEvents& events, const Config& config,
             QWidget* parent = nullptr);

    void setSpec(ListSpec* spec);  // Archive type switch
    [[nodiscard]] PagedQueryModel* model() const;
    [[nodiscard]] QTableView* table() const;
    void revealId(qint64 id);
    void setSearchText(const QString& text);  // Navigator focusMemberLoans
    void focusSearch();
    void addFilterWidget(QWidget* widget, int stretch = 0);
    [[nodiscard]] QWidget* filterColumn() const;
    /// Replace QueryState.facets and reload from page 1 (facet widget adapter).
    void setFacets(const QHash<QString, QStringList>& facets);

    /// Rebuild action buttons/menus from spec->actions() and retranslate titles.
    void rebuildChrome();

protected:
    void changeEvent(QEvent* event) override;

public slots:
    void retranslateUi();

private:
    void buildUi(const Config& config);
    void onSearchEdited(const QString& text);
    void searchNow();
    void onPageChanged();
    void onSelectionChanged();
    void onChecksChanged();
    void onModelLoaded(int total);
    void onModelFailed(const Core::Error& error);
    void onSelectionTarget(int row);
    void retryLoad();
    void rebuildActions();
    void showContextMenu(const QPoint& pos);
    void installShortcuts();
    void clearShortcuts();
    void updateActionEnabled();
    void applyPreview(const PreviewContent& content);
    void clearPreview();
    void runAction(const PageAction& action);
    [[nodiscard]] ActionContext currentContext() const;
    [[nodiscard]] qint64 selectedId() const;
    [[nodiscard]] QList<PageAction> orderedActions() const;
    [[nodiscard]] bool actionIsEnabled(const PageAction& action) const;
    [[nodiscard]] static bool isPageScopedAction(const QString& id);
    [[nodiscard]] static int actionSortRank(const QString& id);

    Config m_config;
    ListSpec* m_spec = nullptr;
    DataEvents& m_events;
    PagedQueryModel* m_model = nullptr;
    QLineEdit* m_search = nullptr;
    QTimer* m_debounce = nullptr;  // 250 ms, singleShot
    QTableView* m_table = nullptr;
    ListPagePager* m_pager = nullptr;
    QWidget* m_filterColumn = nullptr;
    QVBoxLayout* m_filterLayout = nullptr;
    QWidget* m_buttonPad = nullptr;
    QLabel* m_image = nullptr;
    QLabel* m_secondImage = nullptr;
    QWidget* m_previewPanel = nullptr;
    QWidget* m_details = nullptr;
    QVBoxLayout* m_detailsLayout = nullptr;
    QWidget* m_errorStrip = nullptr;
    QLabel* m_errorLabel = nullptr;
    QPushButton* m_errorRetry = nullptr;
    QList<QPushButton*> m_actionButtons;
    QList<PageAction> m_actions;
    QList<QShortcut*> m_shortcuts;
};

}  // namespace VLMS
