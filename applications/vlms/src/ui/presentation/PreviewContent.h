#pragma once

#include <QString>
#include <QVector>

namespace VLMS {

struct PreviewField {
    QString labelKey;
    QString value;
    bool scrollable = false;
};

struct PreviewContent {
    QString imagePath;
    QString secondImagePath;
    QVector<PreviewField> fields;
};

}  // namespace VLMS
