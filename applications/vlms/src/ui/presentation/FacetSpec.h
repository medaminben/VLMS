#pragma once

#include <QString>
#include <QStringList>

#include <functional>

namespace VLMS {

struct FacetSpec {
    QString key;
    QString titleKey;
    /// Fresh values for the facet list (codes or display tokens the page
    /// already understands). Empty loader = static facet filled elsewhere
    /// (e.g. loan open/overdue/returned/all).
    std::function<QStringList()> values;
};

}  // namespace VLMS
