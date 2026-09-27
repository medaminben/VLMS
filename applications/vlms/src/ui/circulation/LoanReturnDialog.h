#pragma once

#include <VLMS/Core/LoanTypes.h>

#include <QDialog>

class QDateEdit;
class QPlainTextEdit;

class LoanReturnDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LoanReturnDialog(const LoanRecord& loan, QWidget* parent = nullptr);

    [[nodiscard]] QString returnedAt() const;
    [[nodiscard]] QString notes() const;

private:
    void buildUi(const LoanRecord& loan);
    void retranslateUi(const LoanRecord& loan);

    QDateEdit* m_returnedDateEdit = nullptr;
    QPlainTextEdit* m_notesEdit = nullptr;
};
