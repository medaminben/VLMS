#pragma once

#include "ui/presentation/ColumnSpec.h"
#include "ui/presentation/FacetSpec.h"
#include "ui/presentation/PageAction.h"
#include "ui/presentation/PreviewContent.h"
#include "ui/presentation/QueryState.h"
#include "ui/workflows/DataEvents.h"

#include <VLMS/Core/Result.h>

#include <QList>
#include <QString>
#include <QVariant>

namespace VLMS {

class ListSpec {
public:
    virtual ~ListSpec() = default;

    virtual QString id() const = 0;
    virtual QList<ColumnSpec> columns() const = 0;
    virtual QList<FacetSpec> facets() const = 0;
    virtual QList<PageAction> actions() const = 0;
    virtual EntitySet dependsOn() const = 0;

    virtual Core::Status load(const QueryState& state) = 0;
    virtual void retranslate() = 0;
    virtual int rowCount() const = 0;
    virtual int totalCount() const = 0;
    virtual qint64 idAt(int row) const = 0;
    virtual QVariant cell(int row, int column, int role) const = 0;
    virtual Core::Result<int> rankOf(qint64 id, const QueryState& state) const = 0;
    virtual PreviewContent preview(qint64 id) const = 0;
};

}  // namespace VLMS
