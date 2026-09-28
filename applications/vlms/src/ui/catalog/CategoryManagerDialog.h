#pragma once

#include <VLMS/Repositories/CatalogRepository.h>

#include <QDialog>

class QPushButton;
class QTableWidget;

class CategoryManagerDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CategoryManagerDialog(VLMS::Repositories::CatalogRepository& repository, QWidget* parent = nullptr);

private slots:
    void refresh();
    void addCategory();
    void editCategory();
    void deleteCategory();

private:
    void buildUi();
    void retranslateUi();
    int selectedCategoryId() const;

    VLMS::Repositories::CatalogRepository& m_repository;
    QTableWidget* m_table = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
};
