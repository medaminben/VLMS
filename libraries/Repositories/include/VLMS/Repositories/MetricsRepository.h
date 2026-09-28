#pragma once

#include <VLMS/Core/Date.h>
#include <VLMS/Repositories/MetricsTypes.h>
#include <VLMS/Core/Result.h>

#include <map>
#include <string>

namespace VLMS::Database {
class SqliteSession;
}  // namespace VLMS::Database

namespace VLMS::Repositories {

class MetricsRepository {
public:
    explicit MetricsRepository(Database::SqliteSession& session);

    [[nodiscard]] Core::Result<LibraryMetrics> fetchMetrics() const;

private:
    enum class Window { Today, ThisWeek, ThisMonth };

    struct DateRange {
        Core::Date start;
        Core::Date endInclusive;
    };

    [[nodiscard]] static DateRange rangeFor(Window window);
    [[nodiscard]] Core::Result<int> scalarCount(
        const std::string& sql,
        const std::map<std::string, std::string>& binds = {}) const;
    [[nodiscard]] Core::Result<MetricsPeriodCounts> fetchPeriodCounts(Window window) const;

    Database::SqliteSession& m_session;
};

}  // namespace VLMS::Repositories
