#pragma once

namespace VLMS {
class CategoryEditor;
}

#include <QDialog>

class QEvent;

class QPushButton;
class QTableWidget;

class CategoryManagerDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CategoryManagerDialog(VLMS::CategoryEditor& editor, QWidget* parent = nullptr);

private slots:
    void refresh();
    void addCategory();
    void editCategory();
    void deleteCategory();

protected:
    void changeEvent(QEvent* event) override;

private:
    void buildUi();
    void applyLabels();
    int selectedCategoryId() const;

    VLMS::CategoryEditor& m_editor;
    QTableWidget* m_table = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
};
