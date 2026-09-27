#include "ui/TablePager.h"

#include <VLMS/Core/Strings.h>
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpacerItem>
#include <QSpinBox>

using VLMS::T;

namespace VLMS {

namespace {

constexpr int kPageSizeChoices[] = {20, 50, 100};

}  // namespace

TablePager::TablePager(QWidget* parent)
    : QWidget(parent) {
    setLayoutDirection(Qt::LayoutDirectionAuto);

    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(0);
    grid->setAlignment(Qt::AlignVCenter);

    m_firstButton = makeSecondaryButton({});
    m_firstButton->setObjectName(QStringLiteral("pagerFirst"));
    m_previousButton = makeSecondaryButton({});
    m_previousButton->setObjectName(QStringLiteral("pagerPrevious"));
    m_nextButton = makeSecondaryButton({});
    m_nextButton->setObjectName(QStringLiteral("pagerNext"));
    m_lastButton = makeSecondaryButton({});
    m_lastButton->setObjectName(QStringLiteral("pagerLast"));
    for (QPushButton* nav : {m_firstButton, m_previousButton, m_nextButton, m_lastButton}) {
        nav->setMaximumHeight(28);
    }

    m_pageSizeCombo = new QComboBox(this);
    m_pageSizeCombo->setObjectName(QStringLiteral("pagerPageSize"));
    m_pageSizeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_pageSizeCombo->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_pageSizeCombo->setMaximumHeight(28);

    m_rangeLabel = new QLabel(this);
    m_rangeLabel->setObjectName(QStringLiteral("pagerRange"));
    m_pageLabel = new QLabel(this);
    m_pageLabel->setObjectName(QStringLiteral("pagerPage"));
    m_pageSpin = new QSpinBox(this);
    m_pageSpin->setObjectName(QStringLiteral("pagerPageSpin"));
    m_pageSpin->setKeyboardTracking(false);
    m_pageSpin->setMinimum(1);
    m_pageSpin->setMaximum(1);
    m_pageSpin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_pageSpin->setMaximumHeight(28);
    m_pagesLabel = new QLabel(this);
    m_pagesLabel->setObjectName(QStringLiteral("pagerPages"));

    connect(m_firstButton, &QPushButton::clicked, this, &TablePager::goFirst);
    connect(m_previousButton, &QPushButton::clicked, this, &TablePager::goPrevious);
    connect(m_nextButton, &QPushButton::clicked, this, &TablePager::goNext);
    connect(m_lastButton, &QPushButton::clicked, this, &TablePager::goLast);
    connect(m_pageSpin, &QSpinBox::valueChanged, this, &TablePager::goToPage);

    fillPageSizeCombo();
    connect(m_pageSizeCombo, &QComboBox::currentIndexChanged, this, &TablePager::onPageSizeChosen);

    constexpr int row = 0;
    int column = 0;
    const auto addCell = [&](QWidget* widget) {
        grid->addWidget(widget, row, column++, Qt::AlignVCenter);
    };
    addCell(m_firstButton);
    addCell(m_previousButton);
    grid->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum),
                  row, column++);
    addCell(m_pageSizeCombo);
    addCell(m_rangeLabel);
    addCell(m_pageLabel);
    addCell(m_pageSpin);
    addCell(m_pagesLabel);
    grid->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum),
                  row, column++);
    addCell(m_nextButton);
    addCell(m_lastButton);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(8, 1);

    retranslateUi();
    updateControls();
}

void TablePager::fillPageSizeCombo()
{
    const QSignalBlocker blocker(m_pageSizeCombo);
    m_pageSizeCombo->clear();
    m_pageSizeCombo->addItem(QString(), kShowAllSize);
    for (const int size : kPageSizeChoices) {
        m_pageSizeCombo->addItem(QString::number(size), size);
    }
    syncPageSizeCombo();
}

void TablePager::syncPageSizeCombo()
{
    const int wanted = m_showAll ? kShowAllSize : m_pageSize;
    for (int i = 0; i < m_pageSizeCombo->count(); ++i) {
        if (m_pageSizeCombo->itemData(i).toInt() == wanted) {
            m_pageSizeCombo->setCurrentIndex(i);
            return;
        }
    }
}

void TablePager::setPageSize(int pageSize)
{
    m_showAll = false;
    m_pageSize = qMax(1, pageSize);
    {
        const QSignalBlocker blocker(m_pageSizeCombo);
        syncPageSizeCombo();
    }
    updateControls();
}

int TablePager::pageSize() const
{
    if (m_showAll) {
        return m_totalCount > 0 ? m_totalCount : 1;
    }
    return m_pageSize;
}

void TablePager::setTotalCount(int totalCount)
{
    m_totalCount = qMax(0, totalCount);
    const int pages = pageCount();
    if (m_currentPage > pages) {
        m_currentPage = pages;
    }
    if (m_currentPage < 1) {
        m_currentPage = 1;
    }
    updateControls();
}

int TablePager::pageCount() const
{
    if (m_totalCount <= 0) {
        return 1;
    }
    const int size = pageSize();
    return (m_totalCount + size - 1) / size;
}

int TablePager::offset() const
{
    return (m_currentPage - 1) * pageSize();
}

void TablePager::resetToFirstPage()
{
    m_currentPage = 1;
    updateControls();
}

void TablePager::setCurrentPage(int page)
{
    const int pages = pageCount();
    m_currentPage = qBound(1, page, pages);
    updateControls();
}

void TablePager::retranslateUi()
{
    m_firstButton->setText(T("pager.first"));
    m_previousButton->setText(T("pager.previous"));
    m_nextButton->setText(T("pager.next"));
    m_lastButton->setText(T("pager.last"));
    m_pageLabel->setText(T("pager.page"));
    if (m_pageSizeCombo->count() > 0) {
        m_pageSizeCombo->setItemText(0, T("pager.all"));
    }
    updateControls();
}

void TablePager::goFirst()
{
    goToPage(1);
}

void TablePager::goPrevious()
{
    if (m_currentPage <= 1) {
        return;
    }
    goToPage(m_currentPage - 1);
}

void TablePager::goNext()
{
    if (m_currentPage >= pageCount()) {
        return;
    }
    goToPage(m_currentPage + 1);
}

void TablePager::goLast()
{
    goToPage(pageCount());
}

void TablePager::goToPage(int page)
{
    const int pages = pageCount();
    const int next = qBound(1, page, pages);
    if (next == m_currentPage) {
        return;
    }
    m_currentPage = next;
    updateControls();
    emit pageChanged(m_currentPage);
}

void TablePager::onPageSizeChosen(int index)
{
    const int size = m_pageSizeCombo->itemData(index).toInt();
    m_showAll = size <= 0;
    if (!m_showAll) {
        m_pageSize = qMax(1, size);
    }
    m_currentPage = 1;
    updateControls();
    emit pageChanged(m_currentPage);
}

void TablePager::updateControls()
{
    const int pages = pageCount();
    setVisible(m_totalCount >= kMinVisibleCount);

    m_firstButton->setEnabled(m_currentPage > 1);
    m_previousButton->setEnabled(m_currentPage > 1);
    m_nextButton->setEnabled(m_currentPage < pages);
    m_lastButton->setEnabled(m_currentPage < pages);

    const int size = pageSize();
    const int from = m_totalCount == 0 ? 0 : offset() + 1;
    const int to = qMin(offset() + size, m_totalCount);

    QString range = T("pager.range");
    range.replace(QStringLiteral("{from}"), QString::number(from));
    range.replace(QStringLiteral("{to}"), QString::number(to));
    range.replace(QStringLiteral("{total}"), QString::number(m_totalCount));
    m_rangeLabel->setText(range);
    m_pagesLabel->setText(QStringLiteral("/%1").arg(pages));

    const QSignalBlocker spinBlocker(m_pageSpin);
    m_pageSpin->setRange(1, pages);
    m_pageSpin->setValue(m_currentPage);
}

}  // namespace VLMS
