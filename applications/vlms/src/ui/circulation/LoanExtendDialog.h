#pragma once

#include <VLMS/Repositories/LoanTypes.h>

#include <QDialog>

class QDateEdit;
class QLabel;

class LoanExtendDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LoanExtendDialog(const LoanRecord& loan, QWidget* parent = nullptr);

    [[nodiscard]] QString dueAt() const;

private:
    void buildUi(const LoanRecord& loan);
    void retranslateUi(const LoanRecord& loan);

    QDateEdit* m_dueDateEdit = nullptr;
    QLabel* m_currentDueLabel = nullptr;
};
