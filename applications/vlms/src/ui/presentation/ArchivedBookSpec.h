#pragma once

#include "ui/presentation/ListSpec.h"
#include "ui/workflows/ArchiveWorkflow.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Repositories/CirculationRepository.h>

#include <QStringList>
#include <QVector>

#include <vector>

namespace VLMS {

class ArchivedBookSpec final : public ListSpec {
public:
    ArchivedBookSpec(Repositories::CatalogRepository& catalog,
                     Repositories::CirculationRepository& circulation,
                     ArchiveWorkflow& workflow);

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

    [[nodiscard]] Repositories::BookQuery toQuery(const QueryState& state) const;
    [[nodiscard]] static QStringList buildCells(const Repositories::BookRecord& book);
    void rebuildDisplayRows();

    Repositories::CatalogRepository& m_catalog;
    Repositories::CirculationRepository& m_circulation;
    ArchiveWorkflow& m_workflow;
    std::vector<Repositories::BookRecord> m_records;
    QVector<DisplayRow> m_rows;
    int m_total = 0;
};

}  // namespace VLMS
