#pragma once

#include <VLMS/Core/Result.h>

#include <QList>
#include <QString>

#include <functional>

class QWidget;

struct BulkRow {
    qint64 id = 0;
    int row = -1;
    QString label;
};

/// Words already translated. The shared prompt templates live in Strings.cpp
/// (`bulk.confirm`, `bulk.confirmOne`, `bulk.blockedIntro`, `bulk.andMore`, `bulk.confirmRest`,
/// `bulk.summary`).
struct BulkActionTexts {
    QString title;
    QString verb;
    QString passive;
    QString noun;
};

/// Sorts `rows` into passes and blocked, asks, then runs `act` on the passes.
/// Returns how many calls to `act` succeeded. Zero means the caller should
/// leave the ticks alone: the librarian said no, or every row was blocked.
int runBulkAction(QWidget* parent,
                  const BulkActionTexts& texts,
                  const QList<BulkRow>& rows,
                  const std::function<VLMS::Status(const BulkRow&)>& check,
                  const std::function<VLMS::Status(const BulkRow&)>& act);
