#pragma once

#include <VLMS/Repositories/LoanTypes.h>

#include <QDialog>

class QEvent;

class QDateEdit;
class QLabel;

class LoanExtendDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LoanExtendDialog(const VLMS::Repositories::LoanRecord& loan, QWidget* parent = nullptr);

    [[nodiscard]] QString dueAt() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void buildUi(const VLMS::Repositories::LoanRecord& loan);
    void applyLabels();

    VLMS::Repositories::LoanRecord m_loan;
    QDateEdit* m_dueDateEdit = nullptr;
    QLabel* m_currentDueLabel = nullptr;
};
