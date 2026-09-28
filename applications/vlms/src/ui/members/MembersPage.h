#pragma once

#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include "ui/TableHeaderSort.h"

#include <QString>
#include <QVector>
#include <QWidget>

#include <string_view>

class QLineEdit;
class QEvent;
class QListWidget;
class QPushButton;
class QTableWidget;
class TableRowChecks;
class QLabel;

namespace VLMS {
class MemberFacetFilters;
class TablePager;
}

class MembersPage final : public QWidget {
    Q_OBJECT

public:
    explicit MembersPage(VLMS::Repositories::MemberRepository& repository,
                         VLMS::Repositories::CirculationRepository& circulationRepository,
                         QWidget* parent = nullptr);

    void retranslateUi();

signals:
    /// Asks the window to show the circulation page narrowed to this member.
    /// Emitted when a removal is blocked by books the member still has out.
    void memberLoansRequested(const QString& membershipNumber);

private slots:
    void refreshMembers();
    void onSearchChanged();
    void addMember();
    void editMember();
    void deleteMember();
    void showLoanHistory();
    void onSelectionChanged();
    void onSortChanged(int column, bool ascending);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct MemberDetailRow {
        QString labelKey;
        QLabel* label = nullptr;
        QWidget* value = nullptr;
    };

    void buildUi();
    void resetPagerAndRefresh();
    VLMS::Repositories::MemberQuery currentMemberQuery() const;
    qint64 selectedMemberId() const;
    void selectMemberId(qint64 id);
    void refreshAllFilters();
    void updatePreview(const VLMS::Repositories::MemberRecord& member);
    void renderPhotoPreview();
    void clearMemberDetails();
    void refreshSelectedMemberPreview();

    VLMS::Repositories::MemberRepository& m_repository;
    VLMS::Repositories::CirculationRepository& m_circulationRepository;

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_loansButton = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_editButton = nullptr;
    QPushButton* m_deleteButton = nullptr;
    VLMS::MemberFacetFilters* m_filters = nullptr;
    QTableWidget* m_membersTable = nullptr;
    TableRowChecks* m_checks = nullptr;
    VLMS::TableHeaderSort* m_sort = nullptr;
    VLMS::TablePager* m_pager = nullptr;
    QLabel* m_photoPreview = nullptr;
    QWidget* m_previewPanel = nullptr;
    QWidget* m_detailsPanel = nullptr;
    QVector<MemberDetailRow> m_detailRows;
    QString m_previewPhotoPath;
};
