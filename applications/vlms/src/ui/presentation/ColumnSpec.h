#pragma once

#include <QString>

namespace VLMS {

struct ColumnSpec {
    QString key;
    QString titleKey;   // Strings::t key
    int width = 100;
    QString sortKey;    // empty = not sortable
};

}  // namespace VLMS
