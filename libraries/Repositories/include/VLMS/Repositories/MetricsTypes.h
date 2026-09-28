#pragma once

#include <string>
#include <vector>

namespace VLMS::Repositories {

struct MetricsPeriodCounts {
    int checkouts = 0;
    int returns = 0;
    int newMembers = 0;
};

struct MetricsCategoryCount {
    std::string label;
    int bookCount = 0;
};

struct LibraryMetrics {
    int bookTitles = 0;
    int totalCopies = 0;
    int availableCopies = 0;
    int totalMembers = 0;
    int membersActive = 0;
    int membersNonActive = 0;
    int openLoans = 0;
    int overdueLoans = 0;
    int returnedLoans = 0;
    MetricsPeriodCounts today;
    MetricsPeriodCounts thisWeek;
    MetricsPeriodCounts thisMonth;
    std::vector<MetricsCategoryCount> topCategories;
};

}  // namespace VLMS::Repositories
