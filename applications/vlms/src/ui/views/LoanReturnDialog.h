#pragma once

#include <VLMS/Repositories/LoanTypes.h>

#include <QDialog>

class QEvent;

class QDateEdit;
class QPlainTextEdit;

class LoanReturnDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LoanReturnDialog(const VLMS::Repositories::LoanRecord& loan, QWidget* parent = nullptr);

    [[nodiscard]] QString returnedAt() const;
    [[nodiscard]] QString notes() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void buildUi(const VLMS::Repositories::LoanRecord& loan);
    void applyLabels();

    VLMS::Repositories::LoanRecord m_loan;
    QDateEdit* m_returnedDateEdit = nullptr;
    QPlainTextEdit* m_notesEdit = nullptr;
};
