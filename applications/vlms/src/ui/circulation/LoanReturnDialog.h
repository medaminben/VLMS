#pragma once

#include <VLMS/Repositories/LoanTypes.h>

#include <QDialog>

class QDateEdit;
class QPlainTextEdit;

class LoanReturnDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LoanReturnDialog(const VLMS::Repositories::LoanRecord& loan, QWidget* parent = nullptr);

    [[nodiscard]] QString returnedAt() const;
    [[nodiscard]] QString notes() const;

private:
    void buildUi(const VLMS::Repositories::LoanRecord& loan);
    void retranslateUi(const VLMS::Repositories::LoanRecord& loan);

    QDateEdit* m_returnedDateEdit = nullptr;
    QPlainTextEdit* m_notesEdit = nullptr;
};
