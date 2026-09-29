#include "ui/presentation/BookListSpec.h"

#include "QtBridge.h"
#include "ui/presentation/LocalNumberCell.h"

#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/ArchiveTypes.h>

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

constexpr int kLocalNumberColumn = 3;

}  // namespace

BookListSpec::BookListSpec(Repositories::CatalogRepository& catalog, BookWorkflow& workflow)
    : m_catalog(catalog)
    , m_workflow(workflow)
{
}

QString BookListSpec::id() const
{
    return QStringLiteral("books");
}

QList<ColumnSpec> BookListSpec::columns() const
{
    return {
        ColumnSpec{QStringLiteral("title"), QStringLiteral("catalog.col.title"), 320,
                   QString::fromLatin1(Repositories::BookSort::kTitle)},
        ColumnSpec{QStringLiteral("author"), QStringLiteral("catalog.col.author"), 200,
                   QString::fromLatin1(Repositories::BookSort::kAuthor)},
        ColumnSpec{QStringLiteral("category"), QStringLiteral("catalog.col.category"), 130,
                   QString::fromLatin1(Repositories::BookSort::kCategory)},
        ColumnSpec{QStringLiteral("localNumber"), QStringLiteral("catalog.col.localNumber"), 110,
                   QString::fromLatin1(Repositories::BookSort::kLocalNumber)},
        ColumnSpec{QStringLiteral("copies"), QStringLiteral("catalog.col.copies"), 70,
                   QString::fromLatin1(Repositories::BookSort::kCopies)},
        ColumnSpec{QStringLiteral("available"), QStringLiteral("catalog.col.available"), 70,
                   QString::fromLatin1(Repositories::BookSort::kAvailable)},
    };
}

QList<FacetSpec> BookListSpec::facets() const
{
    FacetSpec language;
    language.key = QStringLiteral("language");
    language.titleKey = QStringLiteral("catalog.allLanguages");
    language.values = [this]() {
        const auto languages = m_catalog.listBookLanguages(Repositories::ArchiveScope::Live);
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

QList<PageAction> BookListSpec::actions() const
{
    QList<PageAction> out;

    PageAction loans;
    loans.id = QStringLiteral("loans");
    loans.labelKey = QStringLiteral("catalog.loans");
    loans.needs = PageAction::Needs::One;
    loans.run = [this](const ActionContext& ctx) {
        const qint64 id = ctx.ids.isEmpty() ? 0 : ctx.ids.first();
        return m_workflow.showLoans(ctx.parent, id);
    };
    out.append(loans);

    PageAction add;
    add.id = QStringLiteral("add");
    add.labelKey = QStringLiteral("catalog.addBook");
    add.needs = PageAction::Needs::Nothing;
    add.run = [this](const ActionContext& ctx) { return m_workflow.add(ctx.parent); };
    out.append(add);

    PageAction edit;
    edit.id = QStringLiteral("edit");
    edit.labelKey = QStringLiteral("catalog.edit");
    edit.needs = PageAction::Needs::One;
    edit.run = [this](const ActionContext& ctx) {
        const qint64 id = ctx.ids.isEmpty() ? 0 : ctx.ids.first();
        return m_workflow.edit(ctx.parent, id);
    };
    out.append(edit);

    PageAction archive;
    archive.id = QStringLiteral("archive");
    archive.labelKey = QStringLiteral("catalog.delete");
    archive.needs = PageAction::Needs::Some;
    archive.run = [this](const ActionContext& ctx) {
        QList<QString> labels;
        labels.reserve(ctx.ids.size());
        for (const qint64 id : ctx.ids) {
            if (const auto book = m_catalog.getBook(id)) {
                labels.append(qs(book->title));
            } else {
                labels.append(QString::number(id));
            }
        }
        return m_workflow.archive(ctx.parent, ctx.ids, labels);
    };
    out.append(archive);

    PageAction tickAll;
    tickAll.id = QStringLiteral("tickAll");
    tickAll.labelKey = QStringLiteral("tickAll");
    tickAll.needs = PageAction::Needs::Nothing;
    // ListPage::runAction handles ticking; no workflow call.
    out.append(tickAll);

    PageAction refresh;
    refresh.id = QStringLiteral("refresh");
    refresh.labelKey = QStringLiteral("metrics.refresh");
    refresh.needs = PageAction::Needs::Nothing;
    // Reload is owned by ListPage/PagedQueryModel; no BookWorkflow method.
    out.append(refresh);

    return out;
}

EntitySet BookListSpec::dependsOn() const
{
    return Entity::Books | Entity::Copies;
}

Core::Status BookListSpec::load(const QueryState& state)
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

void BookListSpec::retranslate()
{
    rebuildDisplayRows();
}

int BookListSpec::rowCount() const
{
    return m_rows.size();
}

int BookListSpec::totalCount() const
{
    return m_total;
}

qint64 BookListSpec::idAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return 0;
    }
    return m_rows.at(row).id;
}

QVariant BookListSpec::cell(int row, int column, int role) const
{
    if (row < 0 || row >= m_rows.size()) {
        return {};
    }
    const DisplayRow& display = m_rows.at(row);
    if (role == Qt::UserRole && column == 0) {
        return display.id;
    }
    if (column == kLocalNumberColumn) {
        if (role == LocalNumberCell::kNumbersRole) {
            return display.localNumbers;
        }
        if (role == LocalNumberCell::kSelectedRole) {
            return display.selectedLocal;
        }
        if (role == LocalNumberCell::kOnLoanRole) {
            return display.onLoanLocals;
        }
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
    if (column < 0 || column >= display.cells.size()) {
        return {};
    }
    return display.cells.at(column);
}

Core::Result<int> BookListSpec::rankOf(qint64 id, const QueryState& state) const
{
    return m_catalog.rankOfBook(id, toQuery(state));
}

PreviewContent BookListSpec::preview(qint64 id) const
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
        PreviewField{QStringLiteral("book.field.description"), dashIfEmpty(book.description), true},
        PreviewField{QStringLiteral("book.field.title"), qs(book.title), false},
        PreviewField{QStringLiteral("book.field.author"), qs(book.authorName), false},
        PreviewField{QStringLiteral("book.field.publisher"), dashIfEmpty(book.publisherName), false},
        PreviewField{QStringLiteral("book.field.category"), dashIfEmpty(book.categoryLabel), false},
        PreviewField{QStringLiteral("book.field.isbn"), dashIfEmpty(book.isbn), false},
        PreviewField{QStringLiteral("book.field.publicationDate"), dashIfEmpty(book.publicationDate),
                     false},
        PreviewField{QStringLiteral("book.field.place"), dashIfEmpty(book.placeOfPublication), false},
        PreviewField{QStringLiteral("book.field.pages"), dashIfEmpty(book.pages), false},
        PreviewField{QStringLiteral("book.field.dimensions"), dashIfEmpty(book.dimensions), false},
        PreviewField{QStringLiteral("book.field.language"),
                     qs(Core::Strings::bookLanguageLabel(book.language)), false},
        PreviewField{QStringLiteral("catalog.col.copies"), QString::number(book.totalCopies), false},
        PreviewField{QStringLiteral("catalog.col.available"), QString::number(book.availableCopies),
                     false},
    };
    return content;
}

Repositories::BookQuery BookListSpec::toQuery(const QueryState& state) const
{
    Repositories::BookQuery query;
    query.search = ss(state.search);
    query.languages = facetValues(state, QStringLiteral("language"));
    query.categoryCodes = facetValues(state, QStringLiteral("category"));
    query.coverFilter = coverFilterFromState(state);
    query.archive = Repositories::ArchiveScope::Live;
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

QStringList BookListSpec::buildCells(const Repositories::BookRecord& book)
{
    QStringList localNumbers;
    localNumbers.reserve(static_cast<int>(book.localIds.size()));
    for (const std::string& localId : book.localIds) {
        localNumbers.append(qs(localId));
    }
    const QString selected = book.matchedLocalId.empty() ? QString{} : qs(book.matchedLocalId);

    return {
        qs(book.title),
        qs(book.authorName),
        qs(book.categoryLabel),
        LocalNumberCell::cellText(localNumbers, selected),
        QString::number(book.totalCopies),
        QString::number(book.availableCopies),
    };
}

void BookListSpec::rebuildDisplayRows()
{
    m_rows.clear();
    m_rows.reserve(static_cast<int>(m_records.size()));
    for (const Repositories::BookRecord& book : m_records) {
        DisplayRow row;
        row.id = book.id;
        row.cells = buildCells(book);
        row.localNumbers.reserve(static_cast<int>(book.localIds.size()));
        for (const std::string& localId : book.localIds) {
            row.localNumbers.append(qs(localId));
        }
        if (!book.matchedLocalId.empty()) {
            row.selectedLocal = qs(book.matchedLocalId);
        }
        row.onLoanLocals.reserve(static_cast<int>(book.localIdsOnLoan.size()));
        for (const std::string& localId : book.localIdsOnLoan) {
            row.onLoanLocals.append(qs(localId));
        }
        m_rows.append(std::move(row));
    }
}

}  // namespace VLMS
