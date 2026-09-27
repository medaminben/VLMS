#include "ui/ListPageFrame.h"

#include "ui/TablePager.h"

#include <QAbstractItemView>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace VLMS {

ListPageFrame::ListPageFrame(QWidget* parent)
    : QWidget(parent)
{
}

void ListPageFrame::buildShell(const QString& subtitle,
                               const bool showFilters,
                               const bool showSearch)
{
    setObjectName(QStringLiteral("listPageFrame"));

    auto* root = new QVBoxLayout(this);
    configurePageLayout(root);
    root->addWidget(makePageHeader({}, subtitle, this));

    auto* contentRow = new QHBoxLayout();
    contentRow->setContentsMargins(0, 0, 0, 0);
    contentRow->setSpacing(12);

    m_filterColumn = makeFilterColumn(this);
    m_filterColumn->setObjectName(QStringLiteral("filterColumn"));
    m_filterColumn->setVisible(showFilters);
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

    m_searchEdit = new QLineEdit(m_buttonPad);
    m_searchEdit->setObjectName(QStringLiteral("listSearch"));
    m_searchEdit->setVisible(showSearch);
    padLayout->addWidget(m_searchEdit, 1);
    if (!showSearch) {
        // A hidden widget's stretch counts for nothing, so without this the
        // dashboard's lone Refresh button took the whole width of the row.
        // The buttons sit at the trailing end, where the list pages have them.
        padLayout->addStretch(1);
    }

    mainLayout->addWidget(m_buttonPad);

    m_bodyHost = new QWidget(mainColumn);
    auto* bodyLayout = new QVBoxLayout(m_bodyHost);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    mainLayout->addWidget(m_bodyHost, 1);

    contentRow->addWidget(mainColumn, 1);
    root->addLayout(contentRow, 1);
    m_built = true;
}

void ListPageFrame::buildList(const ListConfig& config)
{
    if (m_built) {
        return;
    }

    buildShell(config.subtitle, true, true);

    auto* splitter = new QSplitter(Qt::Horizontal, m_bodyHost);

    auto* tablePanel = new QWidget(splitter);
    auto* tableLayout = new QVBoxLayout(tablePanel);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(0);

    m_table = new QTableWidget(tablePanel);
    m_table->setObjectName(QStringLiteral("listTable"));
    m_table->setColumnCount(config.tableColumnCount);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    configureResizableColumns(m_table, config.tableColumnWidths);

    m_pager = new TablePager(tablePanel);
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
    m_imageLabel = new QLabel(m_previewPanel);
    m_imageLabel->setObjectName(config.imageObjectName);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    applyPreviewLabelGeometry(
        m_imageLabel,
        adaptivePreviewImageSize(m_previewPanel, config.imageBounds, imageColumns));
    if (twoImages) {
        m_secondImageLabel = new QLabel(m_previewPanel);
        m_secondImageLabel->setObjectName(config.secondImageObjectName);
        m_secondImageLabel->setAlignment(Qt::AlignCenter);
        applyPreviewLabelGeometry(
            m_secondImageLabel,
            adaptivePreviewImageSize(m_previewPanel, config.secondImageBounds, imageColumns));
    }

    m_detailsPanel = new QWidget(m_previewPanel);
    m_detailsPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* detailsLayout = new QVBoxLayout(m_detailsPanel);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(10);

    auto* detailsScroll = new QScrollArea(m_previewPanel);
    detailsScroll->setObjectName(config.detailsScrollObjectName);
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setFrameShape(QFrame::NoFrame);
    detailsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    detailsScroll->setWidget(m_detailsPanel);

    if (m_secondImageLabel == nullptr) {
        previewLayout->addWidget(m_imageLabel, 0, Qt::AlignHCenter);
    } else {
        // Top-aligned: the two images keep their own proportions, so a cover
        // and a photo of the same width are not the same height.
        auto* imageRow = new QHBoxLayout();
        imageRow->setContentsMargins(0, 0, 0, 0);
        imageRow->setSpacing(kPreviewImageGap);
        imageRow->addStretch(1);
        imageRow->addWidget(m_imageLabel, 0, Qt::AlignTop);
        imageRow->addWidget(m_secondImageLabel, 0, Qt::AlignTop);
        imageRow->addStretch(1);
        previewLayout->addLayout(imageRow);
    }
    previewLayout->addWidget(detailsScroll, 1);

    splitter->addWidget(tablePanel);
    splitter->addWidget(m_previewPanel);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({720, 340});

    m_bodyHost->layout()->addWidget(splitter);
}

void ListPageFrame::buildDashboard(const QString& subtitle)
{
    if (m_built) {
        return;
    }

    buildShell(subtitle, false, false);

    m_viewerHost = new QWidget(m_bodyHost);
    m_viewerHost->setObjectName(QStringLiteral("viewerHost"));
    auto* viewerLayout = new QVBoxLayout(m_viewerHost);
    viewerLayout->setContentsMargins(0, 0, 0, 0);
    viewerLayout->setSpacing(0);
    m_bodyHost->layout()->addWidget(m_viewerHost);
}

QVBoxLayout* ListPageFrame::filterLayout() const
{
    if (m_filterColumn == nullptr) {
        return nullptr;
    }
    return qobject_cast<QVBoxLayout*>(m_filterColumn->layout());
}

void ListPageFrame::addFilter(QWidget* widget, const int stretch)
{
    if (auto* layout = filterLayout(); layout != nullptr && widget != nullptr) {
        layout->addWidget(widget, stretch);
    }
}

void ListPageFrame::addButton(QPushButton* button)
{
    if (m_buttonPad == nullptr || button == nullptr) {
        return;
    }
    if (auto* layout = qobject_cast<QHBoxLayout*>(m_buttonPad->layout())) {
        layout->addWidget(button);
    }
}

}  // namespace VLMS
