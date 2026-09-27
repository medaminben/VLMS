#include <VLMS/Core/Date.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <tuple>

namespace VLMS {

namespace {

[[nodiscard]] bool civilOk(int year, int month, int day)
{
    if (year < 1 || month < 1 || month > 12 || day < 1) {
        return false;
    }
    const std::chrono::year_month_day ymd{std::chrono::year{year},
                                          std::chrono::month{static_cast<unsigned>(month)},
                                          std::chrono::day{static_cast<unsigned>(day)}};
    return ymd.ok();
}

[[nodiscard]] std::tm localNow()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return local;
}

}  // namespace

Date::Date(int year, int month, int day)
    : m_year(year), m_month(month), m_day(day), m_valid(civilOk(year, month, day))
{
    if (!m_valid) {
        m_year = 0;
        m_month = 0;
        m_day = 0;
    }
}

Date Date::fromIso(std::string_view text)
{
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        return {};
    }
    char buffer[11];
    std::memcpy(buffer, text.data(), 10);
    buffer[10] = '\0';
    int year = 0;
    int month = 0;
    int day = 0;
    if (std::sscanf(buffer, "%4d-%2d-%2d", &year, &month, &day) != 3) {
        return {};
    }
    return Date(year, month, day);
}

Date Date::todayLocal()
{
    const std::tm local = localNow();
    return Date(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

std::string Date::toIso() const
{
    if (!m_valid) {
        return {};
    }
    char buffer[11];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", m_year, m_month, m_day);
    return buffer;
}

Date Date::addDays(int days) const
{
    if (!m_valid) {
        return {};
    }
    const std::chrono::year_month_day ymd{std::chrono::year{m_year},
                                          std::chrono::month{static_cast<unsigned>(m_month)},
                                          std::chrono::day{static_cast<unsigned>(m_day)}};
    const auto shifted = std::chrono::year_month_day{std::chrono::sys_days{ymd}
                                                     + std::chrono::days{days}};
    return Date(static_cast<int>(shifted.year()),
                static_cast<int>(static_cast<unsigned>(shifted.month())),
                static_cast<int>(static_cast<unsigned>(shifted.day())));
}

Date Date::addYears(const int years) const
{
    if (!m_valid) {
        return {};
    }
    const Date same(m_year + years, m_month, m_day);
    if (same.isValid()) {
        return same;
    }
    return Date(m_year + years, 3, 1);
}

bool operator<(const Date& left, const Date& right)
{
    if (!left.isValid() || !right.isValid()) {
        return false;
    }
    return std::tie(left.m_year, left.m_month, left.m_day)
        < std::tie(right.m_year, right.m_month, right.m_day);
}

bool operator>(const Date& left, const Date& right)
{
    return right < left;
}

bool operator<=(const Date& left, const Date& right)
{
    return left.isValid() && right.isValid() && !(right < left);
}

bool operator>=(const Date& left, const Date& right)
{
    return right <= left;
}

DateTime::DateTime(Date date, int hour, int minute, int second)
    : m_date(std::move(date)), m_hour(hour), m_minute(minute), m_second(second)
{
    if (!m_date.isValid() || hour < 0 || hour > 23 || minute < 0 || minute > 59
        || second < 0 || second > 59) {
        m_date = {};
        m_hour = 0;
        m_minute = 0;
        m_second = 0;
    }
}

DateTime DateTime::nowLocal()
{
    const std::tm local = localNow();
    return DateTime(Date(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday),
                    local.tm_hour,
                    local.tm_min,
                    local.tm_sec);
}

bool DateTime::isValid() const
{
    return m_date.isValid();
}

std::string DateTime::toIso() const
{
    if (!isValid()) {
        return {};
    }
    char buffer[20];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%04d-%02d-%02d %02d:%02d:%02d",
                  m_date.year(),
                  m_date.month(),
                  m_date.day(),
                  m_hour,
                  m_minute,
                  m_second);
    return buffer;
}

}  // namespace VLMS
