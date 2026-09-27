#pragma once

#include <VLMS/Core/Date.h>
#include <VLMS/Core/MetricsTypes.h>
#include <VLMS/Core/Result.h>

#include <map>
#include <string>

namespace VLMS {
class SqliteSession;
}

class MetricsRepository {
public:
    explicit MetricsRepository(VLMS::SqliteSession& session);

    [[nodiscard]] VLMS::Result<LibraryMetrics> fetchMetrics() const;

private:
    enum class Window { Today, ThisWeek, ThisMonth };

    struct DateRange {
        VLMS::Date start;
        VLMS::Date endInclusive;
    };

    [[nodiscard]] static DateRange rangeFor(Window window);
    [[nodiscard]] VLMS::Result<int> scalarCount(
        const std::string& sql,
        const std::map<std::string, std::string>& binds = {}) const;
    [[nodiscard]] VLMS::Result<MetricsPeriodCounts> fetchPeriodCounts(Window window) const;

    VLMS::SqliteSession& m_session;
};
