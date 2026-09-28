#pragma once

#include <string>
#include <string_view>

namespace VLMS {

/**
 * A local calendar date, or an invalid sentinel.
 *
 * Invalid dates do not participate in ordering: callers must check isValid()
 * before comparing. An unreadable input must not sort below every real day.
 */
class Date {
public:
    Date() = default;
    Date(int year, int month, int day);

    [[nodiscard]] static Date fromIso(std::string_view text);
    [[nodiscard]] static Date todayLocal();

    [[nodiscard]] bool isValid() const { return m_valid; }
    [[nodiscard]] int year() const { return m_year; }
    [[nodiscard]] int month() const { return m_month; }
    [[nodiscard]] int day() const { return m_day; }

    [[nodiscard]] std::string toIso() const;
    [[nodiscard]] Date addDays(int days) const;
    /// SQLite's '+N years': the same month and day, except 29 February in a
    /// year without one, which rolls to 1 March.
    [[nodiscard]] Date addYears(int years) const;

    [[nodiscard]] friend bool operator==(const Date&, const Date&) = default;
    [[nodiscard]] friend bool operator!=(const Date&, const Date&) = default;
    friend bool operator<(const Date& left, const Date& right);
    friend bool operator>(const Date& left, const Date& right);
    friend bool operator<=(const Date& left, const Date& right);
    friend bool operator>=(const Date& left, const Date& right);

private:
    int m_year = 0;
    int m_month = 0;
    int m_day = 0;
    bool m_valid = false;
};

/**
 * A local date and time, or invalid.
 *
 * toIso() uses SQLite's datetime() shape: 'yyyy-MM-dd HH:mm:ss' with a space,
 * never a 'T'.
 */
class DateTime {
public:
    DateTime() = default;
    DateTime(Date date, int hour, int minute, int second);

    [[nodiscard]] static DateTime nowLocal();

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] const Date& date() const { return m_date; }
    [[nodiscard]] int hour() const { return m_hour; }
    [[nodiscard]] int minute() const { return m_minute; }
    [[nodiscard]] int second() const { return m_second; }

    [[nodiscard]] std::string toIso() const;

    [[nodiscard]] friend bool operator==(const DateTime&, const DateTime&) = default;

private:
    Date m_date;
    int m_hour = 0;
    int m_minute = 0;
    int m_second = 0;
};

}  // namespace VLMS
