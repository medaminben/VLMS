#pragma once

#include <VLMS/Repositories/ArchiveTypes.h>

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>

namespace VLMS {

struct LoanScope {
    qint64 memberId = 0;
    qint64 bookId = 0;
    qint64 copyId = 0;
};

struct QueryState {
    QString search;
    QHash<QString, QStringList> facets;  // facet key -> ticked values
    QString sortKey;
    bool ascending = true;
    int pageSize = 0;  // 0 = Show All (today's default)
    int page = 1;
    Repositories::ArchiveScope scope = Repositories::ArchiveScope::Live;
    std::optional<LoanScope> pinned;
};

}  // namespace VLMS
