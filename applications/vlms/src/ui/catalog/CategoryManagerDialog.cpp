#include "ui/catalog/CategoryManagerDialog.h"

#include <VLMS/Core/Strings.h>
#include "ui/TableHeaderSort.h"
#include "ui/UiHelpers.h"
#include "QtBridge.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QVariant>
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

}  // namespace

CategoryManagerDialog::CategoryManagerDialog(VLMS::Repositories::CatalogRepository& repository, QWidget* parent)
    : QDialog(parent),
      m_repository(repository) {
    resize(640, 420);
    buildUi();
    retranslateUi();
    refresh();
}

void CategoryManagerDialog::buildUi() {
    auto* layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    VLMS::enableWidgetTableSort(m_table);
    layout->addWidget(m_table);

    m_addButton = VLMS::makeSecondaryButton({});
    m_editButton = VLMS::makeSecondaryButton({});
    m_deleteButton = VLMS::makeSecondaryButton({});

    connect(m_addButton, &QPushButton::clicked, this, &CategoryManagerDialog::addCategory);
    connect(m_editButton, &QPushButton::clicked, this, &CategoryManagerDialog::editCategory);
    connect(m_deleteButton, &QPushButton::clicked, this, &CategoryManagerDialog::deleteCategory);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(m_addButton);
    buttonRow->addWidget(m_editButton);
    buttonRow->addWidget(m_deleteButton);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    VLMS::localizeButtonBox(closeBox);
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(closeBox);
}

void CategoryManagerDialog::retranslateUi()
{
    setWindowTitle(T("category.manageTitle"));

    VLMS::retranslateStandardButtons(findChild<QDialogButtonBox*>());

    m_table->setHorizontalHeaderLabels({
        T("category.col.code"),
        T("category.col.label"),
        T("category.col.books"),
    });

    m_addButton->setText(T("category.add"));
    m_editButton->setText(T("category.edit"));
    m_deleteButton->setText(T("category.delete"));
}

void CategoryManagerDialog::refresh() {
    const auto categoriesResult = m_repository.listAllCategories();
    if (!categoriesResult) {
        VLMS::showRepoError(this, categoriesResult.error());
        return;
    }
    const auto& categories = categoriesResult.value();
    m_table->setRowCount(categories.size());

    for (int row = 0; row < static_cast<int>(categories.size()); ++row) {
        const VLMS::Repositories::CategoryRecord& category = categories.at(row);
        auto* codeItem = new QTableWidgetItem(qs(category.code));
        codeItem->setData(Qt::UserRole, QVariant::fromValue(category.id));
        m_table->setItem(row, 0, codeItem);
        m_table->setItem(row, 1, new QTableWidgetItem(qs(category.label)));
        auto* countItem = new QTableWidgetItem();
        countItem->setData(Qt::DisplayRole, category.bookCount);
        m_table->setItem(row, 2, countItem);
    }
}

int CategoryManagerDialog::selectedCategoryId() const {
    const auto items = m_table->selectedItems();
    if (items.isEmpty()) {
        return 0;
    }
    return m_table->item(items.first()->row(), 0)->data(Qt::UserRole).toInt();
}

void CategoryManagerDialog::addCategory() {
    bool ok = false;
    const QString code = VLMS::askForText(
        this,
        T("category.addTitle"),
        T("category.field.code"),
        QString(),
        &ok);
    if (!ok || code.trimmed().isEmpty()) {
        return;
    }

    const QString label = VLMS::askForText(
        this,
        T("category.addTitle"),
        T("category.field.label"),
        QString(),
        &ok);
    if (!ok) {
        return;
    }

    if (const auto created = m_repository.createCategory(ss(code), ss(label)); !created) {
        VLMS::showRepoError(this, created.error());
        return;
    }

    refresh();
}

void CategoryManagerDialog::editCategory() {
    const int categoryId = selectedCategoryId();
    if (categoryId <= 0) {
        VLMS::showInformation(
            this,
            T("category.editTitle"),
            T("category.selectFirst"));
        return;
    }

    const int row = m_table->currentRow();
    const QString currentCode = m_table->item(row, 0)->text();
    const QString currentLabel = m_table->item(row, 1)->text();

    bool ok = false;
    const QString code = VLMS::askForText(
        this,
        T("category.editTitle"),
        T("category.field.code"),
        currentCode,
        &ok);
    if (!ok || code.trimmed().isEmpty()) {
        return;
    }

    const QString label = VLMS::askForText(
        this,
        T("category.editTitle"),
        T("category.field.label"),
        currentLabel,
        &ok);
    if (!ok) {
        return;
    }

    if (const auto updated = m_repository.updateCategory(categoryId, ss(code), ss(label)); !updated) {
        VLMS::showRepoError(this, updated.error());
        return;
    }

    refresh();
}

void CategoryManagerDialog::deleteCategory() {
    const int categoryId = selectedCategoryId();
    if (categoryId <= 0) {
        VLMS::showInformation(
            this,
            T("category.deleteTitle"),
            T("category.selectFirst"));
        return;
    }

    if (!VLMS::askYesNo(this,
                              T("category.deleteTitle"),
                              T("category.deleteConfirm"))) {
        return;
    }

    if (const auto removed = m_repository.deleteCategory(categoryId); !removed) {
        VLMS::showRepoError(this, removed.error());
        return;
    }

    refresh();
}
