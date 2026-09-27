#pragma once

#include <VLMS/Core/CirculationRepository.h>

#include <QDialog>

class QLabel;
class QTableWidget;
class LoanHistoryActions;

/// Every loan ever made of one book, across every copy it has ever had.
/// The member dialog's twin; they differ in their query, their title and one
/// column, which is less than a shared class with a mode flag would cost.
class BookLoansDialog final : public QDialog {
    Q_OBJECT

public:
    BookLoansDialog(CirculationRepository& repository,
                    qint64 bookId,
                    const QString& bookTitle,
                    QWidget* parent = nullptr);

private:
    void buildUi();
    void retranslateUi();
    void refresh();
    void lendACopy();
    QString loanStatusLabel(const LoanRecord& loan) const;

    CirculationRepository& m_repository;
    qint64 m_bookId = 0;
    QString m_bookTitle;
    QTableWidget* m_table = nullptr;
    QLabel* m_emptyLabel = nullptr;
    LoanHistoryActions* m_actions = nullptr;
};
