#include <VLMS/Repositories/MemberRepository.h>

#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Repositories/LoanPolicy.h>
#include <VLMS/Database/SqlText.h>

#include "LoanSql.h"
#include "MemberSql.h"
#include "RepoSql.h"
#include <VLMS/Database/SqliteSession.h>
#include <VLMS/Core/Text.h>

#include <algorithm>
#include <filesystem>
#include <regex>

namespace VLMS::Repositories {

namespace {

std::string memberSelectSql()
{
    return std::string(R"SQL(
        SELECT
            m.id,
            m.membership_number,
            m.first_name,
            m.last_name,
            COALESCE(m.sex, '') AS sex,
            COALESCE(m.date_of_birth, '') AS date_of_birth,
            COALESCE(m.email, '') AS email,
            COALESCE(m.phone, '') AS phone,
            COALESCE(m.address, '') AS address,
            COALESCE(m.city, '') AS city,
            )SQL")
        + MemberSql::statusExpression("m.") + R"SQL( AS status,
            COALESCE(m.photo_path, '') AS photo_path,
            COALESCE(m.id_image_path, '') AS id_image_path,
            COALESCE(m.notes, '') AS notes,
            m.registered_at,
            m.updated_at,
            COALESCE(m.occupation, '') AS occupation,
            COALESCE(m.age_group, '') AS age_group,
            COALESCE(m.full_name, '') AS full_name,
            (
                SELECT COUNT(*)
                FROM loans l
                WHERE l.member_id = m.id
                  AND l.returned_at IS NULL
            ) AS active_loan_count,
            (
                SELECT COUNT(*)
                FROM loans l
                WHERE l.member_id = m.id
            ) AS loan_count,
            COALESCE(m.archived_at, '') AS archived_at,
            COALESCE(m.active_until, '') AS active_until
        FROM members m
)SQL";
}

MemberRecord readMemberRow(Database::SqliteStatement& query)
{
    MemberRecord member;
    member.id = query.int64(0);
    member.membershipNumber = query.text(1);
    member.firstName = query.text(2);
    member.lastName = query.text(3);
    member.sex = query.text(4);
    member.dateOfBirth = query.text(5);
    member.email = query.text(6);
    member.phone = query.text(7);
    member.address = query.text(8);
    member.city = query.text(9);
    member.status = query.text(10);
    member.photoPath = query.text(11);
    member.idImagePath = query.text(12);
    member.notes = query.text(13);
    member.registeredAt = query.text(14);
    member.updatedAt = query.text(15);
    member.occupation = query.text(16);
    member.ageGroup = query.text(17);
    member.fullName = query.text(18);
    member.activeLoanCount = query.integer(19);
    member.loanCount = query.integer(20);
    member.archivedAt = query.text(21);
    member.activeUntil = query.text(22);
    return member;
}

bool codesContain(const std::vector<std::string>& codes, const std::string& value)
{
    return std::find(codes.begin(), codes.end(), value) != codes.end();
}

}  // namespace

MemberRepository::MemberRepository(Database::SqliteSession& session,
                                   std::string resourcesDirectory)
    : m_session(session), m_resourcesDirectory(std::move(resourcesDirectory))
{
}

std::vector<std::string> MemberRepository::statusCodes()
{
    return {
        MemberStatus::kActive,
        MemberStatus::kNonActive,
    };
}

std::vector<std::string> MemberRepository::sexCodes()
{
    return {
        MemberSex::kMale,
        MemberSex::kFemale,
    };
}

std::vector<std::string> MemberRepository::ageGroupCodes()
{
    return {
        MemberAgeGroup::kYouth,
        MemberAgeGroup::kAdult,
    };
}

bool MemberRepository::isValidStatus(const std::string& status) const
{
    return codesContain(statusCodes(), status);
}

bool MemberRepository::isValidSex(const std::string& sex) const
{
    const std::string trimmed = Core::trim(sex);
    return trimmed.empty() || codesContain(sexCodes(), trimmed);
}

bool MemberRepository::isValidEmail(const std::string& email)
{
    const std::string trimmed = Core::trim(email);
    if (trimmed.empty()) {
        return true;
    }

    constexpr int kMaxLocalPart = 64;
    constexpr int kMaxAddress = 254;
    if (static_cast<int>(trimmed.size()) > kMaxAddress) {
        return false;
    }

    static const std::regex pattern(
        R"RX(^[A-Za-z0-9!#$%&'*+/=?^_`{|}~-]+)RX"
        R"RX((?:\.[A-Za-z0-9!#$%&'*+/=?^_`{|}~-]+)*)RX"
        R"RX(@(?:[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?\.)+)RX"
        R"RX([A-Za-z]{2,}$)RX");

    const auto at = trimmed.find('@');
    if (at == std::string::npos || at > static_cast<std::size_t>(kMaxLocalPart)) {
        return false;
    }

    return std::regex_match(trimmed, pattern);
}

bool MemberRepository::isValidDateOfBirth(const std::string& dateOfBirth)
{
    const std::string trimmed = Core::trim(dateOfBirth);
    if (trimmed.empty()) {
        return false;
    }
    return LoanPolicy::parseIsoDate(trimmed).isValid();
}

bool MemberRepository::isDateOfBirthInFuture(const std::string& dateOfBirth)
{
    const Core::Date born = Core::Date::fromIso(Core::trim(dateOfBirth));
    return born.isValid() && Core::Clock::today() < born;
}

std::string MemberRepository::ageGroupFromBirthDate(const std::string& dateOfBirth,
                                                    const std::string& registeredAt)
{
    const Core::Date born = Core::Date::fromIso(Core::trim(dateOfBirth));
    if (!born.isValid()) {
        return {};
    }

    Core::Date on;
    const std::string stamp = Core::trim(registeredAt);
    if (stamp.size() >= 10) {
        on = Core::Date::fromIso(stamp.substr(0, 10));
    }
    if (!on.isValid()) {
        on = Core::Clock::today();
    }

    int age = on.year() - born.year();
    if (on.month() < born.month() || (on.month() == born.month() && on.day() < born.day())) {
        --age;
    }
    return age < 30 ? MemberAgeGroup::kYouth : MemberAgeGroup::kAdult;
}

std::string MemberRepository::statusOn(const std::string& activeUntil, const Core::Date& today)
{
    const Core::Date lastDay = Core::Date::fromIso(Core::trim(activeUntil));
    return lastDay.isValid() && lastDay >= today ? MemberStatus::kActive : MemberStatus::kNonActive;
}

std::string MemberRepository::activeUntilFor(const std::string& currentActiveUntil,
                                             const std::string& chosenStatus,
                                             const Core::Date& today)
{
    const std::string current = Core::trim(currentActiveUntil);
    const std::string chosen =
        Core::trim(chosenStatus).empty() ? std::string(MemberStatus::kActive) : Core::trim(chosenStatus);
    if (!current.empty() && statusOn(current, today) == chosen) {
        return current;
    }
    if (chosen == MemberStatus::kActive) {
        return today.addYears(1).addDays(-1).toIso();
    }
    return today.addDays(-1).toIso();
}

Core::Result<std::vector<MemberRecord>> MemberRepository::listMembers(const MemberQuery& query) const
{
    std::string sql = memberSelectSql() + "        WHERE 1 = 1\n";
    sql += MemberSql::filterClause(query);
    sql += MemberSql::orderClause(query);
    sql += R"SQL(
        LIMIT :limit OFFSET :offset
    )SQL";

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<std::vector<MemberRecord>>(q.error().detail);
    }
    MemberSql::bindFilters(*q, query);
    LoanSql::bindTodayIfPresent(*q, sql);
    if (!q->bind(":limit", static_cast<std::int64_t>(query.limit))
        || !q->bind(":offset", static_cast<std::int64_t>(query.offset))) {
        return RepoSql::sqlResult<std::vector<MemberRecord>>(m_session.lastError());
    }

    std::vector<MemberRecord> members;
    while (q->next()) {
        members.push_back(readMemberRow(*q));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<MemberRecord>>(m_session.lastError());
    }
    return Core::Result<std::vector<MemberRecord>>::ok(std::move(members));
}

Core::Result<int> MemberRepository::rankOfMember(const std::int64_t id, const MemberQuery& query) const
{
    std::string sql =
        "SELECT ranked.rank FROM (\n"
        "    SELECT m.id, (ROW_NUMBER() OVER (ORDER BY "
        + MemberSql::orderExpressions(query) + ")) - 1 AS rank\n"
        "    FROM members m\n"
        "    WHERE 1 = 1\n"
        + MemberSql::filterClause(query)
        + ") ranked WHERE ranked.id = :id\n";

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<int>(q.error().detail);
    }
    MemberSql::bindFilters(*q, query);
    LoanSql::bindTodayIfPresent(*q, sql);
    if (!q->bind(":id", id)) {
        return RepoSql::sqlResult<int>(m_session.lastError());
    }
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlResult<int>(m_session.lastError());
        }
        return RepoSql::notFoundResult<int>("error.member.notFound");
    }
    return Core::Result<int>::ok(q->integer(0));
}

Core::Result<int> MemberRepository::countMembers(const MemberQuery& query) const
{
    std::string sql = R"SQL(
        SELECT COUNT(*)
        FROM members m
        WHERE 1 = 1
    )SQL";
    sql += MemberSql::filterClause(query);

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<int>(q.error().detail);
    }
    MemberSql::bindFilters(*q, query);
    LoanSql::bindTodayIfPresent(*q, sql);
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlResult<int>(m_session.lastError());
        }
        return Core::Result<int>::ok(0);
    }
    return Core::Result<int>::ok(q->integer(0));
}

namespace {

Core::Result<std::vector<std::string>> listDistinctTexts(Database::SqliteSession& session,
                                                   const char* sql)
{
    auto q = session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<std::vector<std::string>>(q.error().detail);
    }

    std::vector<std::string> values;
    while (q->next()) {
        values.push_back(q->text(0));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<std::string>>(session.lastError());
    }
    return Core::Result<std::vector<std::string>>::ok(std::move(values));
}

}  // namespace

namespace {

/// The archived_at condition for a value list that follows a page's scope.
std::string memberScopeCondition(const ArchiveScope scope)
{
    switch (scope) {
    case ArchiveScope::Live:
        return "archived_at IS NULL";
    case ArchiveScope::Archived:
        return "archived_at IS NOT NULL";
    case ArchiveScope::Any:
        break;
    }
    return "1 = 1";
}

}  // namespace

Core::Result<std::vector<std::string>> MemberRepository::listCities(const ArchiveScope scope) const
{
    const std::string sql = "SELECT MIN(trim(city)) FROM members WHERE "
        + memberScopeCondition(scope)
        + " AND length(trim(city)) > 0"
          " GROUP BY trim(city) COLLATE NOCASE"
          " ORDER BY trim(city) COLLATE NOCASE";
    return listDistinctTexts(m_session, sql.c_str());
}

Core::Result<std::vector<std::string>> MemberRepository::listInscriptionYears(const ArchiveScope scope) const
{
    const std::string sql = "SELECT DISTINCT substr(registered_at, 1, 4) FROM members WHERE "
        + memberScopeCondition(scope)
        + " AND length(trim(registered_at)) >= 4"
          " ORDER BY 1 DESC";
    return listDistinctTexts(m_session, sql.c_str());
}

Core::Result<MemberRecord> MemberRepository::getMember(const std::int64_t id) const
{
    const std::string sql = memberSelectSql() + "        WHERE m.id = :id\n";
    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<MemberRecord>(q.error().detail);
    }
    if (!q->bind(":id", id) || !q->bind(LoanSql::todayPlaceholder(), Core::Clock::todayIso())) {
        return RepoSql::sqlResult<MemberRecord>(m_session.lastError());
    }
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlResult<MemberRecord>(m_session.lastError());
        }
        return RepoSql::notFoundResult<MemberRecord>("error.member.notFound");
    }
    return Core::Result<MemberRecord>::ok(readMemberRow(*q));
}

std::string MemberRepository::suggestMembershipNumber() const
{
    auto q = m_session.prepare(
        "SELECT COALESCE(MAX(CAST(membership_number AS INTEGER)), 0) + 1 FROM members");
    if (!q || !q->next()) {
        return "1";
    }
    return std::to_string(q->int64(0));
}

Core::Status MemberRepository::validateInput(const MemberInput& input) const
{
    if (Core::trim(input.firstName).empty() || Core::trim(input.lastName).empty()) {
        return RepoSql::validation("error.member.namesRequired");
    }
    if (!isValidSex(input.sex)) {
        return RepoSql::validation("error.member.invalidSex");
    }
    if (!isValidDateOfBirth(input.dateOfBirth)) {
        return RepoSql::validation("error.member.invalidDob");
    }
    if (isDateOfBirthInFuture(input.dateOfBirth)) {
        return RepoSql::validation("error.member.dobInFuture");
    }
    if (!isValidEmail(input.email)) {
        return RepoSql::validation("error.member.invalidEmail");
    }
    std::string status = Core::trim(input.status);
    if (status.empty()) {
        status = MemberStatus::kActive;
    }
    if (!isValidStatus(status)) {
        return RepoSql::validation("error.member.invalidStatus");
    }
    return Core::Status::ok();
}

Core::Status MemberRepository::recordStatusChange(const std::int64_t memberId,
                                            const std::string& oldStatus,
                                            const std::string& newStatus,
                                            const std::string& note)
{
    auto insert = m_session.prepare(R"SQL(
        INSERT INTO member_status_history (
            member_id, old_status, new_status, note
        ) VALUES (
            :member_id, :old_status, :new_status, :note
        )
    )SQL");
    if (!insert) {
        return RepoSql::sqlFailure(insert.error().detail);
    }
    if (!insert->bind(":member_id", memberId)
        || !insert->bindOptional(":old_status", Database::SqlText::nullableText(oldStatus))
        || !insert->bind(":new_status", newStatus)
        || !insert->bindOptional(":note", Database::SqlText::nullableText(note)) || !insert->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    return Core::Status::ok();
}

Core::Result<std::int64_t> MemberRepository::insertMemberRow(const MemberInput& input)
{
    if (const auto valid = validateInput(input); !valid) {
        return Core::Result<std::int64_t>::fail(valid.error().kind, valid.error().key,
                                          valid.error().detail);
    }

    std::string status = Core::trim(input.status);
    if (status.empty()) {
        status = MemberStatus::kActive;
    }

    const std::string membershipNumber = suggestMembershipNumber();
    // One stamp for both: the age group is judged on the inscription date, so
    // it must be the same instant that lands in registered_at. Leaving the
    // column to SQLite's default would read the wall clock, not Clock.
    const std::string registeredAt = Core::Clock::nowIso();
    const std::string ageGroup = ageGroupFromBirthDate(input.dateOfBirth, registeredAt);
    // Registered today, so the year starts today: registration + 1 year - 1 day.
    const std::string activeUntil = activeUntilFor({}, status, Core::Clock::today());

    auto insert = m_session.prepare(R"SQL(
        INSERT INTO members (
            membership_number, first_name, last_name, sex, date_of_birth,
            email, phone, address, city, active_until, notes, occupation, age_group,
            full_name, registered_at
        ) VALUES (
            :membership_number, :first_name, :last_name, :sex, :date_of_birth,
            :email, :phone, :address, :city, :active_until, :notes, :occupation,
            :age_group, :full_name, :registered_at
        )
    )SQL");
    if (!insert) {
        return RepoSql::sqlResult<std::int64_t>(insert.error().detail);
    }
    if (!insert->bind(":membership_number", membershipNumber)
        || !insert->bind(":first_name", Core::trim(input.firstName))
        || !insert->bind(":last_name", Core::trim(input.lastName))
        || !insert->bindOptional(":sex", Database::SqlText::nullableText(input.sex))
        || !insert->bindOptional(":date_of_birth", Database::SqlText::nullableText(input.dateOfBirth))
        || !insert->bindOptional(":email", Database::SqlText::nullableText(input.email))
        || !insert->bindOptional(":phone", Database::SqlText::nullableText(input.phone))
        || !insert->bindOptional(":address", Database::SqlText::nullableText(input.address))
        || !insert->bindOptional(":city", Database::SqlText::nullableText(input.city))
        || !insert->bind(":active_until", activeUntil)
        || !insert->bindOptional(":notes", Database::SqlText::nullableText(input.notes))
        || !insert->bindOptional(":occupation", Database::SqlText::nullableText(input.occupation))
        || !insert->bindOptional(":age_group", Database::SqlText::nullableText(ageGroup))
        || !insert->bindOptional(":full_name", Database::SqlText::nullableText(input.fullName))
        || !insert->bind(":registered_at", registeredAt)
        || !insert->exec()) {
        return RepoSql::sqlResult<std::int64_t>(m_session.lastError());
    }

    const std::int64_t memberId = m_session.lastInsertRowId();
    if (const auto history =
            recordStatusChange(memberId, {}, status, "Registered, active until " + activeUntil);
        !history) {
        return Core::Result<std::int64_t>::fail(history.error().kind, history.error().key,
                                          history.error().detail);
    }
    return Core::Result<std::int64_t>::ok(memberId);
}

Core::Result<std::int64_t> MemberRepository::createMember(const MemberInput& input)
{
    std::int64_t memberId = 0;
    const Core::Status work = m_session.transaction([&] {
        const auto inserted = insertMemberRow(input);
        if (!inserted) {
            return Core::asStatus(inserted);
        }
        memberId = inserted.value();
        return Core::Status::ok();
    });
    if (!work) {
        return Core::Result<std::int64_t>::fail(work.error().kind, work.error().key, work.error().detail);
    }
    return Core::Result<std::int64_t>::ok(memberId);
}

Core::Status MemberRepository::applyMemberFields(const std::int64_t id, const MemberInput& input)
{
    if (const auto valid = validateInput(input); !valid) {
        return valid;
    }

    const auto existing = getMember(id);
    if (!existing) {
        return Core::asStatus(existing);
    }

    // existing->status is derived on read, so this is what the librarian saw.
    const std::string oldStatus = existing->status;
    const std::string status = Core::trim(input.status).empty() ? oldStatus : Core::trim(input.status);
    const std::string activeUntil = activeUntilFor(existing->activeUntil, status, Core::Clock::today());
    const std::string ageGroup =
        Core::trim(input.dateOfBirth) == Core::trim(existing->dateOfBirth)
            ? existing->ageGroup
            : ageGroupFromBirthDate(input.dateOfBirth, existing->registeredAt);
    auto update = m_session.prepare(R"SQL(
        UPDATE members SET
            first_name = :first_name,
            last_name = :last_name,
            sex = :sex,
            date_of_birth = :date_of_birth,
            email = :email,
            phone = :phone,
            address = :address,
            city = :city,
            active_until = :active_until,
            notes = :notes,
            occupation = :occupation,
            age_group = :age_group,
            full_name = :full_name,
            updated_at = :updated_at
        WHERE id = :id
    )SQL");
    if (!update) {
        return RepoSql::sqlFailure(update.error().detail);
    }
    if (!update->bind(":first_name", Core::trim(input.firstName))
        || !update->bind(":last_name", Core::trim(input.lastName))
        || !update->bindOptional(":sex", Database::SqlText::nullableText(input.sex))
        || !update->bindOptional(":date_of_birth", Database::SqlText::nullableText(input.dateOfBirth))
        || !update->bindOptional(":email", Database::SqlText::nullableText(input.email))
        || !update->bindOptional(":phone", Database::SqlText::nullableText(input.phone))
        || !update->bindOptional(":address", Database::SqlText::nullableText(input.address))
        || !update->bindOptional(":city", Database::SqlText::nullableText(input.city))
        || !update->bind(":active_until", activeUntil)
        || !update->bindOptional(":notes", Database::SqlText::nullableText(input.notes))
        || !update->bindOptional(":occupation", Database::SqlText::nullableText(input.occupation))
        || !update->bindOptional(":age_group", Database::SqlText::nullableText(ageGroup))
        || !update->bindOptional(":full_name", Database::SqlText::nullableText(input.fullName))
        || !update->bind(":updated_at", Core::Clock::nowIso()) || !update->bind(":id", id)
        || !update->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }

    if (oldStatus != status) {
        const std::string note = status == MemberStatus::kActive
            ? "Renewed until " + activeUntil
            : std::string("Ended early");
        if (const auto history = recordStatusChange(id, oldStatus, status, note); !history) {
            return history;
        }
    }
    return Core::Status::ok();
}

Core::Status MemberRepository::updateMember(const std::int64_t id, const MemberInput& input)
{
    return m_session.transaction([&] { return applyMemberFields(id, input); });
}

Core::Result<std::int64_t> MemberRepository::saveNewMember(const MemberWrite& write)
{
    std::int64_t memberId = 0;
    const Core::Status work = m_session.transaction([&] {
        const auto inserted = insertMemberRow(write.member);
        if (!inserted) {
            return Core::asStatus(inserted);
        }
        memberId = inserted.value();

        if (!write.photoSourcePath.empty()) {
            if (const auto photo = storeMemberImage(memberId, write.photoSourcePath, ImageSlot::Photo);
                !photo) {
                return photo;
            }
        }
        if (!write.idImageSourcePath.empty()) {
            if (const auto idImage =
                    storeMemberImage(memberId, write.idImageSourcePath, ImageSlot::IdCard);
                !idImage) {
                return idImage;
            }
        }
        return Core::Status::ok();
    });
    if (!work) {
        return Core::Result<std::int64_t>::fail(work.error().kind, work.error().key, work.error().detail);
    }
    return Core::Result<std::int64_t>::ok(memberId);
}

namespace {

// A stored image path is only safe to delete when it is exactly what this
// repository itself would have written for this member (storeMemberImage's
// "members/<id>/<basename>.<ext>" layout): no root name/directory (rules out
// an absolute path, and on Windows a rooted driveless path such as
// "\Users\x\p.jpg"), no ".." component once normalized (rules out escaping
// resources/), and the first two components must be "members" and this
// member's id (rules out another member's file).
bool isOwnMemberImage(const std::filesystem::path& stored, const std::int64_t memberId)
{
    if (stored.empty()) {
        return false;
    }
    const std::filesystem::path normalized = stored.lexically_normal();
    if (normalized.has_root_name() || normalized.has_root_directory()) {
        return false;
    }
    for (const auto& component : normalized) {
        if (component == "..") {
            return false;
        }
    }
    auto it = normalized.begin();
    const auto end = normalized.end();
    if (it == end || *it != "members") {
        return false;
    }
    ++it;
    return it != end && *it == std::to_string(memberId);
}

}  // namespace

Core::Status MemberRepository::saveExistingMember(const std::int64_t id, const MemberWrite& write)
{
    std::vector<std::string> clearedPaths;
    const Core::Status saved = m_session.transaction([&] {
        if (const auto updated = applyMemberFields(id, write.member); !updated) {
            return updated;
        }
        if (!write.photoSourcePath.empty()) {
            if (const auto photo = storeMemberImage(id, write.photoSourcePath, ImageSlot::Photo);
                !photo) {
                return photo;
            }
        } else if (write.clearPhoto) {
            if (const auto cleared = clearMemberImage(id, ImageSlot::Photo, clearedPaths);
                !cleared) {
                return cleared;
            }
        }
        if (!write.idImageSourcePath.empty()) {
            if (const auto idImage = storeMemberImage(id, write.idImageSourcePath, ImageSlot::IdCard);
                !idImage) {
                return idImage;
            }
        } else if (write.clearIdImage) {
            if (const auto cleared = clearMemberImage(id, ImageSlot::IdCard, clearedPaths);
                !cleared) {
                return cleared;
            }
        }
        return Core::Status::ok();
    });

    // Only after the commit: a save that failed must not have lost the file.
    if (saved) {
        for (const std::string& stored : clearedPaths) {
            // Only delete a file this repository itself would have written for
            // this member. An absolute path may be the librarian's own
            // original (imported data), not a copy this repository made, and
            // a relative path escaping resources/ or naming another member's
            // folder must never be removed either.
            if (!isOwnMemberImage(std::filesystem::path(stored), id)) {
                continue;
            }
            std::error_code ignored;
            std::filesystem::remove(resolveImagePath(stored), ignored);
        }
    }
    return saved;
}

Core::Result<MemberRepository::MemberRemovalBlock> MemberRepository::removalBlock(
    const std::int64_t id) const
{
    auto loanCheck = m_session.prepare(
        "SELECT COUNT(*), COALESCE(SUM(returned_at IS NULL), 0) "
        "FROM loans WHERE member_id = :member_id");
    if (!loanCheck) {
        return RepoSql::sqlResult<MemberRemovalBlock>(loanCheck.error().detail);
    }
    if (!loanCheck->bind(":member_id", id) || !loanCheck->next()) {
        return RepoSql::sqlResult<MemberRemovalBlock>(m_session.lastError());
    }

    if (loanCheck->integer(1) > 0) {
        return Core::Result<MemberRemovalBlock>::ok(MemberRemovalBlock::OpenLoans);
    }
    if (loanCheck->integer(0) > 0) {
        return Core::Result<MemberRemovalBlock>::ok(MemberRemovalBlock::LoanHistory);
    }
    return Core::Result<MemberRemovalBlock>::ok(MemberRemovalBlock::None);
}

Core::Status MemberRepository::canArchiveMember(const std::int64_t id) const
{
    const auto block = removalBlock(id);
    if (!block) {
        return Core::asStatus(block);
    }
    if (block.value() == MemberRemovalBlock::OpenLoans) {
        return RepoSql::validation("error.member.archiveHasLoans");
    }

    auto read = m_session.prepare(
        "SELECT 1 FROM members WHERE id = :id AND archived_at IS NULL");
    if (!read) {
        return RepoSql::sqlFailure(read.error().detail);
    }
    if (!read->bind(":id", id)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (!read->next()) {
        if (!read->ok()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return RepoSql::notFound("error.member.notFound");
    }
    return Core::Status::ok();
}

Core::Status MemberRepository::archiveMember(const std::int64_t id)
{
    return m_session.transaction([&] {
    if (const Core::Status gate = canArchiveMember(id); !gate) {
        return gate;
    }

    auto archive = m_session.prepare(
        "UPDATE members SET archived_at = :stamp, updated_at = :stamp "
        "WHERE id = :id AND archived_at IS NULL");
    if (!archive) {
        return RepoSql::sqlFailure(archive.error().detail);
    }
    if (!archive->bind(":stamp", Core::Clock::nowIso()) || !archive->bind(":id", id)
        || !archive->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (archive->changes() <= 0) {
        return RepoSql::notFound("error.member.notFound");
    }
    return Core::Status::ok();
    });
}

Core::Status MemberRepository::restoreMember(const std::int64_t id)
{
    auto restore = m_session.prepare(
        "UPDATE members SET archived_at = NULL, updated_at = :stamp "
        "WHERE id = :id AND archived_at IS NOT NULL");
    if (!restore) {
        return RepoSql::sqlFailure(restore.error().detail);
    }
    if (!restore->bind(":stamp", Core::Clock::nowIso()) || !restore->bind(":id", id)
        || !restore->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (restore->changes() > 0) {
        return Core::Status::ok();
    }

    auto exists = m_session.prepare("SELECT 1 FROM members WHERE id = :id");
    if (!exists) {
        return RepoSql::sqlFailure(exists.error().detail);
    }
    if (!exists->bind(":id", id)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (exists->next()) {
        return RepoSql::validation("error.member.notArchived");
    }
    return RepoSql::notFound("error.member.notFound");
}

Core::Status MemberRepository::canPurgeMember(const std::int64_t id) const
{
    auto read = m_session.prepare("SELECT archived_at IS NOT NULL FROM members WHERE id = :id");
    if (!read) {
        return RepoSql::sqlFailure(read.error().detail);
    }
    if (!read->bind(":id", id)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (!read->next()) {
        if (!read->ok()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return RepoSql::notFound("error.member.notFound");
    }
    // Delete archives; only the Archive destroys. Asked first, so a live
    // member is refused for being live rather than for their loans.
    if (read->integer(0) == 0) {
        return RepoSql::validation("error.member.notArchived");
    }

    const auto block = removalBlock(id);
    if (!block) {
        return Core::asStatus(block);
    }
    switch (block.value()) {
    case MemberRemovalBlock::OpenLoans:
        return RepoSql::validation("error.member.deleteHasLoans");
    case MemberRemovalBlock::LoanHistory:
        return RepoSql::validation("error.member.hasHistory");
    case MemberRemovalBlock::None:
        break;
    }
    return Core::Status::ok();
}

Core::Status MemberRepository::purgeMember(const std::int64_t id)
{
    const Core::Status removed = m_session.transaction([&] {
        if (const Core::Status gate = canPurgeMember(id); !gate) {
            return gate;
        }

    auto remove = m_session.prepare("DELETE FROM members WHERE id = :id");
    if (!remove) {
        return RepoSql::sqlFailure(remove.error().detail);
    }
    if (!remove->bind(":id", id) || !remove->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (remove->changes() <= 0) {
        return RepoSql::notFound("error.member.notFound");
    }
    return Core::Status::ok();
    });
    if (!removed) {
        return removed;
    }

    const std::filesystem::path memberDir =
        std::filesystem::path(m_resourcesDirectory) / "members" / std::to_string(id);
    if (std::filesystem::exists(memberDir)) {
        std::error_code error;
        std::filesystem::remove_all(memberDir, error);
    }
    return Core::Status::ok();
}

std::string MemberRepository::resolveImagePath(const std::string& storedPath) const
{
    if (storedPath.empty()) {
        return {};
    }
    const std::filesystem::path path(storedPath);
    if (path.is_absolute()) {
        return storedPath;
    }
    return (std::filesystem::path(m_resourcesDirectory) / storedPath).string();
}

Core::Status MemberRepository::storeMemberImage(const std::int64_t memberId,
                                          const std::string& sourceFilePath,
                                          const ImageSlot slot)
{
    const char* basename = slot == ImageSlot::Photo ? "photo" : "id";

    if (sourceFilePath.empty()) {
        return RepoSql::validation("error.image.emptyPath");
    }

    const std::filesystem::path source(sourceFilePath);
    std::error_code error;
    if (!std::filesystem::exists(source, error) || !std::filesystem::is_regular_file(source, error)) {
        return RepoSql::validation("error.image.missingFile");
    }

    const std::filesystem::path memberDir =
        std::filesystem::path(m_resourcesDirectory) / "members" / std::to_string(memberId);
    std::filesystem::create_directories(memberDir, error);
    if (error) {
        return RepoSql::validation("error.image.copyFailed");
    }

    std::string extension = source.extension().string();
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(0, 1);
    }
    if (extension.empty()) {
        extension = "jpg";
    }
    const std::string relativePath =
        "members/" + std::to_string(memberId) + "/" + basename + "." + extension;
    const std::filesystem::path targetPath =
        std::filesystem::path(m_resourcesDirectory) / relativePath;

    if (std::filesystem::exists(targetPath, error)) {
        std::filesystem::remove(targetPath, error);
    }
    if (!std::filesystem::copy_file(source, targetPath, error)) {
        return RepoSql::validation("error.image.copyFailed");
    }

    const char* sql = slot == ImageSlot::Photo
        ? "UPDATE members SET photo_path = :path, updated_at = :updated_at WHERE id = :id"
        : "UPDATE members SET id_image_path = :path, updated_at = :updated_at WHERE id = :id";
    auto update = m_session.prepare(sql);
    if (!update) {
        return RepoSql::sqlFailure(update.error().detail);
    }
    if (!update->bind(":path", relativePath) || !update->bind(":updated_at", Core::Clock::nowIso())
        || !update->bind(":id", memberId) || !update->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    return Core::Status::ok();
}

Core::Status MemberRepository::clearMemberImage(const std::int64_t memberId,
                                          const ImageSlot slot,
                                          std::vector<std::string>& clearedPaths)
{
    std::string stored;
    {
        auto select = m_session.prepare(slot == ImageSlot::Photo
                                            ? "SELECT photo_path FROM members WHERE id = :id"
                                            : "SELECT id_image_path FROM members WHERE id = :id");
        if (!select) {
            return RepoSql::sqlFailure(select.error().detail);
        }
        if (!select->bind(":id", memberId)) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        if (select->next()) {
            stored = select->text(0);
        }
    }
    if (stored.empty()) {
        return Core::Status::ok();
    }

    const char* sql = slot == ImageSlot::Photo
        ? "UPDATE members SET photo_path = NULL, updated_at = :updated_at WHERE id = :id"
        : "UPDATE members SET id_image_path = NULL, updated_at = :updated_at WHERE id = :id";
    auto update = m_session.prepare(sql);
    if (!update) {
        return RepoSql::sqlFailure(update.error().detail);
    }
    if (!update->bind(":updated_at", Core::Clock::nowIso()) || !update->bind(":id", memberId)
        || !update->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    clearedPaths.push_back(stored);
    return Core::Status::ok();
}

Core::Status MemberRepository::setPhotoImage(const std::int64_t memberId,
                                       const std::string& sourceFilePath)
{
    return storeMemberImage(memberId, sourceFilePath, ImageSlot::Photo);
}

Core::Status MemberRepository::setIdImage(const std::int64_t memberId, const std::string& sourceFilePath)
{
    return storeMemberImage(memberId, sourceFilePath, ImageSlot::IdCard);
}

}  // namespace VLMS::Repositories
