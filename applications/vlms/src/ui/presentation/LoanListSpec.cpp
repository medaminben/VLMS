#include "ui/presentation/LoanListSpec.h"

#include "QtBridge.h"

#include <VLMS/Core/Strings.h>
#include <VLMS/Repositories/ArchiveTypes.h>

#include <Qt>
#include <limits>

using VLMS::qs;
using VLMS::ss;
using VLMS::T;

namespace VLMS {
namespace {

QStringList codesToQStringList(const std::vector<std::string>& codes)
{
    QStringList out;
    out.reserve(static_cast<int>(codes.size()));
    for (const std::string& code : codes) {
        out.append(qs(code));
    }
    return out;
}

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

bool isAllLoanFilter(const QString& code)
{
    return code.isEmpty() || code == QLatin1String(Repositories::LoanFilter::kAll);
}

std::vector<std::string> loanFilterValues(const QueryState& state)
{
    std::vector<std::string> filters;
    const auto it = state.facets.constFind(QStringLiteral("loanFilter"));
    if (it == state.facets.cend()) {
        return filters;
    }
    filters.reserve(static_cast<size_t>(it->size()));
    for (const QString& value : *it) {
        if (!isAllLoanFilter(value)) {
            filters.push_back(ss(value));
        }
    }
    return filters;
}

}  // namespace

LoanListSpec::LoanListSpec(Repositories::CirculationRepository& circulation,
                           Repositories::CatalogRepository& catalog,
                           Repositories::MemberRepository& members,
                           LoanWorkflow& workflow)
    : m_circulation(circulation)
    , m_catalog(catalog)
    , m_members(members)
    , m_workflow(workflow)
{
}

QString LoanListSpec::id() const
{
    return QStringLiteral("loans");
}

QList<ColumnSpec> LoanListSpec::columns() const
{
    return {
        ColumnSpec{QStringLiteral("member"), QStringLiteral("circulation.col.member"), 200,
                   QString::fromLatin1(Repositories::LoanSort::kMember)},
        ColumnSpec{QStringLiteral("number"), QStringLiteral("circulation.col.number"), 130,
                   QString::fromLatin1(Repositories::LoanSort::kNumber)},
        ColumnSpec{QStringLiteral("title"), QStringLiteral("circulation.col.title"), 300,
                   QString::fromLatin1(Repositories::LoanSort::kTitle)},
        ColumnSpec{QStringLiteral("borrowed"), QStringLiteral("circulation.col.borrowed"), 110,
                   QString::fromLatin1(Repositories::LoanSort::kBorrowed)},
        ColumnSpec{QStringLiteral("due"), QStringLiteral("circulation.col.due"), 110,
                   QString::fromLatin1(Repositories::LoanSort::kDue)},
        ColumnSpec{QStringLiteral("status"), QStringLiteral("circulation.col.status"), 110,
                   QString::fromLatin1(Repositories::LoanSort::kStatus)},
    };
}

QList<FacetSpec> LoanListSpec::facets() const
{
    FacetSpec loanFilter;
    loanFilter.key = QStringLiteral("loanFilter");
    loanFilter.titleKey = QStringLiteral("circulation.filter.all");
    loanFilter.values = []() {
        return codesToQStringList(Repositories::CirculationRepository::filterCodes());
    };

    FacetSpec status;
    status.key = QStringLiteral("status");
    status.titleKey = QStringLiteral("members.allStatuses");
    status.values = []() {
        return codesToQStringList(Repositories::MemberRepository::statusCodes());
    };

    FacetSpec sex;
    sex.key = QStringLiteral("sex");
    sex.titleKey = QStringLiteral("members.allSexes");
    sex.values = []() { return codesToQStringList(Repositories::MemberRepository::sexCodes()); };

    FacetSpec year;
    year.key = QStringLiteral("year");
    year.titleKey = QStringLiteral("members.allYears");
    year.values = [this]() {
        const auto years = m_circulation.listLoanYears(Repositories::ArchiveScope::Live);
        return years ? codesToQStringList(years.value()) : QStringList{};
    };

    FacetSpec ageGroup;
    ageGroup.key = QStringLiteral("ageGroup");
    ageGroup.titleKey = QStringLiteral("members.allAgeGroups");
    ageGroup.values = []() {
        return codesToQStringList(Repositories::MemberRepository::ageGroupCodes());
    };

    FacetSpec city;
    city.key = QStringLiteral("city");
    city.titleKey = QStringLiteral("members.allCities");
    city.values = [this]() {
        // Circulation lists loans of members who may since be archived.
        const auto cities = m_members.listCities(Repositories::ArchiveScope::Any);
        return cities ? codesToQStringList(cities.value()) : QStringList{};
    };

    return {loanFilter, status, sex, year, ageGroup, city};
}

QList<PageAction> LoanListSpec::actions() const
{
    QList<PageAction> out;

    PageAction ret;
    ret.id = QStringLiteral("return");
    ret.labelKey = QStringLiteral("circulation.return");
    ret.needs = PageAction::Needs::One;
    ret.enabled = [this](const ActionContext& ctx) {
        if (ctx.ids.size() != 1) {
            return false;
        }
        const auto loan = m_circulation.getLoan(ctx.ids.first());
        return loan && loan->returnedAt.empty();
    };
    ret.run = [this](const ActionContext& ctx) {
        const qint64 id = ctx.ids.isEmpty() ? 0 : ctx.ids.first();
        return m_workflow.returnLoan(ctx.parent, id);
    };
    out.append(ret);

    PageAction checkout;
    checkout.id = QStringLiteral("checkout");
    checkout.labelKey = QStringLiteral("circulation.checkout");
    checkout.needs = PageAction::Needs::Nothing;
    checkout.run = [this](const ActionContext& ctx) { return m_workflow.checkout(ctx.parent); };
    out.append(checkout);

    PageAction extend;
    extend.id = QStringLiteral("extend");
    extend.labelKey = QStringLiteral("circulation.extend");
    extend.needs = PageAction::Needs::One;
    extend.enabled = [this](const ActionContext& ctx) {
        if (ctx.ids.size() != 1) {
            return false;
        }
        const auto loan = m_circulation.getLoan(ctx.ids.first());
        return loan && loan->returnedAt.empty();
    };
    extend.run = [this](const ActionContext& ctx) {
        const qint64 id = ctx.ids.isEmpty() ? 0 : ctx.ids.first();
        return m_workflow.extend(ctx.parent, id);
    };
    out.append(extend);

    PageAction archive;
    archive.id = QStringLiteral("archive");
    archive.labelKey = QStringLiteral("circulation.delete");
    archive.needs = PageAction::Needs::Some;
    // Today: enabled when the highlighted loan is returned, or when any rows
    // are ticked. ListPage folds ticks into ctx.ids; a multi-id context is the
    // tick case, a single returned loan is the highlight case.
    archive.enabled = [this](const ActionContext& ctx) {
        if (ctx.ids.isEmpty()) {
            return false;
        }
        if (ctx.ids.size() > 1) {
            return true;
        }
        const auto loan = m_circulation.getLoan(ctx.ids.first());
        return loan && !loan->returnedAt.empty();
    };
    archive.run = [this](const ActionContext& ctx) {
        QList<QString> labels;
        labels.reserve(ctx.ids.size());
        for (const qint64 id : ctx.ids) {
            if (const auto loan = m_circulation.getLoan(id)) {
                labels.append(displayLabel(*loan));
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
    out.append(tickAll);

    PageAction refresh;
    refresh.id = QStringLiteral("refresh");
    refresh.labelKey = QStringLiteral("metrics.refresh");
    refresh.needs = PageAction::Needs::Nothing;
    out.append(refresh);

    return out;
}

EntitySet LoanListSpec::dependsOn() const
{
    return Entity::Loans | Entity::Members | Entity::Copies | Entity::Books;
}

Core::Status LoanListSpec::load(const QueryState& state)
{
    const Repositories::LoanQuery query = toQuery(state);
    const auto rows = m_circulation.listLoans(query);
    if (!rows) {
        return Core::asStatus(rows.error());
    }
    const auto total = m_circulation.countLoans(query);
    if (!total) {
        return Core::asStatus(total.error());
    }

    m_records = rows.value();
    m_total = total.value();
    rebuildDisplayRows();
    return Core::Status::ok();
}

void LoanListSpec::retranslate()
{
    rebuildDisplayRows();
}

int LoanListSpec::rowCount() const
{
    return m_rows.size();
}

int LoanListSpec::totalCount() const
{
    return m_total;
}

qint64 LoanListSpec::idAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return 0;
    }
    return m_rows.at(row).id;
}

QVariant LoanListSpec::cell(int row, int column, int role) const
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

Core::Result<int> LoanListSpec::rankOf(qint64 id, const QueryState& state) const
{
    return m_circulation.rankOfLoan(id, toQuery(state));
}

PreviewContent LoanListSpec::preview(qint64 id) const
{
    PreviewContent content;
    const auto loanResult = m_circulation.getLoan(id);
    if (!loanResult) {
        return content;
    }
    const Repositories::LoanRecord& loan = loanResult.value();

    if (!loan.coverImagePath.empty()) {
        content.imagePath = qs(m_catalog.resolveCoverPath(loan.coverImagePath));
    }
    if (!loan.memberPhotoPath.empty()) {
        content.secondImagePath = qs(m_members.resolveImagePath(loan.memberPhotoPath));
    }

    content.fields = {
        PreviewField{QStringLiteral("loan.field.notes"), dashIfEmpty(loan.notes), true},
        PreviewField{QStringLiteral("book.field.title"), qs(loan.bookTitle), false},
        PreviewField{QStringLiteral("book.field.author"), dashIfEmpty(loan.authorName), false},
        PreviewField{QStringLiteral("loan.field.member"), qs(loan.memberName), false},
        PreviewField{QStringLiteral("member.field.number"), qs(loan.membershipNumber), false},
        PreviewField{QStringLiteral("loan.field.copy"), qs(loan.copyCode), false},
        PreviewField{QStringLiteral("loan.field.borrowedAt"), qs(loan.borrowedAt), false},
        PreviewField{QStringLiteral("loan.field.dueAt"), qs(loan.dueAt), false},
        PreviewField{QStringLiteral("loan.field.returnedAt"), dashIfEmpty(loan.returnedAt), false},
        PreviewField{QStringLiteral("circulation.col.status"), statusLabel(loan), false},
    };
    return content;
}

Repositories::LoanQuery LoanListSpec::toQuery(const QueryState& state) const
{
    Repositories::LoanQuery query;
    query.search = ss(state.search);
    query.filters = loanFilterValues(state);
    query.member.statuses = facetValues(state, QStringLiteral("status"));
    query.member.sexes = facetValues(state, QStringLiteral("sex"));
    query.member.ageGroups = facetValues(state, QStringLiteral("ageGroup"));
    query.member.cities = facetValues(state, QStringLiteral("city"));
    // Year facet uses the loan-year source (borrowed_at), not inscription year.
    query.loanYears = facetValues(state, QStringLiteral("year"));
    query.archive = state.scope;
    query.sortColumn = ss(state.sortKey);
    query.sortAscending = state.ascending;

    if (state.pinned) {
        query.memberId = state.pinned->memberId;
        query.bookId = state.pinned->bookId;
        query.copyId = state.pinned->copyId;
    }

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

QString LoanListSpec::statusLabel(const Repositories::LoanRecord& loan)
{
    if (!loan.returnedAt.empty()) {
        return T("circulation.status.returned");
    }
    if (loan.isOverdue) {
        return T("circulation.status.overdue");
    }
    return T("circulation.status.open");
}

QStringList LoanListSpec::buildCells(const Repositories::LoanRecord& loan)
{
    return {
        qs(loan.memberName),
        qs(loan.membershipNumber),
        qs(loan.bookTitle),
        qs(loan.borrowedAt),
        qs(loan.dueAt),
        statusLabel(loan),
    };
}

QString LoanListSpec::displayLabel(const Repositories::LoanRecord& loan)
{
    return qs(loan.memberName) + QStringLiteral(" — ") + qs(loan.bookTitle);
}

void LoanListSpec::rebuildDisplayRows()
{
    m_rows.clear();
    m_rows.reserve(static_cast<int>(m_records.size()));
    for (const Repositories::LoanRecord& loan : m_records) {
        DisplayRow row;
        row.id = loan.id;
        row.cells = buildCells(loan);
        m_rows.append(std::move(row));
    }
}

}  // namespace VLMS
