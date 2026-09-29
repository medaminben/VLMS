#include "ui/presentation/ArchivedCopySpec.h"

#include "QtBridge.h"
#include "ui/UiHelpers.h"

#include <VLMS/Repositories/ArchiveTypes.h>

#include <Qt>
#include <limits>

using VLMS::T;
using VLMS::qs;
using VLMS::ss;

namespace VLMS {
namespace {

std::vector<std::string> facetValues(const QueryState& state, const QString& key)
{
    std::vector<std::string> values;
    const auto it = state.facets.constFind(key);
    if (it == state.facets.cend()) {
        return values;
    }
    values.reserve(static_cast<size_t>(it->size()));
    for (const QString& value : *it) {
        values.push_back(ss(value));
    }
    return values;
}

Repositories::CoverFilter coverFilterFromState(const QueryState& state)
{
    const auto it = state.facets.constFind(QStringLiteral("cover"));
    if (it == state.facets.cend() || it->isEmpty()) {
        return Repositories::CoverFilter::All;
    }
    const QString value = it->first();
    if (value == QStringLiteral("with")) {
        return Repositories::CoverFilter::WithCover;
    }
    if (value == QStringLiteral("without")) {
        return Repositories::CoverFilter::WithoutCover;
    }
    return Repositories::CoverFilter::All;
}

}  // namespace

ArchivedCopySpec::ArchivedCopySpec(Repositories::CatalogRepository& catalog,
                                   ArchiveWorkflow& workflow)
    : m_catalog(catalog)
    , m_workflow(workflow)
{
}

QString ArchivedCopySpec::id() const
{
    return QStringLiteral("archived-copies");
}

QList<ColumnSpec> ArchivedCopySpec::columns() const
{
    return {
        ColumnSpec{QStringLiteral("localId"), QStringLiteral("archive.col.localId"), 110,
                   QString::fromLatin1(Repositories::CopySort::kLocalId)},
        ColumnSpec{QStringLiteral("source"), QStringLiteral("archive.col.source"), 100,
                   QString::fromLatin1(Repositories::CopySort::kSource)},
        ColumnSpec{QStringLiteral("title"), QStringLiteral("archive.col.title"), 280,
                   QString::fromLatin1(Repositories::CopySort::kTitle)},
        ColumnSpec{QStringLiteral("loans"), QStringLiteral("archive.col.loans"), 80,
                   QString::fromLatin1(Repositories::CopySort::kLoans)},
        ColumnSpec{QStringLiteral("archivedAt"), QStringLiteral("archive.col.archivedAt"), 160,
                   QString::fromLatin1(Repositories::CopySort::kArchivedAt)},
    };
}

QList<FacetSpec> ArchivedCopySpec::facets() const
{
    // ArchivePage uses Any for copy language/category value lists (live titles
    // can still own archived copies).
    FacetSpec language;
    language.key = QStringLiteral("language");
    language.titleKey = QStringLiteral("catalog.allLanguages");
    language.values = [this]() {
        const auto languages = m_catalog.listBookLanguages(Repositories::ArchiveScope::Any);
        if (!languages) {
            return QStringList{};
        }
        QStringList codes;
        codes.reserve(static_cast<int>(languages->size()));
        for (const Repositories::LanguageRecord& languageRecord : languages.value()) {
            codes.append(qs(languageRecord.code));
        }
        return codes;
    };

    FacetSpec category;
    category.key = QStringLiteral("category");
    category.titleKey = QStringLiteral("catalog.allCategories");
    category.values = [this]() {
        const auto categories = m_catalog.listCategories();
        if (!categories) {
            return QStringList{};
        }
        QStringList codes;
        codes.reserve(static_cast<int>(categories->size()));
        for (const Repositories::CategoryRecord& categoryRecord : categories.value()) {
            codes.append(qs(categoryRecord.code));
        }
        return codes;
    };

    FacetSpec cover;
    cover.key = QStringLiteral("cover");
    cover.titleKey = QStringLiteral("catalog.allCovers");
    cover.values = []() {
        return QStringList{QStringLiteral("with"), QStringLiteral("without")};
    };

    return {language, category, cover};
}

QList<PageAction> ArchivedCopySpec::actions() const
{
    QList<PageAction> out;

    PageAction loans;
    loans.id = QStringLiteral("loans");
    loans.labelKey = QStringLiteral("archive.loans");
    loans.needs = PageAction::Needs::One;
    loans.enabled = [this](const ActionContext& ctx) {
        if (ctx.ids.size() != 1) {
            return false;
        }
        const auto copy = findCopy(ctx.ids.first());
        return copy && copy->loanCount > 0;
    };
    loans.run = [this](const ActionContext& ctx) {
        const qint64 id = ctx.ids.isEmpty() ? 0 : ctx.ids.first();
        QString name;
        if (const auto copy = findCopy(id)) {
            name = dashIfEmpty(copy->localId) + QStringLiteral(" / ") + qs(copy->bookTitle);
        }
        return m_workflow.showLoans(ctx.parent, ArchiveType::Copies, id, name);
    };
    out.append(loans);

    PageAction restore;
    restore.id = QStringLiteral("restore");
    restore.labelKey = QStringLiteral("archive.restore");
    restore.needs = PageAction::Needs::Some;
    restore.run = [this](const ActionContext& ctx) {
        QList<QString> labels;
        labels.reserve(ctx.ids.size());
        for (const qint64 id : ctx.ids) {
            if (const auto copy = findCopy(id)) {
                labels.append(dashIfEmpty(copy->localId) + QStringLiteral(" — ")
                              + qs(copy->bookTitle));
            } else {
                labels.append(QString::number(id));
            }
        }
        return m_workflow.restore(ctx.parent, ArchiveType::Copies, ctx.ids, labels);
    };
    out.append(restore);

    PageAction reuse;
    reuse.id = QStringLiteral("reuse");
    reuse.labelKey = QStringLiteral("archive.reuse");
    reuse.needs = PageAction::Needs::One;
    reuse.enabled = [this](const ActionContext& ctx) {
        if (ctx.ids.size() != 1) {
            return false;
        }
        const auto copy = findCopy(ctx.ids.first());
        return copy && !copy->localId.empty();
    };
    reuse.run = [this](const ActionContext& ctx) {
        const qint64 id = ctx.ids.isEmpty() ? 0 : ctx.ids.first();
        return m_workflow.reuseNumber(ctx.parent, id);
    };
    out.append(reuse);

    PageAction purge;
    purge.id = QStringLiteral("purge");
    purge.labelKey = QStringLiteral("archive.purge");
    purge.needs = PageAction::Needs::Some;
    purge.enabled = [this](const ActionContext& ctx) {
        if (ctx.ids.isEmpty()) {
            return false;
        }
        if (ctx.ids.size() > 1) {
            return true;
        }
        const auto copy = findCopy(ctx.ids.first());
        return copy && copy->loanCount == 0;
    };
    purge.run = [this](const ActionContext& ctx) {
        QList<QString> labels;
        labels.reserve(ctx.ids.size());
        for (const qint64 id : ctx.ids) {
            if (const auto copy = findCopy(id)) {
                labels.append(dashIfEmpty(copy->localId) + QStringLiteral(" — ")
                              + qs(copy->bookTitle));
            } else {
                labels.append(QString::number(id));
            }
        }
        return m_workflow.purge(ctx.parent, ArchiveType::Copies, ctx.ids, labels);
    };
    out.append(purge);

    PageAction tickAll;
    tickAll.id = QStringLiteral("tickAll");
    tickAll.labelKey = QStringLiteral("tickAll");
    tickAll.needs = PageAction::Needs::Nothing;
    out.append(tickAll);

    PageAction refresh;
    refresh.id = QStringLiteral("refresh");
    refresh.labelKey = QStringLiteral("metrics.refresh");
    refresh.needs = PageAction::Needs::Nothing;
    out.append(refresh);

    return out;
}

EntitySet ArchivedCopySpec::dependsOn() const
{
    return Entity::Copies;
}

Core::Status ArchivedCopySpec::load(const QueryState& state)
{
    const Repositories::CopyQuery query = toQuery(state);
    const auto rows = m_catalog.listCopyRows(query);
    if (!rows) {
        return Core::asStatus(rows.error());
    }
    const auto total = m_catalog.countCopyRows(query);
    if (!total) {
        return Core::asStatus(total.error());
    }

    m_records = rows.value();
    m_total = total.value();
    rebuildDisplayRows();
    return Core::Status::ok();
}

void ArchivedCopySpec::retranslate()
{
    rebuildDisplayRows();
}

int ArchivedCopySpec::rowCount() const
{
    return m_rows.size();
}

int ArchivedCopySpec::totalCount() const
{
    return m_total;
}

qint64 ArchivedCopySpec::idAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return 0;
    }
    return m_rows.at(row).id;
}

QVariant ArchivedCopySpec::cell(int row, int column, int role) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    const DisplayRow& display = m_rows.at(row);
    if (role == Qt::UserRole && column == 0) {
        return display.id;
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    if (column < 0 || column >= display.cells.size()) {
        return {};
    }
    return display.cells.at(column);
}

Core::Result<int> ArchivedCopySpec::rankOf(qint64 id, const QueryState& state) const
{
    // No rankOfCopy on CatalogRepository (API frozen). Scan the ordered id
    // list with the same filters/sort; limit=0 means unrestricted → use count.
    Repositories::CopyQuery query = toQuery(state);
    const auto total = m_catalog.countCopyRows(query);
    if (!total) {
        return Core::Result<int>::fail(total.error().kind, total.error().key, total.error().detail);
    }
    query.limit = total.value();
    query.offset = 0;
    const auto rows = m_catalog.listCopyRows(query);
    if (!rows) {
        return Core::Result<int>::fail(rows.error().kind, rows.error().key, rows.error().detail);
    }
    for (int index = 0; index < static_cast<int>(rows->size()); ++index) {
        if (rows->at(static_cast<size_t>(index)).id == id) {
            return Core::Result<int>::ok(index);
        }
    }
    return Core::Result<int>::fail(Core::ErrorKind::NotFound, "error.copy.notFound");
}

PreviewContent ArchivedCopySpec::preview(qint64 id) const
{
    PreviewContent content;
    const auto found = findCopy(id);
    if (!found) {
        return content;
    }
    const Repositories::BookCopyRecord& copy = *found;

    if (!copy.coverImagePath.empty()) {
        content.imagePath = qs(m_catalog.resolveCoverPath(copy.coverImagePath));
    }

    content.fields = {
        PreviewField{QStringLiteral("archive.col.localId"), dashIfEmpty(copy.localId), false},
        PreviewField{QStringLiteral("archive.col.source"), sourceLabel(copy.source), false},
        PreviewField{QStringLiteral("archive.col.title"), qs(copy.bookTitle), false},
        PreviewField{QStringLiteral("archive.col.archivedAt"), qs(copy.archivedAt), false},
        PreviewField{QStringLiteral("archive.col.notes"), dashIfEmpty(copy.notes), false},
    };
    return content;
}

Repositories::CopyQuery ArchivedCopySpec::toQuery(const QueryState& state) const
{
    Repositories::CopyQuery query;
    query.search = ss(state.search);
    query.languages = facetValues(state, QStringLiteral("language"));
    query.categoryCodes = facetValues(state, QStringLiteral("category"));
    query.coverFilter = coverFilterFromState(state);
    query.archive = Repositories::ArchiveScope::Archived;
    query.sortColumn = ss(state.sortKey);
    query.sortAscending = state.ascending;

    if (state.pageSize <= 0) {
        query.limit = std::numeric_limits<int>::max();
        query.offset = 0;
    } else {
        const int page = state.page < 1 ? 1 : state.page;
        query.limit = state.pageSize;
        query.offset = (page - 1) * state.pageSize;
    }
    return query;
}

QString ArchivedCopySpec::sourceLabel(const std::string& source)
{
    return T(source == "arabic" ? "book.copy.source.arabic" : "book.copy.source.foreign");
}

QStringList ArchivedCopySpec::buildCells(const Repositories::BookCopyRecord& copy)
{
    return {
        dashIfEmpty(copy.localId),
        sourceLabel(copy.source),
        qs(copy.bookTitle),
        QString::number(copy.loanCount),
        qs(copy.archivedAt),
    };
}

std::optional<Repositories::BookCopyRecord> ArchivedCopySpec::findCopy(qint64 id) const
{
    for (const Repositories::BookCopyRecord& copy : m_records) {
        if (copy.id == id) {
            return copy;
        }
    }
    Repositories::CopyQuery query;
    query.archive = Repositories::ArchiveScope::Archived;
    const auto total = m_catalog.countCopyRows(query);
    if (!total || total.value() <= 0) {
        return std::nullopt;
    }
    query.limit = total.value();
    query.offset = 0;
    const auto rows = m_catalog.listCopyRows(query);
    if (!rows) {
        return std::nullopt;
    }
    for (const Repositories::BookCopyRecord& copy : rows.value()) {
        if (copy.id == id) {
            return copy;
        }
    }
    return std::nullopt;
}

void ArchivedCopySpec::rebuildDisplayRows()
{
    m_rows.clear();
    m_rows.reserve(static_cast<int>(m_records.size()));
    for (const Repositories::BookCopyRecord& copy : m_records) {
        DisplayRow row;
        row.id = copy.id;
        row.cells = buildCells(copy);
        m_rows.append(std::move(row));
    }
}

}  // namespace VLMS
