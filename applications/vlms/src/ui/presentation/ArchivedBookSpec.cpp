#include "ui/presentation/ArchivedBookSpec.h"

#include "QtBridge.h"

#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/LoanTypes.h>

#include <Qt>
#include <limits>

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

ArchivedBookSpec::ArchivedBookSpec(Repositories::CatalogRepository& catalog,
                                   Repositories::CirculationRepository& circulation,
                                   ArchiveWorkflow& workflow)
    : m_catalog(catalog)
    , m_circulation(circulation)
    , m_workflow(workflow)
{
}

QString ArchivedBookSpec::id() const
{
    return QStringLiteral("archived-books");
}

QList<ColumnSpec> ArchivedBookSpec::columns() const
{
    return {
        ColumnSpec{QStringLiteral("title"), QStringLiteral("archive.col.title"), 320,
                   QString::fromLatin1(Repositories::BookSort::kTitle)},
        ColumnSpec{QStringLiteral("author"), QStringLiteral("archive.col.author"), 200,
                   QString::fromLatin1(Repositories::BookSort::kAuthor)},
        ColumnSpec{QStringLiteral("copies"), QStringLiteral("archive.col.copies"), 70,
                   QString::fromLatin1(Repositories::BookSort::kCopies)},
        ColumnSpec{QStringLiteral("archivedAt"), QStringLiteral("archive.col.archivedAt"), 160,
                   QString::fromLatin1(Repositories::BookSort::kArchivedAt)},
    };
}

QList<FacetSpec> ArchivedBookSpec::facets() const
{
    FacetSpec language;
    language.key = QStringLiteral("language");
    language.titleKey = QStringLiteral("catalog.allLanguages");
    language.values = [this]() {
        const auto languages = m_catalog.listBookLanguages(Repositories::ArchiveScope::Archived);
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

QList<PageAction> ArchivedBookSpec::actions() const
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
        Repositories::LoanQuery query;
        query.bookId = ctx.ids.first();
        query.archive = Repositories::ArchiveScope::Any;
        const auto count = m_circulation.countLoans(query);
        return count && count.value() > 0;
    };
    loans.run = [this](const ActionContext& ctx) {
        const qint64 id = ctx.ids.isEmpty() ? 0 : ctx.ids.first();
        QString name;
        if (const auto book = m_catalog.getBook(id)) {
            name = qs(book->title);
        }
        return m_workflow.showLoans(ctx.parent, ArchiveType::Books, id, name);
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
            if (const auto book = m_catalog.getBook(id)) {
                labels.append(qs(book->title));
            } else {
                labels.append(QString::number(id));
            }
        }
        return m_workflow.restore(ctx.parent, ArchiveType::Books, ctx.ids, labels);
    };
    out.append(restore);

    PageAction purge;
    purge.id = QStringLiteral("purge");
    purge.labelKey = QStringLiteral("archive.purge");
    purge.needs = PageAction::Needs::Some;
    purge.enabled = [this](const ActionContext& ctx) {
        if (ctx.ids.isEmpty()) {
            return false;
        }
        if (ctx.fromChecks || ctx.ids.size() > 1) {
            return true;
        }
        // Highlight only: getBook() counts live copies; use the archived list
        // row so the Copies column and the Purge gate stay in agreement.
        for (const Repositories::BookRecord& book : m_records) {
            if (book.id == ctx.ids.first()) {
                return book.totalCopies == 0;
            }
        }
        return false;
    };
    purge.run = [this](const ActionContext& ctx) {
        QList<QString> labels;
        labels.reserve(ctx.ids.size());
        for (const qint64 id : ctx.ids) {
            if (const auto book = m_catalog.getBook(id)) {
                labels.append(qs(book->title));
            } else {
                labels.append(QString::number(id));
            }
        }
        return m_workflow.purge(ctx.parent, ArchiveType::Books, ctx.ids, labels);
    };
    out.append(purge);

    PageAction tickAll;
    tickAll.id = QStringLiteral("tickAll");
    tickAll.labelKey = QStringLiteral("list.tickAll");
    tickAll.needs = PageAction::Needs::Nothing;
    out.append(tickAll);

    PageAction refresh;
    refresh.id = QStringLiteral("refresh");
    refresh.labelKey = QStringLiteral("metrics.refresh");
    refresh.needs = PageAction::Needs::Nothing;
    out.append(refresh);

    return out;
}

EntitySet ArchivedBookSpec::dependsOn() const
{
    return Entity::Books | Entity::Copies;
}

Core::Status ArchivedBookSpec::load(const QueryState& state)
{
    const Repositories::BookQuery query = toQuery(state);
    const auto rows = m_catalog.listBooks(query);
    if (!rows) {
        return Core::asStatus(rows.error());
    }
    const auto total = m_catalog.countBooks(query);
    if (!total) {
        return Core::asStatus(total.error());
    }

    m_records = rows.value();
    m_total = total.value();
    rebuildDisplayRows();
    return Core::Status::ok();
}

void ArchivedBookSpec::retranslate()
{
    rebuildDisplayRows();
}

int ArchivedBookSpec::rowCount() const
{
    return m_rows.size();
}

int ArchivedBookSpec::totalCount() const
{
    return m_total;
}

qint64 ArchivedBookSpec::idAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return 0;
    }
    return m_rows.at(row).id;
}

QVariant ArchivedBookSpec::cell(int row, int column, int role) const
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

Core::Result<int> ArchivedBookSpec::rankOf(qint64 id, const QueryState& state) const
{
    return m_catalog.rankOfBook(id, toQuery(state));
}

PreviewContent ArchivedBookSpec::preview(qint64 id) const
{
    PreviewContent content;
    const auto bookResult = m_catalog.getBook(id);
    if (!bookResult) {
        return content;
    }
    const Repositories::BookRecord& book = bookResult.value();

    if (!book.coverImagePath.empty()) {
        content.imagePath = qs(m_catalog.resolveCoverPath(book.coverImagePath));
    }

    content.fields = {
        PreviewField{QStringLiteral("archive.col.title"), qs(book.title), false},
        PreviewField{QStringLiteral("archive.col.author"), dashIfEmpty(book.authorName), false},
        PreviewField{QStringLiteral("archive.col.copies"), QString::number(book.totalCopies), false},
        PreviewField{QStringLiteral("archive.col.archivedAt"), qs(book.archivedAt), false},
    };
    return content;
}

Repositories::BookQuery ArchivedBookSpec::toQuery(const QueryState& state) const
{
    Repositories::BookQuery query;
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

QStringList ArchivedBookSpec::buildCells(const Repositories::BookRecord& book)
{
    return {
        qs(book.title),
        dashIfEmpty(book.authorName),
        QString::number(book.totalCopies),
        qs(book.archivedAt),
    };
}

void ArchivedBookSpec::rebuildDisplayRows()
{
    m_rows.clear();
    m_rows.reserve(static_cast<int>(m_records.size()));
    for (const Repositories::BookRecord& book : m_records) {
        DisplayRow row;
        row.id = book.id;
        row.cells = buildCells(book);
        m_rows.append(std::move(row));
    }
}

}  // namespace VLMS
