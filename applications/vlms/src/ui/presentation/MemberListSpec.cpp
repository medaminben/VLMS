#include "ui/presentation/MemberListSpec.h"

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

}  // namespace

MemberListSpec::MemberListSpec(Repositories::MemberRepository& members, MemberWorkflow& workflow)
    : m_members(members)
    , m_workflow(workflow)
{
}

QString MemberListSpec::id() const
{
    return QStringLiteral("members");
}

QList<ColumnSpec> MemberListSpec::columns() const
{
    return {
        ColumnSpec{QStringLiteral("number"), QStringLiteral("members.col.number"), 140,
                   QString::fromLatin1(Repositories::MemberSort::kNumber)},
        ColumnSpec{QStringLiteral("name"), QStringLiteral("members.col.name"), 260,
                   QString::fromLatin1(Repositories::MemberSort::kName)},
        ColumnSpec{QStringLiteral("phone"), QStringLiteral("members.col.phone"), 160,
                   QString::fromLatin1(Repositories::MemberSort::kPhone)},
        ColumnSpec{QStringLiteral("city"), QStringLiteral("members.col.city"), 140,
                   QString::fromLatin1(Repositories::MemberSort::kCity)},
        ColumnSpec{QStringLiteral("status"), QStringLiteral("members.col.status"), 120,
                   QString::fromLatin1(Repositories::MemberSort::kStatus)},
        ColumnSpec{QStringLiteral("loans"), QStringLiteral("members.col.loans"), 80,
                   QString::fromLatin1(Repositories::MemberSort::kLoans)},
    };
}

QList<FacetSpec> MemberListSpec::facets() const
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
        const auto years = m_members.listInscriptionYears(Repositories::ArchiveScope::Live);
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
        const auto cities = m_members.listCities(Repositories::ArchiveScope::Live);
        return cities ? codesToQStringList(cities.value()) : QStringList{};
    };

    return {status, sex, year, ageGroup, city};
}

QList<PageAction> MemberListSpec::actions() const
{
    QList<PageAction> out;

    PageAction loans;
    loans.id = QStringLiteral("loans");
    loans.labelKey = QStringLiteral("members.loans");
    loans.needs = PageAction::Needs::One;
    loans.run = [this](const ActionContext& ctx) {
        const qint64 id = ctx.ids.isEmpty() ? 0 : ctx.ids.first();
        return m_workflow.showLoans(ctx.parent, id);
    };
    out.append(loans);

    PageAction add;
    add.id = QStringLiteral("add");
    add.labelKey = QStringLiteral("members.addMember");
    add.needs = PageAction::Needs::Nothing;
    add.run = [this](const ActionContext& ctx) { return m_workflow.add(ctx.parent); };
    out.append(add);

    PageAction edit;
    edit.id = QStringLiteral("edit");
    edit.labelKey = QStringLiteral("members.edit");
    edit.needs = PageAction::Needs::One;
    edit.run = [this](const ActionContext& ctx) {
        const qint64 id = ctx.ids.isEmpty() ? 0 : ctx.ids.first();
        return m_workflow.edit(ctx.parent, id);
    };
    out.append(edit);

    PageAction archive;
    archive.id = QStringLiteral("archive");
    archive.labelKey = QStringLiteral("members.delete");
    archive.needs = PageAction::Needs::Some;
    archive.run = [this](const ActionContext& ctx) {
        QList<QString> labels;
        labels.reserve(ctx.ids.size());
        for (const qint64 id : ctx.ids) {
            if (const auto member = m_members.getMember(id)) {
                labels.append(displayName(*member));
            } else {
                labels.append(QString::number(id));
            }
        }
        return m_workflow.archive(ctx.parent, ctx.ids, labels);
    };
    out.append(archive);

    PageAction tickAll;
    tickAll.id = QStringLiteral("tickAll");
    tickAll.labelKey = QStringLiteral("list.tickAll");
    tickAll.needs = PageAction::Needs::Nothing;
    // ListPage::runAction handles ticking; no workflow call.
    out.append(tickAll);

    PageAction refresh;
    refresh.id = QStringLiteral("refresh");
    refresh.labelKey = QStringLiteral("metrics.refresh");
    refresh.needs = PageAction::Needs::Nothing;
    // Reload is owned by ListPage/PagedQueryModel; no MemberWorkflow method.
    out.append(refresh);

    return out;
}

EntitySet MemberListSpec::dependsOn() const
{
    return Entity::Members | Entity::Loans;
}

Core::Status MemberListSpec::load(const QueryState& state)
{
    const Repositories::MemberQuery query = toQuery(state);
    const auto rows = m_members.listMembers(query);
    if (!rows) {
        return Core::asStatus(rows.error());
    }
    const auto total = m_members.countMembers(query);
    if (!total) {
        return Core::asStatus(total.error());
    }

    m_records = rows.value();
    m_total = total.value();
    rebuildDisplayRows();
    return Core::Status::ok();
}

void MemberListSpec::retranslate()
{
    rebuildDisplayRows();
}

int MemberListSpec::rowCount() const
{
    return m_rows.size();
}

int MemberListSpec::totalCount() const
{
    return m_total;
}

qint64 MemberListSpec::idAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return 0;
    }
    return m_rows.at(row).id;
}

QVariant MemberListSpec::cell(int row, int column, int role) const
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

Core::Result<int> MemberListSpec::rankOf(qint64 id, const QueryState& state) const
{
    return m_members.rankOfMember(id, toQuery(state));
}

PreviewContent MemberListSpec::preview(qint64 id) const
{
    PreviewContent content;
    const auto memberResult = m_members.getMember(id);
    if (!memberResult) {
        return content;
    }
    const Repositories::MemberRecord& member = memberResult.value();

    if (!member.photoPath.empty()) {
        content.imagePath = qs(m_members.resolveImagePath(member.photoPath));
    }
    if (!member.idImagePath.empty()) {
        content.secondImagePath = qs(m_members.resolveImagePath(member.idImagePath));
    }

    const QString fullName = displayName(member);
    content.fields = {
        PreviewField{QStringLiteral("member.field.notes"), dashIfEmpty(member.notes), true},
        PreviewField{QStringLiteral("members.col.name"), fullName, false},
        PreviewField{QStringLiteral("member.field.number"), qs(member.membershipNumber), false},
        PreviewField{QStringLiteral("member.field.sex"),
                     member.sex.empty() ? dashIfEmpty(member.sex)
                                        : qs(Core::Strings::memberSexLabel(member.sex)),
                     false},
        PreviewField{QStringLiteral("member.field.status"),
                     qs(Core::Strings::memberStatusLabel(member.status)), false},
        PreviewField{QStringLiteral("member.field.activeUntil"), dashIfEmpty(member.activeUntil),
                     false},
        PreviewField{QStringLiteral("member.field.occupation"), dashIfEmpty(member.occupation),
                     false},
        PreviewField{QStringLiteral("member.field.ageGroup"),
                     member.ageGroup.empty()
                         ? dashIfEmpty(member.ageGroup)
                         : qs(Core::Strings::memberAgeGroupLabel(member.ageGroup)),
                     false},
        PreviewField{QStringLiteral("member.field.phone"), dashIfEmpty(member.phone), false},
        PreviewField{QStringLiteral("member.field.city"), dashIfEmpty(member.city), false},
        PreviewField{QStringLiteral("member.field.address"), dashIfEmpty(member.address), false},
        PreviewField{QStringLiteral("member.field.dateOfBirth"), dashIfEmpty(member.dateOfBirth),
                     false},
        PreviewField{QStringLiteral("member.field.registeredAt"), dashIfEmpty(member.registeredAt),
                     false},
        PreviewField{QStringLiteral("members.col.loans"), QString::number(member.activeLoanCount),
                     false},
        PreviewField{QStringLiteral("member.field.idImage"),
                     member.idImagePath.empty() ? T("common.emDash") : T("members.hasIdImage"),
                     false},
    };
    return content;
}

Repositories::MemberQuery MemberListSpec::toQuery(const QueryState& state) const
{
    Repositories::MemberQuery query;
    query.search = ss(state.search);
    query.statuses = facetValues(state, QStringLiteral("status"));
    query.sexes = facetValues(state, QStringLiteral("sex"));
    query.inscriptionYears = facetValues(state, QStringLiteral("year"));
    query.ageGroups = facetValues(state, QStringLiteral("ageGroup"));
    query.cities = facetValues(state, QStringLiteral("city"));
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

QString MemberListSpec::displayName(const Repositories::MemberRecord& member)
{
    if (!member.fullName.empty()) {
        return qs(member.fullName);
    }
    return QStringLiteral("%1 %2").arg(qs(member.firstName), qs(member.lastName)).trimmed();
}

QStringList MemberListSpec::buildCells(const Repositories::MemberRecord& member)
{
    return {
        qs(member.membershipNumber),
        displayName(member),
        qs(member.phone),
        qs(member.city),
        qs(Core::Strings::memberStatusLabel(member.status)),
        QString::number(member.activeLoanCount),
    };
}

void MemberListSpec::rebuildDisplayRows()
{
    m_rows.clear();
    m_rows.reserve(static_cast<int>(m_records.size()));
    for (const Repositories::MemberRecord& member : m_records) {
        DisplayRow row;
        row.id = member.id;
        row.cells = buildCells(member);
        m_rows.append(std::move(row));
    }
}

}  // namespace VLMS
