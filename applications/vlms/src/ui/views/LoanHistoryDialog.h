#pragma once

#include "ui/presentation/PageAction.h"
#include "ui/presentation/QueryState.h"

#include <VLMS/Repositories/ArchiveTypes.h>

#include <QDialog>

class QEvent;
#include <QString>

#include <cstdint>

class QLabel;
class QTableView;

namespace VLMS {

class ActionBar;
class DataEvents;
class LoanListSpec;
class LoanWorkflow;
class PagedQueryModel;

/// Pinned loan history for a book, member, or archived entity. Uses a dedicated
/// LoanListSpec (not Circulation's) so QueryState.pinned never corrupts the
/// live Circulation page.
class LoanHistoryDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Kind { Book, Member, ArchivedMember, ArchivedBook, ArchivedCopy };

    LoanHistoryDialog(LoanListSpec& loansSpec,
                      LoanWorkflow& loans,
                      DataEvents& events,
                      Kind kind,
                      qint64 pinnedId,
                      const QString& title,
                      Repositories::ArchiveScope scope,
                      QWidget* parent = nullptr);

    [[nodiscard]] const QueryState& queryState() const;
    [[nodiscard]] PagedQueryModel* model() const;
    [[nodiscard]] QTableView* table() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void buildUi();
    void applyLabels();
    void applyPinnedQuery();
    void updateEmptyState();
    void updateActionEnabled();
    [[nodiscard]] QList<PageAction> historyActions();
    [[nodiscard]] ActionContext currentContext();
    [[nodiscard]] qint64 selectedLoanId() const;
    [[nodiscard]] bool mutableKind() const;
    [[nodiscard]] QString titleKey() const;
    [[nodiscard]] QString emptyKey() const;

    LoanListSpec& m_loansSpec;
    LoanWorkflow& m_loans;
    DataEvents& m_events;
    Kind m_kind = Kind::Book;
    qint64 m_pinnedId = 0;
    QString m_titleName;
    Repositories::ArchiveScope m_scope = Repositories::ArchiveScope::Any;

    PagedQueryModel* m_model = nullptr;
    QTableView* m_table = nullptr;
    QLabel* m_emptyLabel = nullptr;
    ActionBar* m_actionBar = nullptr;  // null for archived history: read-only
};

}  // namespace VLMS
