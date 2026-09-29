#pragma once

#include <VLMS/Core/Result.h>

#include <QList>
#include <QString>

#include <functional>
#include <memory>

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

namespace VLMS {

/// Progress of a running bulk action. `step` reports the row about to run.
class BulkProgress {
public:
    virtual ~BulkProgress() = default;
    virtual void step(int done, const QString& label) = 0;
    [[nodiscard]] virtual bool cancelled() const = 0;
};

/// Everything a bulk action shows the librarian. Implemented over Qt message
/// boxes for widgets and over DialogFactory for workflows.
class BulkUi {
public:
    virtual ~BulkUi() = default;
    virtual bool confirm(const QString& title, const QString& text) = 0;
    virtual void inform(const QString& title, const QString& text) = 0;
    virtual std::unique_ptr<BulkProgress> startProgress(const QString& title, int total) = 0;
};

struct BulkOutcome {
    int done = 0;
    QList<qint64> succeeded;
};

/// Sorts `rows` into passes and blocked, asks, then runs `act` on the passes.
/// `done` counts the calls to `act` that succeeded. Zero means the caller
/// should leave the ticks alone: the librarian said no, or every row was
/// blocked. Failed rows end in one summary.
BulkOutcome runBulkFlow(BulkUi& ui,
                        const BulkActionTexts& texts,
                        const QList<BulkRow>& rows,
                        const std::function<Core::Status(const BulkRow&)>& check,
                        const std::function<Core::Status(const BulkRow&)>& act);

}  // namespace VLMS
