#pragma once

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/LoanTypes.h>

#include <QDialog>
#include <QString>

class QComboBox;
class QDateEdit;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

/// Fixes one side of a checkout. Zero on both is the unscoped dialog the
/// Circulation page opens; a book id offers only that book's free copies, and
/// a member id replaces the member rows with the name.
struct LoanScope {
    qint64 bookId = 0;
    qint64 memberId = 0;
    QString label;
};

class LoanCheckoutDialog final : public QDialog {
    Q_OBJECT

public:
    LoanCheckoutDialog(CirculationRepository& repository,
                       const LoanScope& scope = {},
                       QWidget* parent = nullptr);

    [[nodiscard]] LoanInput loanInput() const;

    /// False when the scope left nothing to lend — every copy is out.
    [[nodiscard]] bool hasLendableCopy() const;

private:
    void buildUi();
    void retranslateUi();
    void refreshMembers();
    void refreshCopies();

    CirculationRepository& m_repository;
    LoanScope m_scope;

    QLineEdit* m_memberSearchEdit = nullptr;
    QComboBox* m_memberCombo = nullptr;
    QLineEdit* m_copySearchEdit = nullptr;
    QComboBox* m_copyCombo = nullptr;
    QDateEdit* m_borrowedDateEdit = nullptr;
    QDateEdit* m_dueDateEdit = nullptr;
    QPlainTextEdit* m_notesEdit = nullptr;
    QLabel* m_memberFixedLabel = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};
