#include "ui/presentation/ArchivedLoanSpec.h"

#include "QtBridge.h"

#include <VLMS/Repositories/ArchiveTypes.h>

#include <Qt>
#include <limits>

using VLMS::qs;
using VLMS::ss;

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

}  // namespace

ArchivedLoanSpec::ArchivedLoanSpec(Repositories::CirculationRepository& circulation,
                                   Repositories::CatalogRepository& catalog,
                                   Repositories::MemberRepository& members,
                                   ArchiveWorkflow& workflow)
    : m_circulation(circulation)
    , m_catalog(catalog)
    , m_members(members)
    , m_workflow(workflow)
{
}

QString ArchivedLoanSpec::id() const
{
    return QStringLiteral("archived-loans");
}

QList<ColumnSpec> ArchivedLoanSpec::columns() const
{
    return {
        ColumnSpec{QStringLiteral("member"), QStringLiteral("archive.col.member"), 200,
                   QString::fromLatin1(Repositories::LoanSort::kMember)},
        ColumnSpec{QStringLiteral("copy"), QStringLiteral("archive.col.copy"), 300,
                   QString::fromLatin1(Repositories::LoanSort::kTitle)},
        ColumnSpec{QStringLiteral("returnedAt"), QStringLiteral("archive.col.returnedAt"), 110,
                   QString::fromLatin1(Repositories::LoanSort::kReturned)},
        ColumnSpec{QStringLiteral("archivedAt"), QStringLiteral("archive.col.archivedAt"), 160,
                   QString::fromLatin1(Repositories::LoanSort::kArchivedAt)},
    };
}

QList<FacetSpec> ArchivedLoanSpec::facets() const
{
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
        const auto years = m_circulation.listLoanYears(Repositories::ArchiveScope::Archived);
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
        // A loan's borrower may still be live.
        const auto cities = m_members.listCities(Repositories::ArchiveScope::Any);
        return cities ? codesToQStringList(cities.value()) : QStringList{};
    };

    return {status, sex, year, ageGroup, city};
}

QList<PageAction> ArchivedLoanSpec::actions() const
{
    QList<PageAction> out;

    PageAction restore;
    restore.id = QStringLiteral("restore");
    restore.labelKey = QStringLiteral("archive.restore");
    restore.needs = PageAction::Needs::Some;
    restore.run = [this](const ActionContext& ctx) {
        QList<QString> labels;
        labels.reserve(ctx.ids.size());
        for (const qint64 id : ctx.ids) {
            if (const auto loan = m_circulation.getLoan(id)) {
                labels.append(displayLabel(*loan));
            } else {
                labels.append(QString::number(id));
            }
        }
        return m_workflow.restore(ctx.parent, ArchiveType::Loans, ctx.ids, labels);
    };
    out.append(restore);

    PageAction purge;
    purge.id = QStringLiteral("purge");
    purge.labelKey = QStringLiteral("archive.purge");
    purge.needs = PageAction::Needs::Some;
    // archive page: loans may always be purged when selected / ticked.
    purge.enabled = [](const ActionContext& ctx) { return !ctx.ids.isEmpty(); };
    purge.run = [this](const ActionContext& ctx) {
        QList<QString> labels;
        labels.reserve(ctx.ids.size());
        for (const qint64 id : ctx.ids) {
            if (const auto loan = m_circulation.getLoan(id)) {
                labels.append(displayLabel(*loan));
            } else {
                labels.append(QString::number(id));
            }
        }
        return m_workflow.purge(ctx.parent, ArchiveType::Loans, ctx.ids, labels);
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

EntitySet ArchivedLoanSpec::dependsOn() const
{
    return Entity::Loans | Entity::Members | Entity::Copies;
}

Core::Status ArchivedLoanSpec::load(const QueryState& state)
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

void ArchivedLoanSpec::retranslate()
{
    rebuildDisplayRows();
}

int ArchivedLoanSpec::rowCount() const
{
    return m_rows.size();
}

int ArchivedLoanSpec::totalCount() const
{
    return m_total;
}

qint64 ArchivedLoanSpec::idAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return 0;
    }
    return m_rows.at(row).id;
}

QVariant ArchivedLoanSpec::cell(int row, int column, int role) const
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

Core::Result<int> ArchivedLoanSpec::rankOf(qint64 id, const QueryState& state) const
{
    return m_circulation.rankOfLoan(id, toQuery(state));
}

PreviewContent ArchivedLoanSpec::preview(qint64 id) const
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
        PreviewField{QStringLiteral("archive.col.member"), qs(loan.memberName), false},
        PreviewField{QStringLiteral("member.field.number"), qs(loan.membershipNumber), false},
        PreviewField{QStringLiteral("archive.col.copy"),
                     qs(loan.copyCode) + QStringLiteral(" / ") + qs(loan.bookTitle), false},
        PreviewField{QStringLiteral("archive.col.returnedAt"), qs(loan.returnedAt), false},
        PreviewField{QStringLiteral("archive.col.archivedAt"), qs(loan.archivedAt), false},
        PreviewField{QStringLiteral("loan.field.notes"), dashIfEmpty(loan.notes), true},
    };
    return content;
}

Repositories::LoanQuery ArchivedLoanSpec::toQuery(const QueryState& state) const
{
    Repositories::LoanQuery query;
    query.search = ss(state.search);
    query.member.statuses = facetValues(state, QStringLiteral("status"));
    query.member.sexes = facetValues(state, QStringLiteral("sex"));
    query.member.ageGroups = facetValues(state, QStringLiteral("ageGroup"));
    query.member.cities = facetValues(state, QStringLiteral("city"));
    query.loanYears = facetValues(state, QStringLiteral("year"));
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

QStringList ArchivedLoanSpec::buildCells(const Repositories::LoanRecord& loan)
{
    return {
        qs(loan.memberName),
        qs(loan.copyCode) + QStringLiteral(" / ") + qs(loan.bookTitle),
        qs(loan.returnedAt),
        qs(loan.archivedAt),
    };
}

QString ArchivedLoanSpec::displayLabel(const Repositories::LoanRecord& loan)
{
    return qs(loan.memberName) + QStringLiteral(" — ") + qs(loan.bookTitle);
}

void ArchivedLoanSpec::rebuildDisplayRows()
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
