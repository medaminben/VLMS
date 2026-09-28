#pragma once

#include "TestDatabase.h"
#include "TestEnv.h"

#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Repositories/MemberTypes.h>
#include <VLMS/Core/Result.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VLMS::Test {

struct MemberSeed {
    std::string membershipNumber;
    std::string firstName = "Amina";
    std::string lastName = "Ben Salah";
    std::string sex = "female";
    std::string dateOfBirth = "1990-05-12";
    std::string email;
    std::string phone;
    std::string address;
    std::string city;
    std::string status = "active";
    std::string notes;
    std::string occupation;
    std::string fullName;

    [[nodiscard]] MemberInput toInput() const;
};

struct BookSeed {
    std::string title = "Untitled Work";
    std::string authorName = "Ibn Khaldun";
    std::string publisherName = "Dar al-Kutub";
    std::int64_t categoryId = 0;
    std::string isbn;
    std::string publicationDate = "2014";
    std::string placeOfPublication;
    std::string pages;
    std::string dimensions;
    std::string language = "ar";
    std::string description;
    int initialCopyCount = 1;

    [[nodiscard]] BookInput toInput() const;
};

[[nodiscard]] MemberSeed uniqueMemberSeed(int index);
[[nodiscard]] BookSeed uniqueBookSeed(int index);

std::int64_t seedMember(TestDatabase& db, const MemberSeed& seed);
std::int64_t seedBook(TestDatabase& db, const BookSeed& seed);
std::int64_t seedCategory(TestDatabase& db, std::string_view code, std::string_view label);

[[nodiscard]] std::vector<std::int64_t> copyIdsOf(const TestDatabase& db, std::int64_t bookId);

std::int64_t rawInsertLoan(const TestDatabase& db,
                           std::int64_t memberId,
                           std::int64_t bookCopyId,
                           const std::string& borrowedAt,
                           const std::string& dueAt,
                           const std::string& returnedAt = {});

bool degradeLoansToPreV1Shape(const TestDatabase& db);

bool rawSetRegisteredAt(const TestDatabase& db, std::int64_t memberId, const std::string& value);
bool rawSetPublicationDate(const TestDatabase& db, std::int64_t bookId, const std::string& value);
bool rawSetCopyLocalId(const TestDatabase& db, std::int64_t copyId, const std::string& value);

struct HostilePayload {
    const char* id;
    std::string value;
    bool isLegitimateText = false;
};

[[nodiscard]] std::vector<HostilePayload> hostilePayloads();

bool schemaIsIntact(const TestDatabase& db,
                    const std::vector<std::string>& expectedTables,
                    std::string* whatChanged);

}  // namespace VLMS::Test
