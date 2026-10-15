#include "ui/views/ListPage.h"

#include "ui/views/ActionBar.h"

#include "ui/views/PreviewImages.h"
#include "ui/views/ListPagePager.h"
#include "QtBridge.h"

#include <VLMS/Core/Strings.h>

#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
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
    if (m_model != nullptr) {
        // Archive type switch: previous sort/facets are meaningless on the new
        // list. Keep search text and page size.
        QueryState state = m_model->query();
        state.sortKey.clear();
        state.ascending = true;
        state.page = 1;
        state.facets.clear();
        m_model->setSpec(spec);
        m_model->setQuery(state);
    }
    if (m_pager != nullptr) {
        m_pager->resetToFirstPage();
    }
    if (QTableView* view = table()) {
        view->setSortingEnabled(false);
        if (QHeaderView* header = view->horizontalHeader()) {
            header->setSortIndicator(-1, Qt::AscendingOrder);
        }
        view->setSortingEnabled(true);
    }
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

QWidget* ListPage::filterColumn() const
{
    return m_filterColumn;
}

void ListPage::setFacets(const QHash<QString, QStringList>& facets)
{
    if (m_model == nullptr) {
        return;
    }
    QueryState state = m_model->query();
    state.facets = facets;
    state.page = 1;
    if (m_pager != nullptr) {
        m_pager->resetToFirstPage();
    }
    m_model->setQuery(state);
}

void ListPage::rebuildChrome()
{
    m_actionBar->setActions(m_spec != nullptr ? m_spec->actions() : QList<PageAction>{});
    applyLocalizedChrome();
    updateActionEnabled();
}

void ListPage::changeEvent(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        rebuildChrome();
    }
    QWidget::changeEvent(event);
}

void ListPage::applyLocalizedChrome()
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
        QEvent languageChange(QEvent::LanguageChange);
        QCoreApplication::sendEvent(m_pager, &languageChange);
    }
    if (m_errorRetry != nullptr) {
        m_errorRetry->setText(T("list.retry"));
    }
    if (m_spec != nullptr) {
        m_spec->retranslate();
        if (m_model != nullptr) {
            m_model->notifySpecRetranslated();
        }
        m_actionBar->retranslate();
        // Refresh preview artwork so theme-dependent placeholders repaint.
        onSelectionChanged();
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
    m_errorRetry = makeSecondaryButton(T("list.retry"));
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
    m_actionBar = new ActionBar(this, m_table, padLayout, this);
    m_actionBar->setContextProvider([this] { return currentContext(); });
    m_actionBar->setRunner(
        [this](const PageAction& action, const ActionContext&) { runAction(action); });
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

    m_pager = new ListPagePager(tablePanel);
    connect(m_pager, &ListPagePager::pageChanged, this, &ListPage::onPageChanged);

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

void ListPage::updateActionEnabled()
{
    if (m_actionBar != nullptr) {
        m_actionBar->refreshEnabled();
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
    if (action.id == QStringLiteral("tickAll")) {
        m_model->toggleTicks();
        updateActionEnabled();
        if (action.run) {
            (void)action.run(currentContext());
        }
        return;
    }
    if (action.id == QStringLiteral("refresh")) {
        if (m_model != nullptr) {
            m_model->reload();
        }
        if (action.run) {
            (void)action.run(currentContext());
        }
        return;
    }

    // Best-effort reveal after add/edit: workflows emit DataEvents with the id.
    QList<qint64> revealIds;
    QMetaObject::Connection revealConn;
    const bool wantReveal =
        action.id == QStringLiteral("add") || action.id == QStringLiteral("edit");
    if (wantReveal) {
        revealConn = connect(&m_events, &DataEvents::changed, this,
                             [&revealIds](EntitySet, const QList<qint64>& ids) {
                                 if (ids.size() == 1 && ids.first() > 0) {
                                     revealIds = ids;
                                 }
                             });
    }
    if (action.run) {
        (void)action.run(currentContext());
    }
    if (revealConn) {
        disconnect(revealConn);
    }
    if (revealIds.size() == 1) {
        revealId(revealIds.first());
    }
}

ActionContext ListPage::currentContext() const
{
    ActionContext ctx;
    ctx.parent = const_cast<ListPage*>(this);
    if (m_model != nullptr) {
        ctx.ids = m_model->checkedIds();
    }
    ctx.fromChecks = !ctx.ids.isEmpty();
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
    // Prefer an explicitly selected row. currentIndex can linger after
    // clearSelection(), which would keep Needs::Some actions wrongly enabled.
    if (m_table->selectionModel() != nullptr) {
        const auto indexes = m_table->selectionModel()->selectedRows();
        if (!indexes.isEmpty()) {
            return m_spec->idAt(indexes.first().row());
        }
    }
    return 0;
}

}  // namespace VLMS
