#include <VLMS/Repositories/MetricsRepository.h>

#include <VLMS/Core/Clock.h>

#include "LoanSql.h"
#include "MemberSql.h"
#include "RepoSql.h"
#include <VLMS/Database/SqliteSession.h>

using VLMS::Clock;
using VLMS::Date;
using VLMS::Result;
namespace LoanSql = VLMS::LoanSql;
namespace RepoSql = VLMS::RepoSql;

MetricsRepository::MetricsRepository(VLMS::SqliteSession& session)
    : m_session(session)
{
}

Result<int> MetricsRepository::scalarCount(const std::string& sql,
                                           const std::map<std::string, std::string>& binds) const
{
    auto query = m_session.prepare(sql);
    if (!query) {
        return RepoSql::sqlResult<int>(query.error().detail);
    }
    for (const auto& [name, value] : binds) {
        if (!query->bind(name, value)) {
            return RepoSql::sqlResult<int>(m_session.lastError());
        }
    }
    if (!query->next()) {
        if (!query->ok()) {
            return RepoSql::sqlResult<int>(m_session.lastError());
        }
        return Result<int>::ok(0);
    }
    return Result<int>::ok(query->integer(0));
}

MetricsRepository::DateRange MetricsRepository::rangeFor(const Window window)
{
    const Date today = Clock::today();

    switch (window) {
    case Window::Today:
        return {today, today};
    case Window::ThisWeek:
        return {today.addDays(-6), today};
    case Window::ThisMonth:
        return {Date(today.year(), today.month(), 1), today};
    }

    return {today, today};
}

Result<MetricsPeriodCounts> MetricsRepository::fetchPeriodCounts(const Window window) const
{
    MetricsPeriodCounts counts;
    const DateRange range = rangeFor(window);

    auto query = m_session.prepare(R"SQL(
        SELECT
            (SELECT COUNT(*)
             FROM loans
             WHERE date(borrowed_at) BETWEEN :start AND :end) AS checkouts,
            (SELECT COUNT(*)
             FROM loans
             WHERE returned_at IS NOT NULL
               AND date(returned_at) BETWEEN :start AND :end) AS returns,
            (SELECT COUNT(*)
             FROM members
             WHERE date(registered_at) BETWEEN :start AND :end) AS new_members
    )SQL");
    if (!query) {
        return RepoSql::sqlResult<MetricsPeriodCounts>(query.error().detail);
    }
    if (!query->bind(":start", range.start.toIso())
        || !query->bind(":end", range.endInclusive.toIso())) {
        return RepoSql::sqlResult<MetricsPeriodCounts>(m_session.lastError());
    }

    if (query->next()) {
        counts.checkouts = query->integer(0);
        counts.returns = query->integer(1);
        counts.newMembers = query->integer(2);
    } else if (!query->ok()) {
        return RepoSql::sqlResult<MetricsPeriodCounts>(m_session.lastError());
    }
    return Result<MetricsPeriodCounts>::ok(counts);
}

Result<LibraryMetrics> MetricsRepository::fetchMetrics() const
{
    LibraryMetrics metrics;

    const struct {
        int* dest;
        std::string sql;
        std::map<std::string, std::string> binds;
    } counts[] = {
        {&metrics.bookTitles,
         "SELECT COUNT(*) FROM books WHERE LENGTH(TRIM(title)) > 0 AND archived_at IS NULL",
         {}},
        {&metrics.totalCopies, "SELECT COUNT(*) FROM book_copies WHERE archived_at IS NULL", {}},
        {&metrics.availableCopies,
         R"SQL(
            SELECT COUNT(*)
            FROM book_copies bc
            WHERE bc.archived_at IS NULL
              AND NOT EXISTS (
                SELECT 1 FROM loans l
                WHERE l.book_copy_id = bc.id AND l.returned_at IS NULL
            )
         )SQL",
         {}},
        {&metrics.totalMembers, "SELECT COUNT(*) FROM members", {}},
        {&metrics.membersActive,
         "SELECT COUNT(*) FROM members m WHERE " + VLMS::MemberSql::isActive("m."),
         {{LoanSql::todayPlaceholder(), Clock::todayIso()}}},
        {&metrics.membersNonActive,
         "SELECT COUNT(*) FROM members m WHERE NOT " + VLMS::MemberSql::isActive("m."),
         {{LoanSql::todayPlaceholder(), Clock::todayIso()}}},
        // Same three states as the Circulation filter: Open stops at the due date.
        {&metrics.openLoans,
         std::string("SELECT COUNT(*) FROM loans WHERE returned_at IS NULL AND NOT ")
             + LoanSql::isOverdue(),
         {{LoanSql::todayPlaceholder(), Clock::todayIso()}}},
        {&metrics.overdueLoans,
         std::string("SELECT COUNT(*) FROM loans WHERE ") + LoanSql::isOverdue(),
         {{LoanSql::todayPlaceholder(), Clock::todayIso()}}},
        {&metrics.returnedLoans, "SELECT COUNT(*) FROM loans WHERE returned_at IS NOT NULL", {}},
    };

    for (const auto& item : counts) {
        const auto value = scalarCount(item.sql, item.binds);
        if (!value) {
            return Result<LibraryMetrics>::fail(value.error().kind, value.error().key,
                                                value.error().detail);
        }
        *item.dest = value.value();
    }

    const auto today = fetchPeriodCounts(Window::Today);
    if (!today) {
        return Result<LibraryMetrics>::fail(today.error().kind, today.error().key,
                                            today.error().detail);
    }
    metrics.today = today.value();

    const auto week = fetchPeriodCounts(Window::ThisWeek);
    if (!week) {
        return Result<LibraryMetrics>::fail(week.error().kind, week.error().key, week.error().detail);
    }
    metrics.thisWeek = week.value();

    const auto month = fetchPeriodCounts(Window::ThisMonth);
    if (!month) {
        return Result<LibraryMetrics>::fail(month.error().kind, month.error().key,
                                            month.error().detail);
    }
    metrics.thisMonth = month.value();

    auto categoryQuery = m_session.prepare(R"SQL(
        SELECT
            COALESCE(NULLIF(TRIM(c.label), ''), c.code) AS label,
            COUNT(b.id) AS book_count
        FROM categories c
        INNER JOIN books b ON b.category_id = c.id
        WHERE LENGTH(TRIM(b.title)) > 0 AND b.archived_at IS NULL
        GROUP BY c.id
        HAVING COUNT(b.id) > 0
        ORDER BY COUNT(b.id) DESC, c.code COLLATE NOCASE
        LIMIT 8
    )SQL");
    if (!categoryQuery) {
        return RepoSql::sqlResult<LibraryMetrics>(categoryQuery.error().detail);
    }

    while (categoryQuery->next()) {
        MetricsCategoryCount row;
        row.label = categoryQuery->text(0);
        row.bookCount = categoryQuery->integer(1);
        metrics.topCategories.push_back(std::move(row));
    }
    if (!categoryQuery->ok()) {
        return RepoSql::sqlResult<LibraryMetrics>(m_session.lastError());
    }

    return Result<LibraryMetrics>::ok(metrics);
}
