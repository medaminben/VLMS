#pragma once

#include "ui/presentation/ListSpec.h"
#include "ui/workflows/LoanWorkflow.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Repositories/LoanTypes.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <QStringList>
#include <QVector>

#include <vector>

namespace VLMS {

class LoanListSpec final : public ListSpec {
public:
    LoanListSpec(Repositories::CirculationRepository& circulation,
                 Repositories::CatalogRepository& catalog,
                 Repositories::MemberRepository& members,
                 LoanWorkflow& workflow);

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

    [[nodiscard]] Repositories::LoanQuery toQuery(const QueryState& state) const;
    [[nodiscard]] static QString statusLabel(const Repositories::LoanRecord& loan);
    [[nodiscard]] static QStringList buildCells(const Repositories::LoanRecord& loan);
    [[nodiscard]] static QString displayLabel(const Repositories::LoanRecord& loan);
    void rebuildDisplayRows();

    Repositories::CirculationRepository& m_circulation;
    Repositories::CatalogRepository& m_catalog;
    Repositories::MemberRepository& m_members;
    LoanWorkflow& m_workflow;
    std::vector<Repositories::LoanRecord> m_records;
    QVector<DisplayRow> m_rows;
    int m_total = 0;
};

}  // namespace VLMS
