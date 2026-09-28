#pragma once

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/LoanTypes.h>

#include <QDialog>

class QLabel;
class QTableWidget;

/// The loan history of an archived member, title, or copy: every loan the
/// query names, archived ones included. Read only -- nothing is lent, extended
/// or returned from the Archive, so unlike the Catalogue's and the Members
/// page's history there is no button but Close.
class ArchiveLoansDialog final : public QDialog {
    Q_OBJECT

public:
    /// `query` says whose loans (memberId, bookId, or copyId); its scope,
    /// limit and offset are overridden. `name` goes in the window title.
    ArchiveLoansDialog(VLMS::Repositories::CirculationRepository& repository,
                       const VLMS::Repositories::LoanQuery& query,
                       const QString& name,
                       QWidget* parent = nullptr);

private:
    void buildUi(const QString& name);
    void refresh();

    VLMS::Repositories::CirculationRepository& m_repository;
    VLMS::Repositories::LoanQuery m_query;
    QTableWidget* m_table = nullptr;
    QLabel* m_emptyLabel = nullptr;
};
