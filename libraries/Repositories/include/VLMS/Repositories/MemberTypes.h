#pragma once

#include <cstdint>
#include <VLMS/Repositories/ArchiveTypes.h>
#include <string>
#include <vector>

namespace VLMS::Repositories {

struct MemberRecord {
    std::int64_t id = 0;
    std::string membershipNumber;
    std::string firstName;
    std::string lastName;
    std::string sex;
    std::string dateOfBirth;
    std::string email;
    std::string phone;
    std::string address;
    std::string city;
    std::string status;
    std::string photoPath;
    std::string idImagePath;
    std::string notes;
    std::string registeredAt;
    std::string updatedAt;
    std::string occupation;
    std::string ageGroup;
    std::string fullName;
    std::string archivedAt;
    /// Last active day, ISO. `status` is worked out from it on every read.
    std::string activeUntil;
    int activeLoanCount = 0;
    /// Every loan row naming this member, archived ones included. The Archive's
    /// gate on permanent removal, so the column that shows it must count the
    /// same rows -- an archived loan still blocks.
    int loanCount = 0;
};

struct MemberInput {
    std::string membershipNumber;
    std::string firstName;
    std::string lastName;
    std::string sex;
    std::string dateOfBirth;
    std::string email;
    std::string phone;
    std::string address;
    std::string city;
    std::string status = "active";
    std::string notes;
    std::string occupation;
    // No ageGroup: MemberRepository derives it from dateOfBirth (on create, and
    // again only when an edit changes the birth date).
    std::string fullName;
};

/// The Members page's filter lists, which the loan lists apply to the
/// borrower too. Each dimension is ANDed; an empty one does not filter.
struct MemberFacets {
    std::vector<std::string> statuses;
    std::vector<std::string> sexes;
    std::vector<std::string> inscriptionYears;
    std::vector<std::string> ageGroups;
    std::vector<std::string> cities;
};

struct MemberQuery {
    std::string search;
    std::vector<std::string> statuses;
    std::vector<std::string> sexes;
    std::vector<std::string> inscriptionYears;
    std::vector<std::string> ageGroups;
    std::vector<std::string> cities;
    ArchiveScope archive = ArchiveScope::Live;
    int limit = 200;
    int offset = 0;
    std::string sortColumn;
    bool sortAscending = true;
};

struct MemberWrite {
    MemberInput member;
    std::string photoSourcePath;
    std::string idImageSourcePath;
    /// Take the stored image off an existing member. Ignored for a new
    /// member, and ignored for a slot that also has a source path.
    bool clearPhoto = false;
    bool clearIdImage = false;
};

namespace MemberStatus {
inline constexpr auto kActive = "active";
inline constexpr auto kNonActive = "non_active";
}  // namespace MemberStatus

namespace MemberSex {
inline constexpr auto kMale = "male";
inline constexpr auto kFemale = "female";
}  // namespace MemberSex

namespace MemberAgeGroup {
inline constexpr auto kYouth = "youth";
inline constexpr auto kAdult = "adult";
}  // namespace MemberAgeGroup

namespace MemberSort {
inline constexpr auto kNumber = "number";
inline constexpr auto kName = "name";
inline constexpr auto kPhone = "phone";
inline constexpr auto kCity = "city";
inline constexpr auto kStatus = "status";
inline constexpr auto kLoans = "loans";
inline constexpr auto kAllLoans = "allLoans";
inline constexpr auto kArchivedAt = "archivedAt";
}  // namespace MemberSort

}  // namespace VLMS::Repositories
