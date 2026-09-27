#include "TestEnv.h"

#include <VLMS/Core/Date.h>

#include <chrono>
#include <ctime>
#include <sstream>

#ifdef _WIN32
#    include <stdlib.h>
#else
#    include <cstdlib>
#endif

namespace VLMS::Test {

namespace {

void setEnv(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unsetEnv(const char* name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

}  // namespace

Date utcToday()
{
    const auto now = std::chrono::system_clock::now();
    const auto days = std::chrono::floor<std::chrono::days>(now);
    const std::chrono::year_month_day ymd{days};
    return Date(static_cast<int>(ymd.year()),
                static_cast<int>(static_cast<unsigned>(ymd.month())),
                static_cast<int>(static_cast<unsigned>(ymd.day())));
}

bool localDateDiffersFromUtcDate()
{
    return Date::todayLocal() != utcToday();
}

std::string timeZoneDescription()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    std::tm utc{};
#if defined(_WIN32)
    localtime_s(&local, &now);
    gmtime_s(&utc, &now);
#else
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
#endif
    const Date localDate = Date::todayLocal();
    const Date utcDate = utcToday();
    std::ostringstream out;
    const char* tz = std::getenv("TZ");
    out << (tz != nullptr ? tz : "local")
        << ", local date " << localDate.toIso()
        << ", UTC date " << utcDate.toIso()
        << ", offset hint " << local.tm_hour << "h vs UTC " << utc.tm_hour << "h";
    return out.str();
}

ScopedEnv::ScopedEnv(const char* name, const std::string& value)
    : m_name(name)
{
    const char* previous = std::getenv(name);
    if (previous != nullptr) {
        m_hadPrevious = true;
        m_previous = previous;
    }
    setEnv(name, value.c_str());
}

ScopedEnv::~ScopedEnv()
{
    if (m_hadPrevious) {
        setEnv(m_name.c_str(), m_previous.c_str());
    } else {
        unsetEnv(m_name.c_str());
    }
}

}  // namespace VLMS::Test
