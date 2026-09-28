#pragma once

#include <cstdint>
#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/MemberTypes.h>
#include <string>
#include <vector>

struct LoanRecord {
    std::int64_t id = 0;
    std::int64_t memberId = 0;
    std::string membershipNumber;
    std::string memberName;
    std::string memberStatus;
    std::int64_t bookCopyId = 0;
    std::string copyCode;
    std::string bookTitle;
    std::string authorName;
    std::string coverImagePath;
    std::string memberPhotoPath;
    std::string borrowedAt;
    std::string dueAt;
    std::string returnedAt;
    std::string notes;
    bool isOverdue = false;
    std::string archivedAt;
};

struct LoanInput {
    std::int64_t memberId = 0;
    std::int64_t bookCopyId = 0;
    std::string borrowedAt;
    std::string dueAt;
    std::string notes;
};

struct LoanQuery {
    std::string search;
    std::vector<std::string> filters;
    std::int64_t memberId = 0;
    std::int64_t bookId = 0;
    /// One copy's loans; 0 = every copy.
    std::int64_t copyId = 0;
    /// The borrower's sex, age group, city and status. Its inscriptionYears
    /// still filters by registration; the loan lists use loanYears instead.
    MemberFacets member;
    /// The year a loan was made (borrowed_at), as "2025".
    std::vector<std::string> loanYears;
    ArchiveScope archive = ArchiveScope::Live;
    int limit = 200;
    int offset = 0;
    std::string sortColumn;
    bool sortAscending = true;
};

struct LoanMemberOption {
    std::int64_t id = 0;
    std::string membershipNumber;
    std::string firstName;
    std::string lastName;
    std::string status;
};

struct LoanCopyOption {
    std::int64_t id = 0;
    std::string globalCopyId;
    std::string localId;
    std::string bookTitle;
    std::string authorName;
};

namespace LoanFilter {
inline constexpr auto kOpen = "open";
inline constexpr auto kOverdue = "overdue";
inline constexpr auto kReturned = "returned";
inline constexpr auto kAll = "all";
}  // namespace LoanFilter

namespace LoanSort {
inline constexpr auto kMember = "member";
inline constexpr auto kNumber = "number";
inline constexpr auto kTitle = "title";
inline constexpr auto kBorrowed = "borrowed";
inline constexpr auto kDue = "due";
inline constexpr auto kStatus = "status";
inline constexpr auto kReturned = "returned";
inline constexpr auto kArchivedAt = "archivedAt";
}  // namespace LoanSort
