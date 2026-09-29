#include "ui/workflows/BulkFlow.h"

#include "QtBridge.h"

#include <VLMS/Core/Strings.h>

#include <QStringList>

namespace VLMS {
namespace {

QString fill(const char* key, const QList<QPair<QString, QString>>& pairs)
{
    QString text = qs(Core::Strings::t(key));
    for (const auto& pair : pairs) {
        text.replace(QStringLiteral("{%1}").arg(pair.first), pair.second);
    }
    return text;
}

struct BlockedRow {
    QString label;
    QString reason;
};

QString blockedText(const BulkActionTexts& texts,
                    int total,
                    const QList<BlockedRow>& blocked,
                    int passCount,
                    bool offerRest)
{
    QStringList lines;
    lines.append(fill("bulk.blockedIntro",
                      {{QStringLiteral("blocked"), QString::number(blocked.size())},
                       {QStringLiteral("count"), QString::number(total)},
                       {QStringLiteral("noun"), texts.noun},
                       {QStringLiteral("passive"), texts.passive}}));
    const int shown = qMin(10, blocked.size());
    for (int i = 0; i < shown; ++i) {
        lines.append(blocked.at(i).label + QStringLiteral(" — ") + blocked.at(i).reason);
    }
    if (blocked.size() > shown) {
        lines.append(fill("bulk.andMore",
                          {{QStringLiteral("count"), QString::number(blocked.size() - shown)}}));
    }
    if (offerRest) {
        lines.append(fill("bulk.confirmRest",
                          {{QStringLiteral("verb"), texts.verb},
                           {QStringLiteral("count"), QString::number(passCount)}}));
    }
    return lines.join(QLatin1Char('\n'));
}

}  // namespace

BulkOutcome runBulkFlow(BulkUi& ui,
                        const BulkActionTexts& texts,
                        const QList<BulkRow>& rows,
                        const std::function<Core::Status(const BulkRow&)>& check,
                        const std::function<Core::Status(const BulkRow&)>& act)
{
    BulkOutcome outcome;
    QList<BulkRow> passes;
    QList<BlockedRow> blocked;
    for (const BulkRow& row : rows) {
        const Core::Status gate = check(row);
        if (gate) {
            passes.append(row);
        } else {
            blocked.append(BlockedRow{row.label, qs(Core::Strings::t(gate.error().key))});
        }
    }

    const int total = rows.size();
    if (total == 1 && !blocked.isEmpty()) {
        // One tick is one record: "0 of 1 books" would be counting for its own sake.
        ui.inform(texts.title, blocked.first().label + QStringLiteral(" — ") + blocked.first().reason);
        return outcome;
    }
    if (blocked.isEmpty()) {
        // A single record is named rather than counted, so no language has to
        // put "1" in front of a plural.
        const QString question =
            total == 1 ? fill("bulk.confirmOne",
                              {{QStringLiteral("verb"), texts.verb},
                               {QStringLiteral("label"), passes.first().label}})
                       : fill("bulk.confirm",
                              {{QStringLiteral("verb"), texts.verb},
                               {QStringLiteral("count"), QString::number(passes.size())},
                               {QStringLiteral("noun"), texts.noun}});
        if (!ui.confirm(texts.title, question)) {
            return outcome;
        }
    } else if (passes.isEmpty()) {
        ui.inform(texts.title, blockedText(texts, total, blocked, 0, false));
        return outcome;
    } else if (!ui.confirm(texts.title, blockedText(texts, total, blocked, passes.size(), true))) {
        return outcome;
    }

    const std::unique_ptr<BulkProgress> progress = ui.startProgress(texts.title, passes.size());
    int failed = 0;
    QString firstReason;
    bool cancelled = false;
    for (int i = 0; i < passes.size(); ++i) {
        if (progress->cancelled()) {
            cancelled = true;
            break;
        }
        progress->step(i, passes.at(i).label);
        const Core::Status result = act(passes.at(i));
        if (result) {
            ++outcome.done;
            outcome.succeeded.append(passes.at(i).id);
        } else {
            ++failed;
            if (firstReason.isEmpty()) {
                firstReason = qs(Core::Strings::t(result.error().key));
            }
        }
    }
    progress->step(passes.size(), QString());

    if (failed > 0 || cancelled) {
        ui.inform(texts.title,
                  fill("bulk.summary",
                       {{QStringLiteral("done"), QString::number(outcome.done)},
                        {QStringLiteral("passive"), texts.passive},
                        {QStringLiteral("failed"), QString::number(failed)},
                        {QStringLiteral("reason"), firstReason}}));
    }
    return outcome;
}

}  // namespace VLMS
