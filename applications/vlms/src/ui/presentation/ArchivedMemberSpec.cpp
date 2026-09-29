#include "ui/presentation/ArchivedMemberSpec.h"

#include "QtBridge.h"
#include "ui/views/UiHelpers.h"

#include <VLMS/Core/Strings.h>
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

ArchivedMemberSpec::ArchivedMemberSpec(Repositories::MemberRepository& members,
                                       ArchiveWorkflow& workflow)
    : m_members(members)
    , m_workflow(workflow)
{
}

QString ArchivedMemberSpec::id() const
{
    return QStringLiteral("archived-members");
}

QList<ColumnSpec> ArchivedMemberSpec::columns() const
{
    return {
        ColumnSpec{QStringLiteral("number"), QStringLiteral("archive.col.number"), 140,
                   QString::fromLatin1(Repositories::MemberSort::kNumber)},
        ColumnSpec{QStringLiteral("name"), QStringLiteral("archive.col.name"), 240,
                   QString::fromLatin1(Repositories::MemberSort::kName)},
        ColumnSpec{QStringLiteral("city"), QStringLiteral("archive.col.city"), 160,
                   QString::fromLatin1(Repositories::MemberSort::kCity)},
        ColumnSpec{QStringLiteral("status"), QStringLiteral("archive.col.status"), 140,
                   QString::fromLatin1(Repositories::MemberSort::kStatus)},
        ColumnSpec{QStringLiteral("loans"), QStringLiteral("archive.col.loans"), 80,
                   QString::fromLatin1(Repositories::MemberSort::kAllLoans)},
        ColumnSpec{QStringLiteral("archivedAt"), QStringLiteral("archive.col.archivedAt"), 160,
                   QString::fromLatin1(Repositories::MemberSort::kArchivedAt)},
    };
}

QList<FacetSpec> ArchivedMemberSpec::facets() const
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
        const auto years = m_members.listInscriptionYears(Repositories::ArchiveScope::Archived);
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
        const auto cities = m_members.listCities(Repositories::ArchiveScope::Archived);
        return cities ? codesToQStringList(cities.value()) : QStringList{};
    };

    return {status, sex, year, ageGroup, city};
}

QList<PageAction> ArchivedMemberSpec::actions() const
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
        const auto member = m_members.getMember(ctx.ids.first());
        return member && member->loanCount > 0;
    };
    loans.run = [this](const ActionContext& ctx) {
        const qint64 id = ctx.ids.isEmpty() ? 0 : ctx.ids.first();
        QString name;
        if (const auto member = m_members.getMember(id)) {
            name = displayName(*member);
        }
        return m_workflow.showLoans(ctx.parent, ArchiveType::Members, id, name);
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
            if (const auto member = m_members.getMember(id)) {
                labels.append(displayName(*member));
            } else {
                labels.append(QString::number(id));
            }
        }
        return m_workflow.restore(ctx.parent, ArchiveType::Members, ctx.ids, labels);
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
        if (ctx.ids.size() > 1) {
            return true;
        }
        const auto member = m_members.getMember(ctx.ids.first());
        return member && member->loanCount == 0;
    };
    purge.run = [this](const ActionContext& ctx) {
        QList<QString> labels;
        labels.reserve(ctx.ids.size());
        for (const qint64 id : ctx.ids) {
            if (const auto member = m_members.getMember(id)) {
                labels.append(displayName(*member));
            } else {
                labels.append(QString::number(id));
            }
        }
        return m_workflow.purge(ctx.parent, ArchiveType::Members, ctx.ids, labels);
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

EntitySet ArchivedMemberSpec::dependsOn() const
{
    return Entity::Members;
}

Core::Status ArchivedMemberSpec::load(const QueryState& state)
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

void ArchivedMemberSpec::retranslate()
{
    rebuildDisplayRows();
}

int ArchivedMemberSpec::rowCount() const
{
    return m_rows.size();
}

int ArchivedMemberSpec::totalCount() const
{
    return m_total;
}

qint64 ArchivedMemberSpec::idAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) {
        return 0;
    }
    return m_rows.at(row).id;
}

QVariant ArchivedMemberSpec::cell(int row, int column, int role) const
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

Core::Result<int> ArchivedMemberSpec::rankOf(qint64 id, const QueryState& state) const
{
    return m_members.rankOfMember(id, toQuery(state));
}

PreviewContent ArchivedMemberSpec::preview(qint64 id) const
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

    content.fields = {
        PreviewField{QStringLiteral("archive.col.number"), qs(member.membershipNumber), false},
        PreviewField{QStringLiteral("archive.col.name"), displayName(member), false},
        PreviewField{QStringLiteral("archive.col.city"), dashIfEmpty(member.city), false},
        PreviewField{QStringLiteral("archive.col.status"),
                     qs(Core::Strings::memberStatusLabel(member.status)), false},
        PreviewField{QStringLiteral("archive.col.archivedAt"), qs(member.archivedAt), false},
    };
    return content;
}

Repositories::MemberQuery ArchivedMemberSpec::toQuery(const QueryState& state) const
{
    Repositories::MemberQuery query;
    query.search = ss(state.search);
    query.statuses = facetValues(state, QStringLiteral("status"));
    query.sexes = facetValues(state, QStringLiteral("sex"));
    query.inscriptionYears = facetValues(state, QStringLiteral("year"));
    query.ageGroups = facetValues(state, QStringLiteral("ageGroup"));
    query.cities = facetValues(state, QStringLiteral("city"));
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

QString ArchivedMemberSpec::displayName(const Repositories::MemberRecord& member)
{
    return qs(member.firstName + " " + member.lastName);
}

QStringList ArchivedMemberSpec::buildCells(const Repositories::MemberRecord& member)
{
    return {
        qs(member.membershipNumber),
        displayName(member),
        dashIfEmpty(member.city),
        qs(Core::Strings::memberStatusLabel(member.status)),
        QString::number(member.loanCount),
        qs(member.archivedAt),
    };
}

void ArchivedMemberSpec::rebuildDisplayRows()
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
