#include <VLMS/Core/Clock.h>

#include <cstdio>

namespace VLMS {

namespace {

std::optional<DateTime> g_fixed;

}  // namespace

DateTime Clock::now()
{
    return g_fixed.value_or(DateTime::nowLocal());
}

Date Clock::today()
{
    return now().date();
}

std::string Clock::todayIso()
{
    return today().toIso();
}

std::string Clock::nowIso()
{
    return now().toIso();
}

void Clock::setFixedDate(const Date& date)
{
    if (!date.isValid()) {
        std::fprintf(stderr, "Clock::setFixedDate ignored an invalid date; clock unchanged.\n");
        return;
    }
    g_fixed = DateTime(date, 12, 0, 0);
}

void Clock::setFixedDateTime(const DateTime& dateTime)
{
    if (!dateTime.isValid()) {
        std::fprintf(stderr, "Clock::setFixedDateTime ignored an invalid datetime; clock unchanged.\n");
        return;
    }
    g_fixed = dateTime;
}

void Clock::reset()
{
    g_fixed.reset();
}

bool Clock::isOverridden()
{
    return g_fixed.has_value();
}

std::optional<DateTime> Clock::fixedDateTime()
{
    return g_fixed;
}

ScopedClock::ScopedClock(const Date& date)
    : m_previous(Clock::fixedDateTime())
{
    Clock::setFixedDate(date);
}

ScopedClock::ScopedClock(const DateTime& dateTime)
    : m_previous(Clock::fixedDateTime())
{
    Clock::setFixedDateTime(dateTime);
}

ScopedClock::~ScopedClock()
{
    if (m_previous.has_value()) {
        Clock::setFixedDateTime(*m_previous);
    } else {
        Clock::reset();
    }
}

}  // namespace VLMS
