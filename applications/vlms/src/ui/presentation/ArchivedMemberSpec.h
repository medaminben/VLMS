#pragma once

#include "ui/presentation/ListSpec.h"
#include "ui/workflows/ArchiveWorkflow.h"

#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QStringList>
#include <QVector>

#include <vector>

namespace VLMS {

class ArchivedMemberSpec final : public ListSpec {
public:
    ArchivedMemberSpec(Repositories::MemberRepository& members, ArchiveWorkflow& workflow);

    QString id() const override;
    QList<ColumnSpec> columns() const override;
    QList<FacetSpec> facets() const override;
    QList<PageAction> actions() const override;
    EntitySet dependsOn() const override;

    Core::Status load(const QueryState& state) override;
    void retranslate() override;
    int rowCount() const override;
    int totalCount() const override;
    qint64 idAt(int row) const override;
    QVariant cell(int row, int column, int role) const override;
    Core::Result<int> rankOf(qint64 id, const QueryState& state) const override;
    PreviewContent preview(qint64 id) const override;

private:
    struct DisplayRow {
        qint64 id = 0;
        QStringList cells;
    };

    [[nodiscard]] Repositories::MemberQuery toQuery(const QueryState& state) const;
    [[nodiscard]] static QString displayName(const Repositories::MemberRecord& member);
    [[nodiscard]] static QStringList buildCells(const Repositories::MemberRecord& member);
    void rebuildDisplayRows();

    Repositories::MemberRepository& m_members;
    ArchiveWorkflow& m_workflow;
    std::vector<Repositories::MemberRecord> m_records;
    QVector<DisplayRow> m_rows;
    int m_total = 0;
};

}  // namespace VLMS
