#include "ui/ListPage.h"

#include "ui/PreviewImages.h"
#include "ui/TablePager.h"
#include "QtBridge.h"

#include <VLMS/Core/Strings.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

using VLMS::T;

namespace VLMS {
namespace {

constexpr int kDebounceMs = 250;

bool isPrimaryAction(const QString& id)
{
    return id == QStringLiteral("add") || id == QStringLiteral("checkout");
}

}  // namespace

ListPage::ListPage(ListSpec* spec, DataEvents& events, const Config& config, QWidget* parent)
    : QWidget(parent),
      m_config(config),
      m_spec(spec),
      m_events(events)
{
    m_model = new PagedQueryModel(m_spec, m_events, this);
    buildUi(config);
    connect(m_model, &PagedQueryModel::loaded, this, &ListPage::onModelLoaded);
    connect(m_model, &PagedQueryModel::failed, this, &ListPage::onModelFailed);
    connect(m_model, &PagedQueryModel::selectionTargetChanged, this, &ListPage::onSelectionTarget);
    connect(m_model, &PagedQueryModel::dataChanged, this, [this](const QModelIndex&,
                                                                 const QModelIndex&,
                                                                 const QList<int>& roles) {
        if (roles.isEmpty() || roles.contains(Qt::CheckStateRole)) {
            onChecksChanged();
        }
    });
    rebuildChrome();
    m_model->reload();
}

void ListPage::setSpec(ListSpec* spec)
{
    if (m_spec == spec) {
        return;
    }
    m_spec = spec;
    m_model->setSpec(spec);
    rebuildChrome();
}

PagedQueryModel* ListPage::model() const
{
    return m_model;
}

QTableView* ListPage::table() const
{
    return m_table;
}

void ListPage::revealId(qint64 id)
{
    if (m_model == nullptr) {
        return;
    }
    m_model->revealId(id);
}

void ListPage::setSearchText(const QString& text)
{
    if (m_search == nullptr) {
        return;
    }
    {
        const QSignalBlocker blocker(m_search);
        m_search->setText(text);
    }
    if (m_debounce != nullptr) {
        m_debounce->stop();
    }
    searchNow();
}

void ListPage::focusSearch()
{
    if (m_search != nullptr) {
        m_search->setFocus(Qt::OtherFocusReason);
    }
}

void ListPage::addFilterWidget(QWidget* widget, const int stretch)
{
    if (m_filterLayout == nullptr || widget == nullptr) {
        return;
    }
    m_filterLayout->addWidget(widget, stretch);
}

void ListPage::rebuildChrome()
{
    rebuildActions();
    installShortcuts();
    retranslateUi();
    updateActionEnabled();
}

void ListPage::changeEvent(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QWidget::changeEvent(event);
}

void ListPage::retranslateUi()
{
    if (auto* title = findChild<QLabel*>(QStringLiteral("pageTitle"))) {
        title->setText(T(ss(m_config.titleKey)));
    }
    if (auto* subtitle = findChild<QLabel*>(QStringLiteral("pageSubtitle"))) {
        subtitle->setText(T(ss(m_config.bodyKey)));
    }
    if (m_search != nullptr) {
        m_search->setPlaceholderText(T(ss(m_config.searchPlaceholderKey)));
    }
    if (m_pager != nullptr) {
        m_pager->retranslateUi();
    }
    if (m_errorRetry != nullptr) {
        m_errorRetry->setText(QStringLiteral("Retry"));
    }
    if (m_spec != nullptr) {
        m_spec->retranslate();
        const QList<PageAction> actions = orderedActions();
        for (int i = 0; i < m_actionButtons.size() && i < actions.size(); ++i) {
            m_actionButtons.at(i)->setText(T(ss(actions.at(i).labelKey)));
        }
    }
}

void ListPage::buildUi(const Config& config)
{
    setObjectName(QStringLiteral("listPage"));

    auto* root = new QVBoxLayout(this);
    configurePageLayout(root);
    root->addWidget(makePageHeader({}, {}, this));

    m_errorStrip = new QWidget(this);
    m_errorStrip->setObjectName(QStringLiteral("listErrorStrip"));
    m_errorStrip->setVisible(false);
    auto* errorLayout = new QHBoxLayout(m_errorStrip);
    errorLayout->setContentsMargins(8, 4, 8, 4);
    errorLayout->setSpacing(8);
    m_errorLabel = new QLabel(m_errorStrip);
    m_errorLabel->setObjectName(QStringLiteral("listErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorRetry = makeSecondaryButton(QStringLiteral("Retry"));
    m_errorRetry->setObjectName(QStringLiteral("listErrorRetry"));
    connect(m_errorRetry, &QPushButton::clicked, this, &ListPage::retryLoad);
    errorLayout->addWidget(m_errorLabel, 1);
    errorLayout->addWidget(m_errorRetry);
    root->addWidget(m_errorStrip);

    auto* contentRow = new QHBoxLayout();
    contentRow->setContentsMargins(0, 0, 0, 0);
    contentRow->setSpacing(12);

    m_filterColumn = makeFilterColumn(this);
    m_filterColumn->setObjectName(QStringLiteral("filterColumn"));
    m_filterLayout = qobject_cast<QVBoxLayout*>(m_filterColumn->layout());
    contentRow->addWidget(m_filterColumn);

    auto* mainColumn = new QWidget(this);
    mainColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* mainLayout = new QVBoxLayout(mainColumn);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(12);

    m_buttonPad = new QWidget(mainColumn);
    m_buttonPad->setObjectName(QStringLiteral("buttonPad"));
    auto* padLayout = new QHBoxLayout(m_buttonPad);
    padLayout->setContentsMargins(0, 0, 0, 0);
    padLayout->setSpacing(8);

    m_search = new QLineEdit(m_buttonPad);
    m_search->setObjectName(QStringLiteral("listSearch"));
    padLayout->addWidget(m_search, 1);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);
    connect(m_search, &QLineEdit::textChanged, this, &ListPage::onSearchEdited);
    connect(m_debounce, &QTimer::timeout, this, &ListPage::searchNow);
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        m_debounce->stop();
        searchNow();
    });

    mainLayout->addWidget(m_buttonPad);

    auto* bodyHost = new QWidget(mainColumn);
    auto* bodyLayout = new QVBoxLayout(bodyHost);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal, bodyHost);

    auto* tablePanel = new QWidget(splitter);
    auto* tableLayout = new QVBoxLayout(tablePanel);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(0);

    m_table = new QTableView(tablePanel);
    m_table->setObjectName(QStringLiteral("listTable"));
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, &ListPage::showContextMenu);
    if (m_table->selectionModel() != nullptr) {
        connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &ListPage::onSelectionChanged);
    }
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        if (m_table->selectionModel() != nullptr) {
            connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged,
                    this, &ListPage::onSelectionChanged, Qt::UniqueConnection);
        }
        updateActionEnabled();
    });

    if (QHeaderView* header = m_table->horizontalHeader()) {
        header->setSectionResizeMode(QHeaderView::Interactive);
        header->setStretchLastSection(true);
        header->setCascadingSectionResizes(false);
        header->setMinimumSectionSize(60);
        if (m_spec != nullptr) {
            const QList<ColumnSpec> cols = m_spec->columns();
            for (int column = 0; column < cols.size(); ++column) {
                header->resizeSection(column, cols.at(column).width);
            }
        }
    }

    m_pager = new TablePager(tablePanel);
    connect(m_pager, &TablePager::pageChanged, this, &ListPage::onPageChanged);

    tableLayout->addWidget(m_table, 1);
    tableLayout->addWidget(m_pager);

    m_previewPanel = new QWidget(splitter);
    m_previewPanel->setObjectName(QStringLiteral("previewPanel"));
    m_previewPanel->setMinimumWidth(config.previewPanelMinWidth);
    auto* previewLayout = new QVBoxLayout(m_previewPanel);
    previewLayout->setContentsMargins(8, 0, 0, 0);
    previewLayout->setSpacing(4);

    const bool twoImages = !config.secondImageObjectName.isEmpty();
    const int imageColumns = twoImages ? 2 : 1;
    m_image = new QLabel(m_previewPanel);
    m_image->setObjectName(config.imageObjectName);
    m_image->setAlignment(Qt::AlignCenter);
    applyPreviewLabelGeometry(
        m_image, adaptivePreviewImageSize(m_previewPanel, config.imageBounds, imageColumns));
    if (twoImages) {
        m_secondImage = new QLabel(m_previewPanel);
        m_secondImage->setObjectName(config.secondImageObjectName);
        m_secondImage->setAlignment(Qt::AlignCenter);
        applyPreviewLabelGeometry(
            m_secondImage,
            adaptivePreviewImageSize(m_previewPanel, config.secondImageBounds, imageColumns));
    }

    m_details = new QWidget(m_previewPanel);
    m_details->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_detailsLayout = new QVBoxLayout(m_details);
    m_detailsLayout->setContentsMargins(0, 0, 0, 0);
    m_detailsLayout->setSpacing(10);

    auto* detailsScroll = new QScrollArea(m_previewPanel);
    detailsScroll->setObjectName(QStringLiteral("listDetailsScroll"));
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setFrameShape(QFrame::NoFrame);
    detailsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailsScroll->setWidget(m_details);

    if (m_secondImage == nullptr) {
        previewLayout->addWidget(m_image, 0, Qt::AlignHCenter);
    } else {
        auto* imageRow = new QHBoxLayout();
        imageRow->setContentsMargins(0, 0, 0, 0);
        imageRow->setSpacing(kPreviewImageGap);
        imageRow->addStretch(1);
        imageRow->addWidget(m_image, 0, Qt::AlignTop);
        imageRow->addWidget(m_secondImage, 0, Qt::AlignTop);
        imageRow->addStretch(1);
        previewLayout->addLayout(imageRow);
    }
    previewLayout->addWidget(detailsScroll, 1);

    splitter->addWidget(tablePanel);
    splitter->addWidget(m_previewPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({720, 340});

    bodyLayout->addWidget(splitter);
    mainLayout->addWidget(bodyHost, 1);

    contentRow->addWidget(mainColumn, 1);
    root->addLayout(contentRow, 1);
}

void ListPage::onSearchEdited(const QString& /*text*/)
{
    m_debounce->start();
}

void ListPage::searchNow()
{
    if (m_model == nullptr || m_search == nullptr) {
        return;
    }
    QueryState state = m_model->query();
    state.search = m_search->text();
    state.page = 1;
    if (m_pager != nullptr) {
        m_pager->resetToFirstPage();
    }
    m_model->setQuery(state);
}

void ListPage::onPageChanged()
{
    if (m_model == nullptr || m_pager == nullptr) {
        return;
    }
    QueryState state = m_model->query();
    state.page = m_pager->currentPage();
    int pageSize = 0;
    if (auto* combo = m_pager->findChild<QComboBox*>(QStringLiteral("pagerPageSize"))) {
        pageSize = combo->currentData().toInt();
    }
    state.pageSize = pageSize;
    m_model->setQuery(state);
}

void ListPage::onSelectionChanged()
{
    updateActionEnabled();
    const qint64 id = selectedId();
    if (id <= 0 || m_spec == nullptr) {
        clearPreview();
        return;
    }
    applyPreview(m_spec->preview(id));
}

void ListPage::onChecksChanged()
{
    updateActionEnabled();
}

void ListPage::onModelLoaded(const int total)
{
    if (m_errorStrip != nullptr) {
        m_errorStrip->setVisible(false);
    }
    if (m_pager != nullptr) {
        m_pager->setTotalCount(total);
    }
    updateActionEnabled();
}

void ListPage::onModelFailed(const Core::Error& error)
{
    if (m_errorStrip == nullptr || m_errorLabel == nullptr) {
        return;
    }
    m_errorLabel->setText(errorText(error));
    m_errorStrip->setVisible(true);
}

void ListPage::onSelectionTarget(const int row)
{
    selectTableRow(m_table, row);
}

void ListPage::retryLoad()
{
    if (m_model != nullptr) {
        m_model->reload();
    }
}

void ListPage::rebuildActions()
{
    if (m_buttonPad == nullptr) {
        return;
    }
    auto* padLayout = qobject_cast<QHBoxLayout*>(m_buttonPad->layout());
    if (padLayout == nullptr) {
        return;
    }
    for (QPushButton* button : m_actionButtons) {
        padLayout->removeWidget(button);
        button->deleteLater();
    }
    m_actionButtons.clear();
    m_actions = orderedActions();

    for (const PageAction& action : m_actions) {
        QPushButton* button = isPrimaryAction(action.id)
            ? makePrimaryButton(T(ss(action.labelKey)))
            : makeSecondaryButton(T(ss(action.labelKey)));
        button->setObjectName(QStringLiteral("listAction_%1").arg(action.id));
        button->setProperty("actionId", action.id);
        connect(button, &QPushButton::clicked, this, [this, action] { runAction(action); });
        padLayout->addWidget(button);
        m_actionButtons.append(button);
    }
}

void ListPage::showContextMenu(const QPoint& pos)
{
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("listContextMenu"));
    const QList<PageAction> actions = orderedActions();
    for (const PageAction& action : actions) {
        if (!actionIsEnabled(action)) {
            continue;
        }
        QAction* item = menu.addAction(T(ss(action.labelKey)));
        QObject::connect(item, &QAction::triggered, this, [this, action] { runAction(action); });
    }
    if (menu.isEmpty()) {
        return;
    }
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void ListPage::clearShortcuts()
{
    for (QShortcut* shortcut : m_shortcuts) {
        shortcut->deleteLater();
    }
    m_shortcuts.clear();
}

void ListPage::installShortcuts()
{
    clearShortcuts();
    const QList<PageAction> actions = orderedActions();
    for (const PageAction& action : actions) {
        QKeySequence sequence = action.shortcut;
        if (sequence.isEmpty()) {
            if (action.id == QStringLiteral("add")) {
                sequence = QKeySequence(QStringLiteral("Ctrl+N"));
            } else if (action.id == QStringLiteral("edit")) {
                sequence = QKeySequence(Qt::Key_Return);
            } else if (action.id == QStringLiteral("loans")) {
                sequence = QKeySequence(QStringLiteral("Ctrl+L"));
            } else if (action.id == QStringLiteral("archive")
                       || action.id == QStringLiteral("restore")) {
                sequence = QKeySequence(Qt::Key_Delete);
            } else if (action.id == QStringLiteral("purge")) {
                sequence = QKeySequence(QStringLiteral("Shift+Del"));
            } else if (action.id == QStringLiteral("tickAll")) {
                sequence = QKeySequence(QStringLiteral("Ctrl+A"));
            } else if (action.id == QStringLiteral("refresh")) {
                sequence = QKeySequence(Qt::Key_F5);
            }
        }
        if (sequence.isEmpty()) {
            continue;
        }

        const bool pageScoped = isPageScopedAction(action.id);
        QWidget* host = pageScoped ? static_cast<QWidget*>(this) : static_cast<QWidget*>(m_table);
        auto* shortcut = new QShortcut(sequence, host);
        shortcut->setContext(pageScoped ? Qt::WindowShortcut : Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, action] {
            if (actionIsEnabled(action)) {
                runAction(action);
            }
        });
        // Enter and Return both edit when present.
        if (action.id == QStringLiteral("edit") && sequence == QKeySequence(Qt::Key_Return)) {
            auto* enter = new QShortcut(QKeySequence(Qt::Key_Enter), m_table);
            enter->setContext(Qt::WidgetWithChildrenShortcut);
            connect(enter, &QShortcut::activated, this, [this, action] {
                if (actionIsEnabled(action)) {
                    runAction(action);
                }
            });
            m_shortcuts.append(enter);
        }
        m_shortcuts.append(shortcut);
    }
}

void ListPage::updateActionEnabled()
{
    const QList<PageAction> actions = orderedActions();
    for (int i = 0; i < m_actionButtons.size() && i < actions.size(); ++i) {
        m_actionButtons.at(i)->setEnabled(actionIsEnabled(actions.at(i)));
    }
}

void ListPage::applyPreview(const PreviewContent& content)
{
    const bool twoImages = m_secondImage != nullptr;
    const int imageColumns = twoImages ? 2 : 1;
    const QSize imageSize =
        adaptivePreviewImageSize(m_previewPanel, m_config.imageBounds, imageColumns);

    const bool memberLike = m_config.imageObjectName.contains(QStringLiteral("member"), Qt::CaseInsensitive)
        || m_config.imageObjectName.contains(QStringLiteral("Photo"), Qt::CaseInsensitive);
    if (memberLike && m_secondImage == nullptr) {
        showMemberPhoto(m_image, content.imagePath, imageSize);
    } else {
        showBookCover(m_image, content.imagePath, imageSize);
    }
    if (m_secondImage != nullptr) {
        const QSize secondSize =
            adaptivePreviewImageSize(m_previewPanel, m_config.secondImageBounds, imageColumns);
        showMemberPhoto(m_secondImage, content.secondImagePath, secondSize);
    }

    clearLayout(m_detailsLayout);
    for (const PreviewField& field : content.fields) {
        auto* fieldWidget = new QWidget(m_details);
        auto* fieldLayout = new QVBoxLayout(fieldWidget);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(2);

        auto* label = new QLabel(fieldWidget);
        label->setObjectName(QStringLiteral("bookDetailLabel"));
        label->setAlignment(Qt::AlignLeading);
        label->setText(T(ss(field.labelKey)) + QStringLiteral(":"));

        QWidget* value = makeDetailValueWidget(fieldWidget, field.scrollable);
        setDetailValueText(value, field.value);

        fieldLayout->addWidget(label);
        fieldLayout->addWidget(value);
        m_detailsLayout->addWidget(fieldWidget);
    }
    m_detailsLayout->addStretch(1);
}

void ListPage::clearPreview()
{
    const bool twoImages = m_secondImage != nullptr;
    const int imageColumns = twoImages ? 2 : 1;
    const QSize imageSize =
        adaptivePreviewImageSize(m_previewPanel, m_config.imageBounds, imageColumns);
    const bool memberLike = m_config.imageObjectName.contains(QStringLiteral("member"), Qt::CaseInsensitive)
        || m_config.imageObjectName.contains(QStringLiteral("Photo"), Qt::CaseInsensitive);
    if (memberLike && m_secondImage == nullptr) {
        showMemberPhoto(m_image, {}, imageSize);
    } else {
        showBookCover(m_image, {}, imageSize);
    }
    if (m_secondImage != nullptr) {
        const QSize secondSize =
            adaptivePreviewImageSize(m_previewPanel, m_config.secondImageBounds, imageColumns);
        showMemberPhoto(m_secondImage, {}, secondSize);
    }
    clearLayout(m_detailsLayout);
    m_detailsLayout->addStretch(1);
}

void ListPage::runAction(const PageAction& action)
{
    if (!actionIsEnabled(action)) {
        return;
    }
    if (action.id == QStringLiteral("tickAll")) {
        m_model->tickAll();
        updateActionEnabled();
        if (action.run) {
            (void)action.run(currentContext());
        }
        return;
    }
    if (action.run) {
        (void)action.run(currentContext());
    }
}

ActionContext ListPage::currentContext() const
{
    ActionContext ctx;
    ctx.parent = const_cast<ListPage*>(this);
    if (m_model != nullptr) {
        ctx.ids = m_model->checkedIds();
    }
    if (ctx.ids.isEmpty()) {
        const qint64 id = selectedId();
        if (id > 0) {
            ctx.ids.append(id);
        }
    }
    return ctx;
}

qint64 ListPage::selectedId() const
{
    if (m_table == nullptr || m_model == nullptr || m_spec == nullptr) {
        return 0;
    }
    const QModelIndex current = m_table->currentIndex();
    if (!current.isValid()) {
        const auto indexes = m_table->selectionModel() != nullptr
            ? m_table->selectionModel()->selectedRows()
            : QModelIndexList{};
        if (indexes.isEmpty()) {
            return 0;
        }
        return m_spec->idAt(indexes.first().row());
    }
    return m_spec->idAt(current.row());
}

QList<PageAction> ListPage::orderedActions() const
{
    if (m_spec == nullptr) {
        return {};
    }
    QList<PageAction> actions = m_spec->actions();
    std::stable_sort(actions.begin(), actions.end(),
                     [](const PageAction& a, const PageAction& b) {
                         return actionSortRank(a.id) < actionSortRank(b.id);
                     });
    return actions;
}

bool ListPage::actionIsEnabled(const PageAction& action) const
{
    const ActionContext ctx = currentContext();
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

bool ListPage::isPageScopedAction(const QString& id)
{
    return id == QStringLiteral("add") || id == QStringLiteral("refresh");
}

int ListPage::actionSortRank(const QString& id)
{
    // Circulation visual order first, then the shared list chrome order.
    static const QStringList kOrder = {
        QStringLiteral("return"),
        QStringLiteral("checkout"),
        QStringLiteral("extend"),
        QStringLiteral("add"),
        QStringLiteral("edit"),
        QStringLiteral("loans"),
        QStringLiteral("archive"),
        QStringLiteral("restore"),
        QStringLiteral("purge"),
        QStringLiteral("tickAll"),
        QStringLiteral("refresh"),
    };
    const int index = kOrder.indexOf(id);
    return index >= 0 ? index : 1000;
}

}  // namespace VLMS
