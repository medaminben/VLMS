#pragma once

#include <VLMS/Repositories/LoanTypes.h>

#include <QDialog>

class QDateEdit;
class QLabel;

class LoanExtendDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LoanExtendDialog(const VLMS::Repositories::LoanRecord& loan, QWidget* parent = nullptr);

    [[nodiscard]] QString dueAt() const;

private:
    void buildUi(const VLMS::Repositories::LoanRecord& loan);
    void retranslateUi(const VLMS::Repositories::LoanRecord& loan);

    QDateEdit* m_dueDateEdit = nullptr;
    QLabel* m_currentDueLabel = nullptr;
};
