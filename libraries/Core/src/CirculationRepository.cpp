#include <VLMS/Core/CirculationRepository.h>

#include <VLMS/Core/Clock.h>
#include <VLMS/Core/LoanPolicy.h>
#include <VLMS/Core/MemberTypes.h>
#include <VLMS/Core/SqlText.h>

#include "LoanSql.h"
#include "MemberSql.h"
#include "RepoSql.h"
#include "SqliteSession.h"
#include "Text.h"

using VLMS::Clock;
using VLMS::Result;
using VLMS::Status;
using VLMS::SqliteStatement;
using VLMS::trim;
using VLMS::SqlText::escapeLike;
using VLMS::SqlText::nullableText;
namespace LoanPolicy = VLMS::LoanPolicy;
namespace LoanSql = VLMS::LoanSql;
namespace MemberSql = VLMS::MemberSql;
namespace RepoSql = VLMS::RepoSql;

namespace {

constexpr const char* kLoanAlias = "l.";

std::string loanSelectSql()
{
    return std::string(R"SQL(
        SELECT
            l.id,
            l.member_id,
            m.membership_number,
            TRIM(m.first_name || ' ' || m.last_name) AS member_name,
            )SQL")
        + MemberSql::statusExpression("m.") + R"SQL( AS member_status,
            l.book_copy_id,
            COALESCE(NULLIF(TRIM(bc.global_copy_id), ''), bc.local_id) AS copy_code,
            b.title AS book_title,
            COALESCE(a.name, '') AS author_name,
            COALESCE(b.cover_image_path, '') AS cover_image_path,
            l.borrowed_at,
            l.due_at,
            COALESCE(l.returned_at, '') AS returned_at,
            COALESCE(l.notes, '') AS notes,
            CASE WHEN )SQL"
        + LoanSql::isOverdue(kLoanAlias) + R"SQL( THEN 1 ELSE 0 END AS is_overdue,
            COALESCE(l.archived_at, '') AS archived_at,
            COALESCE(m.photo_path, '') AS member_photo_path
        FROM loans l
        INNER JOIN members m ON m.id = l.member_id
        INNER JOIN book_copies bc ON bc.id = l.book_copy_id
        INNER JOIN books b ON b.id = bc.book_id
        LEFT JOIN authors a ON a.id = b.author_id
    )SQL";
}

LoanRecord readLoanRow(SqliteStatement& query)
{
    LoanRecord loan;
    loan.id = query.int64(0);
    loan.memberId = query.int64(1);
    loan.membershipNumber = query.text(2);
    loan.memberName = query.text(3);
    loan.memberStatus = query.text(4);
    loan.bookCopyId = query.int64(5);
    loan.copyCode = query.text(6);
    loan.bookTitle = query.text(7);
    loan.authorName = query.text(8);
    loan.coverImagePath = query.text(9);
    loan.borrowedAt = query.text(10);
    loan.dueAt = query.text(11);
    loan.returnedAt = query.text(12);
    loan.notes = query.text(13);
    loan.isOverdue = query.integer(14) != 0;
    loan.archivedAt = query.text(15);
    loan.memberPhotoPath = query.text(16);
    return loan;
}

}  // namespace

CirculationRepository::CirculationRepository(VLMS::SqliteSession& session)
    : m_session(session)
{
}

std::vector<std::string> CirculationRepository::filterCodes()
{
    return {
        LoanFilter::kOpen,
        LoanFilter::kOverdue,
        LoanFilter::kReturned,
        LoanFilter::kAll,
    };
}

Result<std::vector<LoanRecord>> CirculationRepository::listLoans(const LoanQuery& query) const
{
    std::string sql = loanSelectSql() + "        WHERE 1 = 1\n";
    sql += LoanSql::filterClause(query);
    sql += LoanSql::orderClause(query);
    sql += R"SQL(
        LIMIT :limit OFFSET :offset
    )SQL";

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<std::vector<LoanRecord>>(q.error().detail);
    }
    LoanSql::bindTodayIfPresent(*q, sql);
    LoanSql::bindFilters(*q, query);
    if (!q->bind(":limit", static_cast<std::int64_t>(query.limit))
        || !q->bind(":offset", static_cast<std::int64_t>(query.offset))) {
        return RepoSql::sqlResult<std::vector<LoanRecord>>(m_session.lastError());
    }

    std::vector<LoanRecord> loans;
    while (q->next()) {
        loans.push_back(readLoanRow(*q));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<LoanRecord>>(m_session.lastError());
    }
    return Result<std::vector<LoanRecord>>::ok(std::move(loans));
}

Result<int> CirculationRepository::rankOfLoan(const std::int64_t id, const LoanQuery& query) const
{
    std::string sql =
        "SELECT ranked.rank FROM (\n"
        "    SELECT l.id, (ROW_NUMBER() OVER (ORDER BY "
        + LoanSql::orderExpressions(query) + ")) - 1 AS rank\n"
        "    FROM loans l\n"
        "    INNER JOIN members m ON m.id = l.member_id\n"
        "    INNER JOIN book_copies bc ON bc.id = l.book_copy_id\n"
        "    INNER JOIN books b ON b.id = bc.book_id\n"
        "    LEFT JOIN authors a ON a.id = b.author_id\n"
        "    WHERE 1 = 1\n"
        + LoanSql::filterClause(query)
        + ") ranked WHERE ranked.id = :id\n";

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<int>(q.error().detail);
    }
    LoanSql::bindTodayIfPresent(*q, sql);
    LoanSql::bindFilters(*q, query);
    if (!q->bind(":id", id)) {
        return RepoSql::sqlResult<int>(m_session.lastError());
    }
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlResult<int>(m_session.lastError());
        }
        return RepoSql::notFoundResult<int>("error.loan.notFound");
    }
    return Result<int>::ok(q->integer(0));
}

Result<int> CirculationRepository::countLoans(const LoanQuery& query) const
{
    std::string sql = R"SQL(
        SELECT COUNT(*)
        FROM loans l
        INNER JOIN members m ON m.id = l.member_id
        INNER JOIN book_copies bc ON bc.id = l.book_copy_id
        INNER JOIN books b ON b.id = bc.book_id
        LEFT JOIN authors a ON a.id = b.author_id
        WHERE 1 = 1
    )SQL";
    sql += LoanSql::filterClause(query);

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<int>(q.error().detail);
    }
    LoanSql::bindTodayIfPresent(*q, sql);
    LoanSql::bindFilters(*q, query);
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlResult<int>(m_session.lastError());
        }
        return Result<int>::ok(0);
    }
    return Result<int>::ok(q->integer(0));
}

Result<std::vector<std::string>> CirculationRepository::listLoanYears(const ArchiveScope scope) const
{
    std::string sql = "SELECT DISTINCT substr(borrowed_at, 1, 4) FROM loans"
                      " WHERE length(trim(borrowed_at)) >= 4";
    if (scope == ArchiveScope::Live) {
        sql += " AND archived_at IS NULL";
    } else if (scope == ArchiveScope::Archived) {
        sql += " AND archived_at IS NOT NULL";
    }
    sql += " ORDER BY 1 DESC";
    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<std::vector<std::string>>(q.error().detail);
    }
    std::vector<std::string> years;
    while (q->next()) {
        years.push_back(q->text(0));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<std::string>>(m_session.lastError());
    }
    return Result<std::vector<std::string>>::ok(std::move(years));
}

Result<LoanRecord> CirculationRepository::getLoan(const std::int64_t id) const
{
    auto q = m_session.prepare(loanSelectSql() + "        WHERE l.id = :id\n");
    if (!q) {
        return RepoSql::sqlResult<LoanRecord>(q.error().detail);
    }
    if (!q->bind(LoanSql::todayPlaceholder(), Clock::todayIso()) || !q->bind(":id", id)) {
        return RepoSql::sqlResult<LoanRecord>(m_session.lastError());
    }
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlResult<LoanRecord>(m_session.lastError());
        }
        return RepoSql::notFoundResult<LoanRecord>("error.loan.notFound");
    }
    return Result<LoanRecord>::ok(readLoanRow(*q));
}

Status CirculationRepository::memberCanBorrow(const std::int64_t memberId) const
{
    auto q = m_session.prepare("SELECT " + MemberSql::isActive("")
                               + ", archived_at FROM members WHERE id = :id");
    if (!q) {
        return RepoSql::sqlFailure(q.error().detail);
    }
    if (!q->bind(":id", memberId) || !q->bind(LoanSql::todayPlaceholder(), Clock::todayIso())) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (!q->next()) {
        if (!q->ok()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return RepoSql::notFound("error.member.notFound");
    }

    if (!q->isNull(1)) {
        return RepoSql::validation("error.loan.memberArchived");
    }
    if (q->integer(0) == 0) {
        return RepoSql::validation("error.loan.memberInactive");
    }
    return Status::ok();
}

Status CirculationRepository::copyIsAvailable(const std::int64_t bookCopyId) const
{
    auto exists = m_session.prepare("SELECT archived_at IS NOT NULL FROM book_copies WHERE id = :id");
    if (!exists) {
        return RepoSql::sqlFailure(exists.error().detail);
    }
    if (!exists->bind(":id", bookCopyId)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (!exists->next()) {
        if (!exists->ok()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return RepoSql::notFound("error.loan.copyNotFound");
    }
    if (exists->integer(0) != 0) {
        return RepoSql::validation("error.loan.copyArchived");
    }

    auto openLoan = m_session.prepare(
        "SELECT id FROM loans WHERE book_copy_id = :copy_id AND returned_at IS NULL LIMIT 1");
    if (!openLoan) {
        return RepoSql::sqlFailure(openLoan.error().detail);
    }
    if (!openLoan->bind(":copy_id", bookCopyId)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (openLoan->next()) {
        return RepoSql::validation("error.loan.copyOnLoan");
    }
    if (!openLoan->ok()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    return Status::ok();
}

Result<std::int64_t> CirculationRepository::createLoan(const LoanInput& input)
{
    if (input.memberId <= 0) {
        return RepoSql::validationResult<std::int64_t>("error.loan.memberRequired");
    }
    if (input.bookCopyId <= 0) {
        return RepoSql::validationResult<std::int64_t>("error.loan.copyRequired");
    }

    const std::string borrowedAt =
        trim(input.borrowedAt).empty() ? Clock::today().toIso() : trim(input.borrowedAt);
    const std::string dueAt = trim(input.dueAt).empty()
        ? LoanPolicy::suggestedDueDate(Clock::today()).toIso()
        : trim(input.dueAt);

    if (const auto dates = LoanPolicy::validateLoanDates(borrowedAt, dueAt, Clock::today());
        !dates) {
        return Result<std::int64_t>::fail(VLMS::ErrorKind::Validation, dates.key);
    }

    std::int64_t newId = 0;
    const Status work = m_session.transaction([&] {
        if (const auto member = memberCanBorrow(input.memberId); !member) {
            return member;
        }
        if (const auto copy = copyIsAvailable(input.bookCopyId); !copy) {
            return copy;
        }

        auto insert = m_session.prepare(R"SQL(
            INSERT INTO loans (
                member_id, book_copy_id, borrowed_at, due_at, notes
            ) VALUES (
                :member_id, :book_copy_id, :borrowed_at, :due_at, :notes
            )
        )SQL");
        if (!insert) {
            return RepoSql::sqlFailure(insert.error().detail);
        }
        if (!insert->bind(":member_id", input.memberId)
            || !insert->bind(":book_copy_id", input.bookCopyId)
            || !insert->bind(":borrowed_at", borrowedAt) || !insert->bind(":due_at", dueAt)
            || !insert->bindOptional(":notes", nullableText(input.notes)) || !insert->exec()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        newId = m_session.lastInsertRowId();
        return Status::ok();
    });
    if (!work) {
        return Result<std::int64_t>::fail(work.error().kind, work.error().key, work.error().detail);
    }
    return Result<std::int64_t>::ok(newId);
}

Status CirculationRepository::returnLoan(const std::int64_t loanId,
                                         const std::string& returnedAt,
                                         const std::string& notes)
{
    const auto existing = getLoan(loanId);
    if (!existing) {
        return asStatus(existing);
    }
    if (!existing->returnedAt.empty()) {
        return RepoSql::validation("error.loan.alreadyReturned");
    }

    const std::string returnDate =
        trim(returnedAt).empty() ? Clock::today().toIso() : trim(returnedAt);

    if (const auto dates =
            LoanPolicy::validateReturnDate(returnDate, existing->borrowedAt, Clock::today());
        !dates) {
        return Status::fail(VLMS::ErrorKind::Validation, dates.key);
    }

    auto update = m_session.prepare(R"SQL(
        UPDATE loans SET
            returned_at = :returned_at,
            notes = COALESCE(NULLIF(TRIM(:notes), ''), notes)
        WHERE id = :id AND returned_at IS NULL
    )SQL");
    if (!update) {
        return RepoSql::sqlFailure(update.error().detail);
    }
    if (!update->bind(":returned_at", returnDate) || !update->bind(":notes", notes)
        || !update->bind(":id", loanId) || !update->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (update->changes() <= 0) {
        return RepoSql::validation("error.loan.cannotReturn");
    }
    return Status::ok();
}

Status CirculationRepository::extendLoan(const std::int64_t loanId, const std::string& dueAt)
{
    const auto existing = getLoan(loanId);
    if (!existing) {
        return asStatus(existing);
    }
    if (!existing->returnedAt.empty()) {
        return RepoSql::validation("error.loan.alreadyReturned");
    }

    const std::string newDueAt = trim(dueAt);
    if (newDueAt.empty()) {
        return RepoSql::validation("error.loan.dueRequired");
    }

    if (const auto dates = LoanPolicy::validateExtension(newDueAt, existing->dueAt, Clock::today());
        !dates) {
        return Status::fail(VLMS::ErrorKind::Validation, dates.key);
    }

    auto update = m_session.prepare(R"SQL(
        UPDATE loans SET due_at = :due_at
        WHERE id = :id AND returned_at IS NULL
    )SQL");
    if (!update) {
        return RepoSql::sqlFailure(update.error().detail);
    }
    if (!update->bind(":due_at", newDueAt) || !update->bind(":id", loanId) || !update->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (update->changes() <= 0) {
        return RepoSql::validation("error.loan.cannotExtend");
    }
    return Status::ok();
}

Status CirculationRepository::canArchiveLoan(const std::int64_t loanId) const
{
    auto read = m_session.prepare(
        "SELECT returned_at IS NULL, archived_at IS NOT NULL FROM loans WHERE id = :id");
    if (!read) {
        return RepoSql::sqlFailure(read.error().detail);
    }
    if (!read->bind(":id", loanId)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (!read->next()) {
        if (!read->ok()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return RepoSql::notFound("error.loan.notFound");
    }
    // Only history is archived: a book still out is Circulation's business.
    if (read->integer(0) != 0) {
        return RepoSql::validation("error.loan.archiveOpen");
    }
    if (read->integer(1) != 0) {
        return RepoSql::notFound("error.loan.notFound");
    }
    return Status::ok();
}

Status CirculationRepository::archiveLoan(const std::int64_t loanId)
{
    return m_session.transaction([&] {
    if (const Status gate = canArchiveLoan(loanId); !gate) {
        return gate;
    }

    auto archive = m_session.prepare("UPDATE loans SET archived_at = :stamp WHERE id = :id");
    if (!archive) {
        return RepoSql::sqlFailure(archive.error().detail);
    }
    if (!archive->bind(":stamp", Clock::nowIso()) || !archive->bind(":id", loanId)
        || !archive->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    return Status::ok();
    });
}

Status CirculationRepository::restoreLoan(const std::int64_t loanId)
{
    auto restore = m_session.prepare(
        "UPDATE loans SET archived_at = NULL WHERE id = :id AND archived_at IS NOT NULL");
    if (!restore) {
        return RepoSql::sqlFailure(restore.error().detail);
    }
    if (!restore->bind(":id", loanId) || !restore->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (restore->changes() > 0) {
        return Status::ok();
    }

    auto exists = m_session.prepare("SELECT 1 FROM loans WHERE id = :id");
    if (!exists) {
        return RepoSql::sqlFailure(exists.error().detail);
    }
    if (!exists->bind(":id", loanId)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (exists->next()) {
        return RepoSql::validation("error.loan.notArchived");
    }
    return RepoSql::notFound("error.loan.notFound");
}

Status CirculationRepository::canPurgeLoan(const std::int64_t loanId) const
{
    auto read = m_session.prepare("SELECT archived_at IS NOT NULL FROM loans WHERE id = :id");
    if (!read) {
        return RepoSql::sqlFailure(read.error().detail);
    }
    if (!read->bind(":id", loanId)) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (!read->next()) {
        if (!read->ok()) {
            return RepoSql::sqlFailure(m_session.lastError());
        }
        return RepoSql::notFound("error.loan.notFound");
    }
    // Permanent removal belongs to the Archive alone: a live loan is deleted
    // from the Circulation page only in the sense of being put down first.
    if (read->integer(0) == 0) {
        return RepoSql::validation("error.loan.notArchived");
    }
    return Status::ok();
}

Status CirculationRepository::purgeLoan(const std::int64_t loanId)
{
    return m_session.transaction([&] {
    if (const Status gate = canPurgeLoan(loanId); !gate) {
        return gate;
    }

    auto remove = m_session.prepare("DELETE FROM loans WHERE id = :id");
    if (!remove) {
        return RepoSql::sqlFailure(remove.error().detail);
    }
    if (!remove->bind(":id", loanId) || !remove->exec()) {
        return RepoSql::sqlFailure(m_session.lastError());
    }
    if (remove->changes() <= 0) {
        return RepoSql::notFound("error.loan.notFound");
    }
    return Status::ok();
    });
}

Result<std::vector<LoanMemberOption>>
CirculationRepository::listBorrowableMembers(const std::string& search) const
{
    std::string sql = "SELECT id, membership_number, first_name, last_name, "
        + MemberSql::statusExpression("") + " FROM members WHERE " + MemberSql::isActive("")
        + " AND archived_at IS NULL ";

    const std::string trimmedSearch = trim(search);
    if (!trimmedSearch.empty()) {
        sql += " AND (membership_number LIKE :search ESCAPE '\\' "
               "OR first_name LIKE :search ESCAPE '\\' "
               "OR last_name LIKE :search ESCAPE '\\' "
               "OR phone LIKE :search ESCAPE '\\') ";
    }
    sql += " ORDER BY last_name COLLATE NOCASE, first_name COLLATE NOCASE LIMIT 100";

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<std::vector<LoanMemberOption>>(q.error().detail);
    }
    if (!q->bind(LoanSql::todayPlaceholder(), Clock::todayIso())) {
        return RepoSql::sqlResult<std::vector<LoanMemberOption>>(m_session.lastError());
    }
    if (!trimmedSearch.empty()
        && !q->bind(":search", "%" + escapeLike(trimmedSearch) + "%")) {
        return RepoSql::sqlResult<std::vector<LoanMemberOption>>(m_session.lastError());
    }

    std::vector<LoanMemberOption> members;
    while (q->next()) {
        LoanMemberOption member;
        member.id = q->int64(0);
        member.membershipNumber = q->text(1);
        member.firstName = q->text(2);
        member.lastName = q->text(3);
        member.status = q->text(4);
        members.push_back(std::move(member));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<LoanMemberOption>>(m_session.lastError());
    }
    return Result<std::vector<LoanMemberOption>>::ok(std::move(members));
}

Result<std::vector<LoanCopyOption>>
CirculationRepository::listAvailableCopies(const std::string& search,
                                           const std::int64_t bookId) const
{
    std::string sql = R"SQL(
        SELECT
            bc.id,
            bc.global_copy_id,
            bc.local_id,
            b.title,
            COALESCE(a.name, '') AS author_name
        FROM book_copies bc
        INNER JOIN books b ON b.id = bc.book_id
        LEFT JOIN authors a ON a.id = b.author_id
        LEFT JOIN loans active_loan
            ON active_loan.book_copy_id = bc.id
           AND active_loan.returned_at IS NULL
        WHERE active_loan.id IS NULL
          AND LENGTH(TRIM(b.title)) > 0
          AND bc.archived_at IS NULL
          AND b.archived_at IS NULL
    )SQL";

    // One book's free copies: the query already excludes copies on loan and
    // archived ones, so scoping it is a single clause over the join it makes.
    if (bookId > 0) {
        sql += " AND bc.book_id = :book_id ";
    }

    const std::string trimmedSearch = trim(search);
    if (!trimmedSearch.empty()) {
        sql += " AND (b.title LIKE :search ESCAPE '\\' "
               "OR bc.local_id LIKE :search ESCAPE '\\' "
               "OR bc.global_copy_id LIKE :search ESCAPE '\\' "
               "OR COALESCE(bc.central_id, '') LIKE :search ESCAPE '\\' "
               "OR COALESCE(a.name, '') LIKE :search ESCAPE '\\' "
               "OR COALESCE(b.isbn, '') LIKE :search ESCAPE '\\') ";
    }
    sql += " ORDER BY b.title COLLATE NOCASE, bc.local_id COLLATE NOCASE LIMIT 100";

    auto q = m_session.prepare(sql);
    if (!q) {
        return RepoSql::sqlResult<std::vector<LoanCopyOption>>(q.error().detail);
    }
    if (!trimmedSearch.empty()
        && !q->bind(":search", "%" + escapeLike(trimmedSearch) + "%")) {
        return RepoSql::sqlResult<std::vector<LoanCopyOption>>(m_session.lastError());
    }
    if (bookId > 0 && !q->bind(":book_id", bookId)) {
        return RepoSql::sqlResult<std::vector<LoanCopyOption>>(m_session.lastError());
    }

    std::vector<LoanCopyOption> copies;
    while (q->next()) {
        LoanCopyOption copy;
        copy.id = q->int64(0);
        copy.globalCopyId = q->text(1);
        copy.localId = q->text(2);
        copy.bookTitle = q->text(3);
        copy.authorName = q->text(4);
        copies.push_back(std::move(copy));
    }
    if (!q->ok()) {
        return RepoSql::sqlResult<std::vector<LoanCopyOption>>(m_session.lastError());
    }
    return Result<std::vector<LoanCopyOption>>::ok(std::move(copies));
}
