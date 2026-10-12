#include "ui/views/ActionBar.h"

#include "QtBridge.h"
#include "ui/views/UiHelpers.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QTableView>

#include <algorithm>

using VLMS::T;
using VLMS::ss;

namespace VLMS {

namespace {

bool isPrimaryAction(const QString& id)
{
    return id == QStringLiteral("add") || id == QStringLiteral("checkout")
        || id == QStringLiteral("restore");
}

}  // namespace

ActionBar::ActionBar(QWidget* page, QTableView* table, QHBoxLayout* buttons, QObject* parent)
    : QObject(parent),
      m_page(page),
      m_table(table),
      m_buttonLayout(buttons)
{
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, &ActionBar::showContextMenu);
}

void ActionBar::setContextProvider(ContextProvider provider)
{
    m_context = std::move(provider);
}

void ActionBar::setRunner(Runner runner)
{
    m_runner = std::move(runner);
}

void ActionBar::setActions(QList<PageAction> actions)
{
    std::stable_sort(actions.begin(), actions.end(), [](const PageAction& a, const PageAction& b) {
        return sortRank(a.id) < sortRank(b.id);
    });
    m_actions = std::move(actions);
    rebuildButtons();
    installShortcuts();
    refreshEnabled();
}

void ActionBar::refreshEnabled()
{
    for (int i = 0; i < m_buttons.size() && i < m_actions.size(); ++i) {
        m_buttons.at(i)->setEnabled(isEnabled(m_actions.at(i)));
    }
}

void ActionBar::retranslate()
{
    for (int i = 0; i < m_buttons.size() && i < m_actions.size(); ++i) {
        m_buttons.at(i)->setText(T(ss(m_actions.at(i).labelKey)));
    }
}

void ActionBar::run(const PageAction& action)
{
    if (!isEnabled(action)) {
        return;
    }
    const ActionContext ctx = context();
    if (m_runner) {
        m_runner(action, ctx);
    } else if (action.run) {
        (void)action.run(ctx);
    }
    refreshEnabled();
}

bool ActionBar::isEnabled(const PageAction& action) const
{
    const ActionContext ctx = context();
    if (action.enabled && !action.enabled(ctx)) {
        return false;
    }
    switch (action.needs) {
    case PageAction::Needs::Nothing:
        return true;
    case PageAction::Needs::One:
        return ctx.ids.size() == 1;
    case PageAction::Needs::Some:
        return !ctx.ids.isEmpty();
    }
    return false;
}

ActionContext ActionBar::context() const
{
    ActionContext ctx;
    if (m_context) {
        ctx = m_context();
    }
    if (ctx.parent == nullptr) {
        ctx.parent = m_page;
    }
    return ctx;
}

int ActionBar::sortRank(const QString& id)
{
    // Circulation visual order first, then the shared list chrome order.
    static const QStringList kOrder = {
        QStringLiteral("return"),
        QStringLiteral("checkout"),
        QStringLiteral("extend"),
        QStringLiteral("add"),
        QStringLiteral("edit"),
        QStringLiteral("loans"),
        QStringLiteral("reuse"),
        QStringLiteral("archive"),
        QStringLiteral("restore"),
        QStringLiteral("purge"),
        QStringLiteral("tickAll"),
        QStringLiteral("refresh"),
    };
    const int index = kOrder.indexOf(id);
    return index >= 0 ? index : 1000;
}

bool ActionBar::isPageScoped(const QString& id)
{
    return id == QStringLiteral("add") || id == QStringLiteral("checkout")
        || id == QStringLiteral("refresh");
}

QKeySequence ActionBar::standardShortcut(const QString& id)
{
    if (id == QStringLiteral("add") || id == QStringLiteral("checkout")) {
        return QKeySequence(QStringLiteral("Ctrl+N"));
    }
    if (id == QStringLiteral("edit")) {
        return QKeySequence(Qt::Key_Return);
    }
    if (id == QStringLiteral("loans")) {
        return QKeySequence(QStringLiteral("Ctrl+L"));
    }
    if (id == QStringLiteral("archive") || id == QStringLiteral("restore")) {
        return QKeySequence(Qt::Key_Delete);
    }
    if (id == QStringLiteral("purge")) {
        return QKeySequence(QStringLiteral("Shift+Del"));
    }
    if (id == QStringLiteral("tickAll")) {
        return QKeySequence(QStringLiteral("Ctrl+A"));
    }
    if (id == QStringLiteral("refresh")) {
        return QKeySequence(Qt::Key_F5);
    }
    return {};
}

void ActionBar::rebuildButtons()
{
    for (QPushButton* button : m_buttons) {
        m_buttonLayout->removeWidget(button);
        button->setParent(nullptr);
        button->deleteLater();
    }
    m_buttons.clear();

    for (const PageAction& action : m_actions) {
        QPushButton* button = isPrimaryAction(action.id)
            ? makePrimaryButton(T(ss(action.labelKey)))
            : makeSecondaryButton(T(ss(action.labelKey)));
        button->setObjectName(QStringLiteral("listAction_%1").arg(action.id));
        button->setProperty("actionId", action.id);
        connect(button, &QPushButton::clicked, this, [this, action] { run(action); });
        m_buttonLayout->addWidget(button);
        m_buttons.append(button);
    }
}

void ActionBar::clearShortcuts()
{
    for (QShortcut* shortcut : m_shortcuts) {
        shortcut->deleteLater();
    }
    m_shortcuts.clear();
}

void ActionBar::installShortcuts()
{
    clearShortcuts();
    for (const PageAction& action : m_actions) {
        const QKeySequence sequence =
            action.shortcut.isEmpty() ? standardShortcut(action.id) : action.shortcut;
        if (sequence.isEmpty()) {
            continue;
        }
        const bool pageScoped = isPageScoped(action.id);
        QWidget* host = pageScoped ? m_page : static_cast<QWidget*>(m_table);
        auto* shortcut = new QShortcut(sequence, host);
        shortcut->setContext(pageScoped ? Qt::WindowShortcut : Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, action] { run(action); });
        m_shortcuts.append(shortcut);
        // Enter and Return both edit when present.
        if (action.id == QStringLiteral("edit") && sequence == QKeySequence(Qt::Key_Return)) {
            auto* enter = new QShortcut(QKeySequence(Qt::Key_Enter), m_table);
            enter->setContext(Qt::WidgetWithChildrenShortcut);
            connect(enter, &QShortcut::activated, this, [this, action] { run(action); });
            m_shortcuts.append(enter);
        }
    }
}

void ActionBar::showContextMenu(const QPoint& pos)
{
    QMenu menu(m_page);
    menu.setObjectName(QStringLiteral("listContextMenu"));
    for (const PageAction& action : m_actions) {
        if (!isEnabled(action)) {
            continue;
        }
        QAction* item = menu.addAction(T(ss(action.labelKey)));
        connect(item, &QAction::triggered, this, [this, action] { run(action); });
    }
    if (menu.isEmpty()) {
        return;
    }
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

}  // namespace VLMS
