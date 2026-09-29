#pragma once

#include <VLMS/Core/Result.h>

#include <QKeySequence>
#include <QList>
#include <QString>

#include <functional>

class QWidget;

namespace VLMS {

struct ActionContext {
    QList<qint64> ids;
    QWidget* parent = nullptr;
};

struct PageAction {
    QString id;
    QString labelKey;
    QKeySequence shortcut;
    enum class Needs { Nothing, One, Some } needs = Needs::Nothing;
    std::function<Core::Status(const ActionContext&)> run;
    std::function<bool(const ActionContext&)> enabled;  // optional extra rule
};

}  // namespace VLMS
