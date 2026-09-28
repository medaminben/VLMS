#include "ui/metrics/MetricsPage.h"

#include <VLMS/Core/Strings.h>
#include "ui/ListPageFrame.h"
#include "ui/TableHeaderSort.h"
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

using VLMS::T;
using VLMS::cd;
using VLMS::qd;
using VLMS::qs;
using VLMS::qsl;
using VLMS::ss;
using VLMS::svl;

namespace {

using VLMS::Strings;

constexpr int kSectionSpacing = 12;
constexpr int kMetricGridSpacing = 8;
constexpr int kMetricCardPadding = 8;
constexpr int kMetricTableRowHeight = 26;
constexpr int kMetricTableMinWidth = 360;
constexpr int kMetricTableMaxWidth = 520;
constexpr int kActivityTableMinWidth = 440;

QTableWidgetItem* makeCenteredTableItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsEnabled);
    return item;
}

QTableWidgetItem* makeCenteredTableItem(const int value)
{
    auto* item = new QTableWidgetItem();
    item->setData(Qt::DisplayRole, value);
    item->setTextAlignment(Qt::AlignCenter);
    item->setFlags(Qt::ItemIsEnabled);
    return item;
}

const QString kPeriodToday = QStringLiteral("today");
const QString kPeriodWeek = QStringLiteral("week");
const QString kPeriodMonth = QStringLiteral("month");

int activityRowForPeriod(const QTableWidget* table, const QString& periodKey)
{
    if (table == nullptr) {
        return -1;
    }
    for (int row = 0; row < table->rowCount(); ++row) {
        const QTableWidgetItem* label = table->item(row, 0);
        if (label != nullptr && label->data(Qt::UserRole).toString() == periodKey) {
            return row;
        }
    }
    return -1;
}

void configureMetricTable(QTableWidget* table)
{
    if (table == nullptr) {
        return;
    }
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(kMetricTableRowHeight);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

}  // namespace

MetricsPage::MetricsPage(VLMS::Repositories::MetricsRepository& repository, QWidget* parent)
    : QWidget(parent),
      m_repository(repository) {
    buildUi();
    retranslateUi();
    refreshMetrics();
}

void MetricsPage::buildUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* frame = new VLMS::ListPageFrame(this);
    frame->buildDashboard(T("page.metrics.body"));
    m_refreshButton = VLMS::makeSecondaryButton({});
    connect(m_refreshButton, &QPushButton::clicked, this, &MetricsPage::refreshMetrics);
    frame->addButton(m_refreshButton);
    rootLayout->addWidget(frame);

    auto* contentHost = new QWidget(frame->viewerHost());
    auto* contentLayout = new QVBoxLayout(contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(kSectionSpacing);

    const QStringList overviewKeys = {
        QStringLiteral("metrics.bookTitles"),
        QStringLiteral("metrics.totalCopies"),
        QStringLiteral("metrics.availableCopies"),
        QStringLiteral("metrics.onLoanCopies"),
    };
    contentLayout->addWidget(makeSection(
        QStringLiteral("metrics.section.overview"),
        makeMetricGrid(overviewKeys, &m_overviewCards, contentHost, 4)));

    const QStringList memberKeys = {
        QStringLiteral("metrics.totalMembers"),
        QStringLiteral("metrics.membersActive"),
        QStringLiteral("metrics.membersNonActive"),
    };
    contentLayout->addWidget(makeSection(
        QStringLiteral("metrics.section.members"),
        makeMetricGrid(memberKeys, &m_memberCards, contentHost, 3)));

    const QStringList circulationKeys = {
        QStringLiteral("metrics.openLoans"),
        QStringLiteral("metrics.overdueLoans"),
        QStringLiteral("metrics.returnedLoans"),
    };
    contentLayout->addWidget(makeSection(
        QStringLiteral("metrics.section.circulation"),
        makeMetricGrid(circulationKeys, &m_circulationCards, contentHost, 3)));

    m_activityTable = new QTableWidget(contentHost);
    m_activityTable->setObjectName(QStringLiteral("metricsActivityTable"));
    m_activityTable->setColumnCount(4);
    m_activityTable->setRowCount(3);
    m_activityTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    configureMetricTable(m_activityTable);
    VLMS::enableWidgetTableSort(m_activityTable);
    // Four columns, not two: at the shared minimum each was 90px and the bold
    // "New members" header lost its first and last letters.
    m_activityTable->setMinimumWidth(kActivityTableMinWidth);
    m_activityTable->setMaximumWidth(kMetricTableMaxWidth);

    auto* categoriesSection = new QWidget(contentHost);
    auto* categoriesLayout = new QVBoxLayout(categoriesSection);
    categoriesLayout->setContentsMargins(0, 0, 0, 0);
    categoriesLayout->setSpacing(8);

    m_categoriesTable = new QTableWidget(categoriesSection);
    m_categoriesTable->setObjectName(QStringLiteral("metricsCategoriesTable"));
    m_categoriesTable->setColumnCount(2);
    m_categoriesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_categoriesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    configureMetricTable(m_categoriesTable);
    VLMS::enableWidgetTableSort(m_categoriesTable);
    m_categoriesTable->setMinimumWidth(kMetricTableMinWidth);
    m_categoriesTable->setMaximumWidth(kMetricTableMaxWidth);

    m_categoriesEmpty = new QLabel(categoriesSection);
    m_categoriesEmpty->setObjectName(QStringLiteral("emptyState"));
    m_categoriesEmpty->setAlignment(Qt::AlignCenter);
    m_categoriesEmpty->setVisible(false);
    m_categoriesEmpty->setMinimumWidth(kMetricTableMinWidth);
    m_categoriesEmpty->setMaximumWidth(kMetricTableMaxWidth);

    categoriesLayout->addWidget(m_categoriesTable);
    categoriesLayout->addWidget(m_categoriesEmpty);

    auto* tablesRow = new QHBoxLayout();
    tablesRow->setContentsMargins(0, 0, 0, 0);
    tablesRow->setSpacing(kSectionSpacing);
    tablesRow->addStretch(1);
    tablesRow->addWidget(
        makeSection(QStringLiteral("metrics.section.activity"), m_activityTable, true),
        0,
        Qt::AlignTop);
    tablesRow->addWidget(
        makeSection(QStringLiteral("metrics.section.categories"), categoriesSection, true),
        0,
        Qt::AlignTop);
    tablesRow->addStretch(1);
    contentLayout->addLayout(tablesRow);
    contentLayout->addStretch(1);

    auto* contentScroll = new QScrollArea(frame->viewerHost());
    contentScroll->setObjectName(QStringLiteral("metricsScroll"));
    contentScroll->setWidgetResizable(true);
    contentScroll->setFrameShape(QFrame::NoFrame);
    contentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentScroll->setWidget(contentHost);

    frame->viewerHost()->layout()->addWidget(contentScroll);
}

QWidget* MetricsPage::makeMetricGrid(const QStringList& labelKeys,
                                     QVector<MetricCard>* cards,
                                     QWidget* parent,
                                     const int columns) {
    auto* gridHost = new QWidget(parent);
    auto* grid = new QGridLayout(gridHost);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(kMetricGridSpacing);
    grid->setVerticalSpacing(kMetricGridSpacing);

    const int columnCount = qMax(1, columns);

    cards->clear();
    cards->reserve(labelKeys.size());

    for (int index = 0; index < labelKeys.size(); ++index) {
        MetricCard card;
        card.labelKey = labelKeys.at(index);

        auto* frame = VLMS::makeCard(gridHost);
        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(
            kMetricCardPadding,
            kMetricCardPadding,
            kMetricCardPadding,
            kMetricCardPadding);
        layout->setSpacing(2);

        card.label = new QLabel(frame);
        card.label->setObjectName(QStringLiteral("metricLabel"));
        card.label->setAlignment(Qt::AlignCenter);
        card.value = new QLabel(frame);
        card.value->setObjectName(QStringLiteral("metricValue"));
        card.value->setAlignment(Qt::AlignCenter);

        layout->addWidget(card.label, 0, Qt::AlignCenter);
        layout->addWidget(card.value, 0, Qt::AlignCenter);

        cards->append(card);
        grid->addWidget(frame, index / columnCount, index % columnCount);
    }

    for (int column = 0; column < columnCount; ++column) {
        grid->setColumnStretch(column, 1);
    }

    return gridHost;
}

QWidget* MetricsPage::makeSection(const QString& titleKey, QWidget* content, const bool centerTitle) {
    auto* section = new QWidget(this);
    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* title = new QLabel(section);
    title->setObjectName(QStringLiteral("sectionTitle"));
    title->setProperty("i18nKey", titleKey);
    title->setAlignment(centerTitle ? Qt::AlignCenter : Qt::AlignLeading);
    layout->addWidget(title);

    if (centerTitle) {
        auto* centeredRow = new QHBoxLayout();
        centeredRow->setContentsMargins(0, 0, 0, 0);
        centeredRow->addStretch(1);
        centeredRow->addWidget(content);
        centeredRow->addStretch(1);
        layout->addLayout(centeredRow);
    } else {
        layout->addWidget(content);
    }

    return section;
}

void MetricsPage::setMetricValue(QLabel* label, const int value) {
    if (label != nullptr) {
        label->setText(QString::number(value));
    }
}

void MetricsPage::refreshMetrics() {
    const auto metricsResult = m_repository.fetchMetrics();
    if (!metricsResult) {
        VLMS::showRepoError(this, metricsResult.error());
        return;
    }
    const VLMS::Repositories::LibraryMetrics& metrics = metricsResult.value();

    const QList<int> overviewValues = {
        metrics.bookTitles,
        metrics.totalCopies,
        metrics.availableCopies,
        metrics.totalCopies - metrics.availableCopies,
    };
    for (int index = 0; index < m_overviewCards.size() && index < overviewValues.size(); ++index) {
        setMetricValue(m_overviewCards.at(index).value, overviewValues.at(index));
    }

    const QList<int> memberValues = {
        metrics.totalMembers,
        metrics.membersActive,
        metrics.membersNonActive,
    };
    for (int index = 0; index < m_memberCards.size() && index < memberValues.size(); ++index) {
        setMetricValue(m_memberCards.at(index).value, memberValues.at(index));
    }

    const QList<int> circulationValues = {
        metrics.openLoans,
        metrics.overdueLoans,
        metrics.returnedLoans,
    };
    for (int index = 0; index < m_circulationCards.size() && index < circulationValues.size(); ++index) {
        setMetricValue(m_circulationCards.at(index).value, circulationValues.at(index));
    }

    const struct {
        QString key;
        VLMS::Repositories::MetricsPeriodCounts counts;
    } periods[] = {
        {kPeriodToday, metrics.today},
        {kPeriodWeek, metrics.thisWeek},
        {kPeriodMonth, metrics.thisMonth},
    };
    for (const auto& period : periods) {
        const int row = activityRowForPeriod(m_activityTable, period.key);
        if (row < 0) {
            continue;
        }
        m_activityTable->setItem(row, 1, makeCenteredTableItem(period.counts.checkouts));
        m_activityTable->setItem(row, 2, makeCenteredTableItem(period.counts.returns));
        m_activityTable->setItem(row, 3, makeCenteredTableItem(period.counts.newMembers));
    }

    m_categoriesTable->setRowCount(metrics.topCategories.size());
    for (int row = 0; row < static_cast<int>(metrics.topCategories.size()); ++row) {
        const VLMS::Repositories::MetricsCategoryCount& category = metrics.topCategories.at(row);
        m_categoriesTable->setItem(row, 0, makeCenteredTableItem(qs(category.label)));
        m_categoriesTable->setItem(row, 1, makeCenteredTableItem(category.bookCount));
    }

    const bool hasCategories = !metrics.topCategories.empty();
    m_categoriesTable->setVisible(hasCategories);
    m_categoriesEmpty->setVisible(!hasCategories);
}

void MetricsPage::refreshActivityTable() {
    const struct {
        QString periodKey;
        QString i18nKey;
    } periods[] = {
        {kPeriodToday, QStringLiteral("metrics.period.today")},
        {kPeriodWeek, QStringLiteral("metrics.period.week")},
        {kPeriodMonth, QStringLiteral("metrics.period.month")},
    };

    int fallbackRow = 0;
    for (const auto& period : periods) {
        int row = activityRowForPeriod(m_activityTable, period.periodKey);
        if (row < 0) {
            row = fallbackRow++;
        }
        auto* label = makeCenteredTableItem(T(ss(period.i18nKey)));
        label->setData(Qt::UserRole, period.periodKey);
        m_activityTable->setItem(row, 0, label);
    }
}

void MetricsPage::refreshTopCategoriesTable() {
    m_categoriesTable->setHorizontalHeaderLabels({
        T("metrics.col.category"),
        T("metrics.col.books"),
    });
}

void MetricsPage::retranslateUi() {
    if (auto* title = findChild<QLabel*>(QStringLiteral("pageTitle"))) {
        title->setText(T("page.metrics.title"));
    }
    if (auto* subtitle = findChild<QLabel*>(QStringLiteral("pageSubtitle"))) {
        subtitle->setText(T("page.metrics.body"));
    }

    m_refreshButton->setText(T("metrics.refresh"));

    for (const QLabel* label : findChildren<QLabel*>()) {
        const QString key = label->property("i18nKey").toString();
        if (!key.isEmpty()) {
            const_cast<QLabel*>(label)->setText(T(ss(key)));
        }
    }

    const auto retranslateCards = [](const QVector<MetricCard>& cards) {
        for (const MetricCard& card : cards) {
            if (card.label != nullptr) {
                card.label->setText(T(ss(card.labelKey)));
            }
        }
    };

    retranslateCards(m_overviewCards);
    retranslateCards(m_memberCards);
    retranslateCards(m_circulationCards);

    m_activityTable->setHorizontalHeaderLabels({
        T("metrics.col.period"),
        T("metrics.col.checkouts"),
        T("metrics.col.returns"),
        T("metrics.col.newMembers"),
    });
    refreshActivityTable();

    refreshTopCategoriesTable();
    m_categoriesEmpty->setText(T("metrics.noCategories"));
    refreshMetrics();
}
