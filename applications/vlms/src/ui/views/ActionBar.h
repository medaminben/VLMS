#pragma once

#include "ui/presentation/PageAction.h"

#include <QList>
#include <QObject>
#include <QPoint>

#include <functional>

class QHBoxLayout;
class QPushButton;
class QShortcut;
class QTableView;
class QWidget;

namespace VLMS {

/// The buttons, context menu and shortcuts of one list, built from one list
/// of PageActions. ListPage and LoanHistoryDialog both use it, so every list
/// orders, binds and enables its actions the same way.
///
/// Ctrl+N and F5 belong to the window that holds `page`; every other shortcut
/// only fires while the table has focus, so Enter and Ctrl+A keep their usual
/// meaning in a search box.
class ActionBar final : public QObject {
    Q_OBJECT

public:
    using ContextProvider = std::function<ActionContext()>;
    using Runner = std::function<void(const PageAction&, const ActionContext&)>;

    ActionBar(QWidget* page, QTableView* table, QHBoxLayout* buttons, QObject* parent = nullptr);

    /// Who the actions act on: ticked ids, else the selected row.
    void setContextProvider(ContextProvider provider);
    /// Replaces running action.run, for callers that wrap it (reveal, reload).
    void setRunner(Runner runner);

    /// Sorts the actions into the standard order and rebuilds buttons and shortcuts.
    void setActions(QList<PageAction> actions);
    [[nodiscard]] const QList<PageAction>& actions() const { return m_actions; }

    void refreshEnabled();
    void retranslate();
    void run(const PageAction& action);
    [[nodiscard]] bool isEnabled(const PageAction& action) const;
    [[nodiscard]] ActionContext context() const;

    [[nodiscard]] static int sortRank(const QString& id);
    [[nodiscard]] static bool isPageScoped(const QString& id);
    [[nodiscard]] static QKeySequence standardShortcut(const QString& id);

private:
    void rebuildButtons();
    void installShortcuts();
    void clearShortcuts();
    void showContextMenu(const QPoint& pos);

    QWidget* m_page = nullptr;
    QTableView* m_table = nullptr;
    QHBoxLayout* m_buttonLayout = nullptr;
    ContextProvider m_context;
    Runner m_runner;
    QList<PageAction> m_actions;
    QList<QPushButton*> m_buttons;
    QList<QShortcut*> m_shortcuts;
};

}  // namespace VLMS
