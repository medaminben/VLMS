#pragma once

#include <QList>
#include <QObject>
#include <QtGlobal>

namespace VLMS {

enum class Entity {
    Books = 1,
    Copies = 2,
    Members = 4,
    Loans = 8,
    Categories = 16,
};
Q_DECLARE_FLAGS(EntitySet, Entity)

class DataEvents : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    void emitChanged(EntitySet what, QList<qint64> ids)
    {
        emit changed(what, std::move(ids));
    }

signals:
    void changed(VLMS::EntitySet what, QList<qint64> ids);
};

}  // namespace VLMS

Q_DECLARE_OPERATORS_FOR_FLAGS(VLMS::EntitySet)
