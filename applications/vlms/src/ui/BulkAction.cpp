#include "ui/BulkAction.h"

#include "QtBridge.h"
#include "ui/UiHelpers.h"

#include <VLMS/Core/Strings.h>

#include <QProgressDialog>
#include <QStringList>

namespace {

QString fill(const char* key, const QList<QPair<QString, QString>>& pairs)
{
    QString text = VLMS::qs(VLMS::Strings::t(key));
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

int runBulkAction(QWidget* parent,
                  const BulkActionTexts& texts,
                  const QList<BulkRow>& rows,
                  const std::function<VLMS::Status(const BulkRow&)>& check,
                  const std::function<VLMS::Status(const BulkRow&)>& act)
{
    QList<BulkRow> passes;
    QList<BlockedRow> blocked;
    for (const BulkRow& row : rows) {
        const VLMS::Status gate = check(row);
        if (gate) {
            passes.append(row);
        } else {
            blocked.append(BlockedRow{row.label, VLMS::qs(VLMS::Strings::t(gate.error().key))});
        }
    }

    const int total = rows.size();
    if (total == 1 && !blocked.isEmpty()) {
        // One tick is one record: "0 of 1 books" would be counting for its own sake.
        VLMS::showInformation(parent, texts.title,
                                    blocked.first().label + QStringLiteral(" — ")
                                        + blocked.first().reason);
        return 0;
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
        if (!VLMS::askYesNo(parent, texts.title, question)) {
            return 0;
        }
    } else if (passes.isEmpty()) {
        VLMS::showInformation(parent, texts.title,
                                    blockedText(texts, total, blocked, 0, false));
        return 0;
    } else if (!VLMS::askYesNo(parent, texts.title,
                                     blockedText(texts, total, blocked, passes.size(), true))) {
        return 0;
    }

    QProgressDialog progress(parent);
    progress.setWindowTitle(texts.title);
    progress.setMinimumDuration(500);
    progress.setWindowModality(Qt::WindowModal);
    progress.setRange(0, passes.size());
    progress.setValue(0);

    int done = 0;
    int failed = 0;
    QString firstReason;
    bool cancelled = false;
    for (int i = 0; i < passes.size(); ++i) {
        if (progress.wasCanceled()) {
            cancelled = true;
            break;
        }
        progress.setLabelText(passes.at(i).label);
        progress.setValue(i);
        const VLMS::Status result = act(passes.at(i));
        if (result) {
            ++done;
        } else {
            ++failed;
            if (firstReason.isEmpty()) {
                firstReason = VLMS::qs(VLMS::Strings::t(result.error().key));
            }
        }
    }
    progress.setValue(passes.size());

    if (failed > 0 || cancelled) {
        VLMS::showInformation(
            parent, texts.title,
            fill("bulk.summary",
                 {{QStringLiteral("done"), QString::number(done)},
                  {QStringLiteral("passive"), texts.passive},
                  {QStringLiteral("failed"), QString::number(failed)},
                  {QStringLiteral("reason"), firstReason}}));
    }
    return done;
}
