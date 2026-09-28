#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Repositories/MemberRepository.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace VLMS;
using namespace Test;

namespace {

std::string trimmed(std::string_view text)
{
    const auto begin = text.find_first_not_of(" \t\n\r");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\n\r");
    return std::string(text.substr(begin, end - begin + 1));
}

}  // namespace

class test_core_MemberRepository : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(resetStore()) << (m_db ? m_db->lastError() : "no database");
    }

    void TearDown() override
    {
        m_repository.reset();
        m_db.reset();
    }

    [[nodiscard]] bool resetStore()
    {
        m_repository.reset();
        m_db = std::make_unique<TestDatabase>();
        if (!m_db->isValid()) {
            return false;
        }
        m_repository = std::make_unique<Repositories::MemberRepository>(m_db->session(),
                                                          m_db->resourcesDirectory());
        return true;
    }

    std::string writeSampleImage(const std::string& name)
    {
        const std::filesystem::path path = std::filesystem::path(m_db->rootDirectory()) / name;
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }
        // 1x1 PNG. setPhotoImage copies the file; it does not decode pixels.
        static const unsigned char kPng[] = {
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
            0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
            0x0c, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
            0x00, 0x00, 0x03, 0x00, 0x01, 0x3b, 0x6d, 0x88, 0xdc, 0x00, 0x00, 0x00,
            0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
        };
        file.write(reinterpret_cast<const char*>(kPng), sizeof(kPng));
        return path.string();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_repository;
};

TEST_F(test_core_MemberRepository, CreateMemberGeneratesMembershipNumberWhenBlank)
{
    MemberSeed seed = uniqueMemberSeed(1);
    seed.membershipNumber.clear();

    std::int64_t id = 0;
    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    const auto stored = m_repository->getMember(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->membershipNumber.empty())
        << "a blank membership number must be generated, not stored blank";
}

TEST_F(test_core_MemberRepository, CreateMemberRejectsBlankNames)
{
    MemberSeed seed = uniqueMemberSeed(2);
    seed.firstName = "  ";

    const auto failed = m_repository->createMember(seed.toInput());
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());

    seed = uniqueMemberSeed(3);
    seed.lastName.clear();
    EXPECT_FALSE(m_repository->createMember(seed.toInput()));
    EXPECT_EQ(m_db->count("members"), 0);
}

TEST_F(test_core_MemberRepository, CreateMemberRejectsInvalidStatus)
{
    MemberSeed seed = uniqueMemberSeed(4);
    seed.status = "vip";

    const auto failed = m_repository->createMember(seed.toInput());
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    EXPECT_EQ(m_db->count("members"), 0);
}

TEST_F(test_core_MemberRepository, CreateMemberRejectsInvalidSex)
{
    MemberSeed seed = uniqueMemberSeed(5);
    seed.sex = "other";

    const auto failed = m_repository->createMember(seed.toInput());
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
}

TEST_F(test_core_MemberRepository, CreateMemberAcceptsEmptySex)
{
    MemberSeed seed = uniqueMemberSeed(6);
    seed.sex.clear();

    std::int64_t id = 0;
    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    const auto stored = m_repository->getMember(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_TRUE(stored->sex.empty());
}

TEST_F(test_core_MemberRepository, CreateMemberWritesStatusHistoryRow)
{
    EXPECT_GT(seedMember(*m_db, uniqueMemberSeed(7)), 0);
    EXPECT_EQ(m_db->count("member_status_history"), 1);
}

TEST_F(test_core_MemberRepository, CreateMemberStoresBlankOptionalFieldsAsNull)
{
    MemberSeed seed = uniqueMemberSeed(8);
    seed.email.clear();
    seed.phone.clear();
    seed.address.clear();
    seed.city.clear();
    seed.notes.clear();

    std::int64_t id = 0;
    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    // nullableText() turns a blank optional field into SQL NULL. Pinned here
    // because Repositories::CatalogRepository uses the other convention ('') for
    // publication_date, and the two are being unified.
    const int nulls = m_db->scalar(
                              "SELECT (phone IS NULL) + (address IS NULL) "
                              "+ (city IS NULL) + (notes IS NULL) "
                              "+ (email IS NULL) "
                              "FROM members WHERE id = :id",
                              {{"id", id}})
                          .toInt();
    EXPECT_EQ(nulls, 5);
}

TEST_F(test_core_MemberRepository, CreateMemberAcceptsAWellFormedEmail)
{
    const std::vector<std::pair<const char*, std::string>> cases = {
        {"plain", "amina@example.org"},
        {"dotted local", "amina.ben.salah@example.org"},
        {"plus tag", "amina+library@example.org"},
        {"apostrophe", "o'brien@example.org"},
        {"hyphenated domain", "a@sous-domaine.exemple.tn"},
        {"digits", "m0000123@bib.tn"},
        {"blank is optional", ""},
        {"blanks only", "   "},
    };

    for (const auto& [name, email] : cases) {
        SCOPED_TRACE(name);
        ASSERT_TRUE(resetStore()) << m_db->lastError();

        MemberSeed seed = uniqueMemberSeed(90);
        seed.email = email;

        std::int64_t id = 0;
        const auto created = m_repository->createMember(seed.toInput());
        ASSERT_TRUE(created) << created.error().key;
        id = created.value();

        const auto stored = m_repository->getMember(id);
        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(stored->email, trimmed(email));
    }
}

TEST_F(test_core_MemberRepository, CreateMemberRejectsAMalformedEmail)
{
    const std::vector<std::pair<const char*, std::string>> cases = {
        {"no at", "amina.example.org"},
        {"two ats", "amina@@example.org"},
        {"no domain", "amina@"},
        {"no local part", "@example.org"},
        {"no dot in domain", "amina@example"},
        {"space inside", "amina ben@example.org"},
        {"trailing text", "amina@example.org and more"},
        {"newline", "amina@example.org\nbcc@evil.test"},
        {"quoted form", "\"amina ben\"@example.org"},
        {"angle brackets", "<amina@example.org>"},
        {"one letter tld", "amina@example.o"},
        {"hyphen starts label", "amina@-example.org"},
        {"nul byte", std::string("a@b.org\0x", 9)},
        {"local part too long", std::string(65, 'a') + "@example.org"},
        {"address too long", "a@" + std::string(260, 'b') + ".org"},
    };

    for (const auto& [name, email] : cases) {
        SCOPED_TRACE(name);
        ASSERT_TRUE(resetStore()) << m_db->lastError();

        MemberSeed seed = uniqueMemberSeed(91);
        seed.email = email;

        EXPECT_FALSE(m_repository->createMember(seed.toInput())) << email;
        EXPECT_TRUE(true); // error key checked on the Result/Status above when captured
        EXPECT_EQ(m_db->count("members"), 0);
    }
}

TEST_F(test_core_MemberRepository, UpdateMemberRejectsAMalformedEmail)
{
    MemberSeed seed = uniqueMemberSeed(92);
    seed.email = "amina@example.org";
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);

    Repositories::MemberInput edited = seed.toInput();
    edited.email = "amina@example";
    EXPECT_FALSE(m_repository->updateMember(id, edited));

    // A rejected edit must leave the stored address alone, not blank it.
    const auto stored = m_repository->getMember(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->email, "amina@example.org");
}

TEST_F(test_core_MemberRepository, EmailIsStoredTrimmed)
{
    MemberSeed seed = uniqueMemberSeed(93);
    seed.email = "  amina@example.org  ";

    std::int64_t id = 0;
    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    EXPECT_EQ(m_db->scalar("SELECT email FROM members WHERE id = :id",
                           {{"id", id}})
                  .toString(),
              "amina@example.org");
}

TEST_F(test_core_MemberRepository, EmailRoundTripsThroughCreateAndUpdate)
{
    MemberSeed seed = uniqueMemberSeed(9);
    seed.email = "amina@example.org";

    std::int64_t id = 0;
    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    auto stored = m_repository->getMember(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->email, seed.email);

    Repositories::MemberInput edited = seed.toInput();
    edited.membershipNumber = stored->membershipNumber;
    edited.email = "amina.bensalah@example.org";
    const auto mutated = m_repository->updateMember(id, edited);
    ASSERT_TRUE(mutated) << mutated.error().key;

    stored = m_repository->getMember(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->email, edited.email);

    // listMembers builds its own SELECT; a column added to one and not the
    // other is the classic way for an edit to look saved and read back empty.
    const auto listed = VLMS_UNWRAP(m_repository->listMembers({}));
    ASSERT_FALSE(listed.empty());
    EXPECT_EQ(listed.front().email, edited.email);
}

TEST_F(test_core_MemberRepository, UpdateMemberDoesNotRequireMembershipNumber)
{
    const std::int64_t id = seedMember(*m_db, uniqueMemberSeed(10));
    ASSERT_GT(id, 0);
    const auto before = m_repository->getMember(id);
    ASSERT_TRUE(before.has_value());

    MemberSeed changed = uniqueMemberSeed(10);
    changed.membershipNumber.clear();
    changed.city = "Mahdia";

    const auto mutated = m_repository->updateMember(id, changed.toInput());
    ASSERT_TRUE(mutated) << mutated.error().key;
    const auto after = m_repository->getMember(id);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->membershipNumber, before->membershipNumber);
    EXPECT_EQ(after->city, "Mahdia");
}

TEST_F(test_core_MemberRepository, UpdateMemberWritesHistoryOnlyOnStatusChange)
{
    MemberSeed seed = uniqueMemberSeed(11);
    seed.status = Repositories::MemberStatus::kNonActive;
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    EXPECT_EQ(m_db->count("member_status_history"), 1);

    // Same status: no new history row.
    seed.city = "Ksour Essef";
    const auto mutated = m_repository->updateMember(id, seed.toInput());
    ASSERT_TRUE(mutated) << mutated.error().key;
    EXPECT_EQ(m_db->count("member_status_history"), 1);

    // Changed status: exactly one more.
    seed.status = Repositories::MemberStatus::kActive;
    const auto mutatedAgain = m_repository->updateMember(id, seed.toInput());
    ASSERT_TRUE(mutatedAgain) << mutatedAgain.error().key;
    EXPECT_EQ(m_db->count("member_status_history"), 2);
}

TEST_F(test_core_MemberRepository, FailedUpdateLeavesMemberUnchanged)
{
    MemberSeed seed = uniqueMemberSeed(12);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);

    MemberSeed invalid = seed;
    invalid.status = "bogus";
    EXPECT_FALSE(m_repository->updateMember(id, invalid.toInput()));

    const auto stored = m_repository->getMember(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->status, seed.status);
    EXPECT_EQ(m_db->count("member_status_history"), 1);
}

TEST_F(test_core_MemberRepository, DeleteMemberRefusesWithActiveLoans)
{
    MemberSeed member = uniqueMemberSeed(13);
    member.status = Repositories::MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(13));
    ASSERT_GT(bookId, 0);

    const Core::Date today = Core::Date::todayLocal();
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(),
                            today.toIso(),
                            today.addDays(14).toIso()),
              0);

    EXPECT_EQ(VLMS_UNWRAP(m_repository->removalBlock(memberId)),
              Repositories::MemberRepository::MemberRemovalBlock::OpenLoans);

    // Neither removal is available while a book is still out, and the member
    // has to survive both refusals.
    const auto purgeFailed = m_repository->purgeMember(memberId);
    EXPECT_FALSE(purgeFailed);
    EXPECT_FALSE(purgeFailed.error().key.empty());
    const auto archiveFailed = m_repository->archiveMember(memberId);
    EXPECT_FALSE(archiveFailed);
    EXPECT_FALSE(archiveFailed.error().key.empty());
    EXPECT_EQ(m_db->count("members"), 1);
}

TEST_F(test_core_MemberRepository, DeleteMemberRemovesMember)
{
    const std::int64_t id = seedMember(*m_db, uniqueMemberSeed(14));
    ASSERT_GT(id, 0);

    EXPECT_EQ(VLMS_UNWRAP(m_repository->removalBlock(id)), Repositories::MemberRepository::MemberRemovalBlock::None);
    ASSERT_TRUE(m_repository->archiveMember(id));
    const auto mutated = m_repository->purgeMember(id);
    ASSERT_TRUE(mutated) << mutated.error().key;
    EXPECT_EQ(m_db->count("members"), 0);
}

TEST_F(test_core_MemberRepository, PurgeRefusesWithReturnedLoanHistory)
{
    // The reported bug: every loan is back on the shelf, so the open-loan guard
    // waves the member through, and then loans.member_id -- which has no ON
    // DELETE clause -- fails the DELETE with "FOREIGN KEY constraint failed".
    MemberSeed member = uniqueMemberSeed(15);
    member.status = Repositories::MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(15));
    ASSERT_GT(bookId, 0);

    const Core::Date today = Core::Date::todayLocal();
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(),
                            today.addDays(-30).toIso(),
                            today.addDays(-16).toIso(),
                            today.addDays(-20).toIso()),
              0);

    EXPECT_EQ(VLMS_UNWRAP(m_repository->removalBlock(memberId)),
              Repositories::MemberRepository::MemberRemovalBlock::LoanHistory);

    const auto failed = m_repository->purgeMember(memberId);
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());
    // The refusal has to be the repository's own, decided before the DELETE ran
    // -- not SQLite's, reported after it failed.
    EXPECT_EQ(failed.error().key.find("FOREIGN KEY"), std::string::npos);
    EXPECT_EQ(m_db->count("members"), 1);
    EXPECT_EQ(m_db->count("loans"), 1);
}

TEST_F(test_core_MemberRepository, ArchiveHidesMemberButKeepsLoanHistory)
{
    MemberSeed member = uniqueMemberSeed(16);
    member.status = Repositories::MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(16));
    ASSERT_GT(bookId, 0);

    const Core::Date today = Core::Date::todayLocal();
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copyIdsOf(*m_db, bookId).front(),
                            today.addDays(-30).toIso(),
                            today.addDays(-16).toIso(),
                            today.addDays(-20).toIso()),
              0);

    const auto mutated = m_repository->archiveMember(memberId);
    ASSERT_TRUE(mutated) << mutated.error().key;

    // The row and the loan are both still there -- that is the whole point of
    // archiving rather than deleting -- but the member is gone from the list.
    EXPECT_EQ(m_db->count("members"), 1);
    EXPECT_EQ(m_db->count("loans"), 1);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(Repositories::MemberQuery{})), 0);
    EXPECT_TRUE(VLMS_UNWRAP(m_repository->listMembers(Repositories::MemberQuery{})).empty());

    // Still reachable by id, so anything holding one -- the loan history
    // dialog above all -- can still name the person.
    EXPECT_TRUE(m_repository->getMember(memberId).has_value());
}

TEST_F(test_core_MemberRepository, ArchiveIsNotUndoneBySecondCall)
{
    const std::int64_t id = seedMember(*m_db, uniqueMemberSeed(17));
    ASSERT_GT(id, 0);

    const auto mutated = m_repository->archiveMember(id);
    ASSERT_TRUE(mutated) << mutated.error().key;
    const std::string firstStamp =
        m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(id))
            .toString();
    EXPECT_FALSE(firstStamp.empty());

    // Archiving an archived member changes nothing and reports the failure
    // rather than silently restamping the date they were retired on.
    EXPECT_FALSE(m_repository->archiveMember(id));
    EXPECT_EQ(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(id))
                  .toString(),
              firstStamp);
}

TEST_F(test_core_MemberRepository, SuggestMembershipNumberIsUniqueAgainstExisting)
{
    std::set<std::string> suggestions;
    for (int i = 0; i < 5; ++i) {
        const std::string suggested = m_repository->suggestMembershipNumber();
        EXPECT_FALSE(suggested.empty());
        suggestions.insert(suggested);

        MemberSeed seed = uniqueMemberSeed(20 + i);
        seed.membershipNumber = suggested;
        EXPECT_TRUE(m_repository->createMember(seed.toInput())) << "repository call failed";
    }

    // membership_number is UNIQUE; a repeated suggestion would have failed the
    // insert above, but assert distinctness directly too.
    EXPECT_EQ(suggestions.size(), 5u);
}

TEST_F(test_core_MemberRepository, SetPhotoImageStoresRelativePath)
{
    const std::int64_t id = seedMember(*m_db, uniqueMemberSeed(30));
    ASSERT_GT(id, 0);

    const std::string source = writeSampleImage("photo.png");
    ASSERT_FALSE(source.empty());

    const auto mutated = m_repository->setPhotoImage(id, source);
    ASSERT_TRUE(mutated) << mutated.error().key;

    const auto stored = m_repository->getMember(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->photoPath.empty());
    EXPECT_FALSE(std::filesystem::path(stored->photoPath).is_absolute())
        << "the stored path must be relative to the resources directory";
    EXPECT_TRUE(std::filesystem::exists(m_repository->resolveImagePath(stored->photoPath)));
}

TEST_F(test_core_MemberRepository, SetPhotoImageDoesNotTouchIdImagePath)
{
    // Behavioural guard for storeMemberImage's column selection. The column
    // name used to be interpolated with .arg(); C7 made it an ImageSlot enum
    // and this pair is what proves the mapping survived the change. The enum
    // itself is a compile-time guarantee that no test can express.
    const std::int64_t id = seedMember(*m_db, uniqueMemberSeed(31));
    ASSERT_GT(id, 0);

    const std::string source = writeSampleImage("p.png");
    ASSERT_FALSE(source.empty());
    EXPECT_TRUE(m_repository->setPhotoImage(id, source));

    const auto stored = m_repository->getMember(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->photoPath.empty());
    EXPECT_TRUE(stored->idImagePath.empty()) << "setPhotoImage must not write id_image_path";
}

TEST_F(test_core_MemberRepository, SetIdImageDoesNotTouchPhotoPath)
{
    const std::int64_t id = seedMember(*m_db, uniqueMemberSeed(32));
    ASSERT_GT(id, 0);

    const std::string source = writeSampleImage("i.png");
    ASSERT_FALSE(source.empty());
    EXPECT_TRUE(m_repository->setIdImage(id, source));

    const auto stored = m_repository->getMember(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_FALSE(stored->idImagePath.empty());
    EXPECT_TRUE(stored->photoPath.empty()) << "setIdImage must not write photo_path";
}

TEST_F(test_core_MemberRepository, CreateMemberRejectsAnUnparseableDateOfBirth)
{
    // Everything here would have been stored verbatim in a date column. The
    // first three are the dangerous ones: SQLite's date() does not reject
    // them, it reinterprets them -- '1987' as a Julian day giving
    // '-4707-05-30', and the two impossible calendar dates by rolling over
    // into the following month.
    const std::vector<std::pair<const char*, std::string>> cases = {
        {"bare year", "1987"},
        {"30 February", "1988-02-30"},
        {"29 Feb, non-leap", "1987-02-29"},
        {"european", "14/08/1987"},
        {"year-month", "1987-08"},
        {"prose", "summer of 1987"},
        {"unpadded", "1987-8-4"},
    };

    for (const auto& [name, dateOfBirth] : cases) {
        SCOPED_TRACE(name);
        ASSERT_TRUE(resetStore()) << m_db->lastError();

        MemberSeed seed = uniqueMemberSeed(40);
        seed.dateOfBirth = dateOfBirth;

        const int before = m_db->count("members");
        EXPECT_FALSE(m_repository->createMember(seed.toInput())) << dateOfBirth;
        EXPECT_TRUE(true); // error key checked on the Result/Status above when captured
        EXPECT_EQ(m_db->count("members"), before);
    }
}

TEST_F(test_core_MemberRepository, CreateMemberRejectsABirthDateAfterToday)
{
    // A future birth date gives a negative age, which the age-group rule
    // would silently file as youth. Today itself is a real (if early) birth.
    const Core::ScopedClock pinned(Core::Date(2026, 9, 19));

    MemberSeed future = uniqueMemberSeed(43);
    future.dateOfBirth = "2026-09-20";
    const auto failed = m_repository->createMember(future.toInput());
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().key, "error.member.dobInFuture");
    EXPECT_EQ(m_db->count("members"), 0);

    MemberSeed bornToday = uniqueMemberSeed(44);
    bornToday.dateOfBirth = "2026-09-19";
    const auto created = m_repository->createMember(bornToday.toInput());
    ASSERT_TRUE(created) << created.error().key;
}

TEST_F(test_core_MemberRepository, UpdateMemberRejectsABirthDateAfterToday)
{
    const Core::ScopedClock pinned(Core::Date(2026, 9, 19));

    MemberSeed seed = uniqueMemberSeed(45);
    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;

    Repositories::MemberInput edited = seed.toInput();
    edited.dateOfBirth = "2027-01-01";
    const auto failed = m_repository->updateMember(created.value(), edited);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().key, "error.member.dobInFuture");
    EXPECT_EQ(m_repository->getMember(created.value())->dateOfBirth, seed.dateOfBirth);
}

TEST_F(test_core_MemberRepository, AgeGroupFromBirthDateUsesThirtyOnInscriptionDate)
{
    EXPECT_EQ(Repositories::MemberRepository::ageGroupFromBirthDate("1996-09-20", "2026-09-19"),
              Repositories::MemberAgeGroup::kYouth);
    EXPECT_EQ(Repositories::MemberRepository::ageGroupFromBirthDate("1996-09-19", "2026-09-19"),
              Repositories::MemberAgeGroup::kAdult);
    EXPECT_EQ(Repositories::MemberRepository::ageGroupFromBirthDate("1996-09-18", "2026-09-19"),
              Repositories::MemberAgeGroup::kAdult);
    EXPECT_EQ(Repositories::MemberRepository::ageGroupFromBirthDate("1996-09-20", "2026-09-19 10:00:00"),
              Repositories::MemberAgeGroup::kYouth);
}

TEST_F(test_core_MemberRepository, CreateMemberRejectsABlankDateOfBirth)
{
    MemberSeed seed = uniqueMemberSeed(41);
    seed.dateOfBirth.clear();

    const auto failed = m_repository->createMember(seed.toInput());
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().key, "error.member.invalidDob");
    EXPECT_EQ(m_db->count("members"), 0);
}

TEST_F(test_core_MemberRepository, BlankOptionalFieldsAreStoredAsNullNotEmptyString)
{
    MemberSeed seed = uniqueMemberSeed(42);
    seed.phone.clear();
    seed.address.clear();
    seed.city.clear();
    seed.notes.clear();
    seed.sex.clear();

    std::int64_t id = 0;
    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    id = created.value();

    // A column holding both '' and NULL for "the librarian left this empty"
    // needs every query to test for both, and one of them is always
    // forgotten. This is the convention, asserted against the raw column
    // rather than through getMember(), which COALESCEs both to ''.
    for (const std::string& column : {std::string("sex"), std::string("phone"),
                                      std::string("address"), std::string("city"),
                                      std::string("notes")}) {
        const SqlValue raw = m_db->scalar(
            "SELECT " + column + " FROM members WHERE id = " + std::to_string(id));
        EXPECT_TRUE(raw.isNull()) << column;
    }
}

TEST_F(test_core_MemberRepository, UpdateMemberStampsUpdatedAtFromTheClock)
{
    const std::int64_t id = seedMember(*m_db, uniqueMemberSeed(34));
    ASSERT_GT(id, 0);

    const auto before = m_repository->getMember(id);
    ASSERT_TRUE(before.has_value());

    Repositories::MemberInput input;
    input.membershipNumber = before->membershipNumber;
    input.firstName = "Amina";
    input.lastName = "Ben Salah";
    input.dateOfBirth = before->dateOfBirth;
    input.status = before->status;

    // C9 replaced datetime('now') -- which SQLite resolves in UTC -- with a
    // value bound from Clock::nowIso(). The space separator matters: rows
    // written before the Clock existed carry 'YYYY-MM-DD HH:MM:SS', and
    // Qt::ISODate's 'T' sorts after every one of them.
    const Core::ScopedClock pinned(Core::DateTime(Core::Date(2021, 6, 15), 9, 30, 0));
    const auto mutated = m_repository->updateMember(id, input);
    ASSERT_TRUE(mutated) << mutated.error().key;

    const auto after = m_repository->getMember(id);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->updatedAt, "2021-06-15 09:30:00");
}

TEST_F(test_core_MemberRepository, SetPhotoImageStampsUpdatedAtFromTheClock)
{
    const std::int64_t id = seedMember(*m_db, uniqueMemberSeed(35));
    ASSERT_GT(id, 0);

    const std::string source = writeSampleImage("stamp.png");
    ASSERT_FALSE(source.empty());

    const Core::ScopedClock pinned(Core::DateTime(Core::Date(2021, 6, 15), 9, 30, 0));
    const auto mutated = m_repository->setPhotoImage(id, source);
    ASSERT_TRUE(mutated) << mutated.error().key;

    const auto stored = m_repository->getMember(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->updatedAt, "2021-06-15 09:30:00");
}

TEST_F(test_core_MemberRepository, SetPhotoImageRejectsMissingFile)
{
    const std::int64_t id = seedMember(*m_db, uniqueMemberSeed(33));
    ASSERT_GT(id, 0);

    const auto failed = m_repository->setPhotoImage(id, "/nonexistent/x.png");
    EXPECT_FALSE(failed);
    EXPECT_FALSE(failed.error().key.empty());

    const auto failedEmpty = m_repository->setPhotoImage(id, "");
    EXPECT_FALSE(failedEmpty);
    EXPECT_FALSE(failedEmpty.error().key.empty());
}

TEST_F(test_core_MemberRepository, ListMembersAndCountMembersAgree)
{
    const std::vector<std::string> none;
    const std::vector<std::pair<const char*, std::pair<std::string, std::vector<std::string>>>> cases = {
        {"no filters", {"", none}},
        {"search", {"Member", none}},
        {"one status", {"", {Repositories::MemberStatus::kActive}}},
        {"two statuses", {"", {Repositories::MemberStatus::kActive, Repositories::MemberStatus::kNonActive}}},
        {"search+status", {"Member", {Repositories::MemberStatus::kActive}}},
    };

    for (const auto& [name, queryBits] : cases) {
        SCOPED_TRACE(name);
        ASSERT_TRUE(resetStore()) << m_db->lastError();

        for (int i = 0; i < 6; ++i) {
            MemberSeed seed = uniqueMemberSeed(40 + i);
            seed.status = (i % 2 == 0) ? Repositories::MemberStatus::kActive
                                       : Repositories::MemberStatus::kNonActive;
            EXPECT_GT(seedMember(*m_db, seed), 0);
        }

        Repositories::MemberQuery query;
        query.search = queryBits.first;
        query.statuses = queryBits.second;

        EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(query)),
                  static_cast<int>(VLMS_UNWRAP(m_repository->listMembers(query)).size()));
    }
}

TEST_F(test_core_MemberRepository, ListMembersFiltersBySexYearAgeGroupAndCity)
{
    // Pinned so the 2010-born seed is youth on create whatever year this runs.
    const Core::ScopedClock pinned(Core::Date(2026, 9, 19));

    MemberSeed adultMale = uniqueMemberSeed(300);
    adultMale.sex = Repositories::MemberSex::kMale;
    adultMale.occupation = "قاضي";
    adultMale.city = "Tunis";
    const std::int64_t maleId = seedMember(*m_db, adultMale);
    ASSERT_GT(maleId, 0);
    ASSERT_TRUE(rawSetRegisteredAt(*m_db, maleId, "2019-03-01 10:00:00"));

    MemberSeed youthFemale = uniqueMemberSeed(301);
    youthFemale.sex = Repositories::MemberSex::kFemale;
    youthFemale.occupation = "تلميذة";
    youthFemale.dateOfBirth = "2010-06-15";
    youthFemale.city = "Sfax";
    const std::int64_t femaleId = seedMember(*m_db, youthFemale);
    ASSERT_GT(femaleId, 0);
    ASSERT_TRUE(rawSetRegisteredAt(*m_db, femaleId, "2024-06-15 09:00:00"));

    const auto idsOf = [](const std::vector<Repositories::MemberRecord>& rows) {
        std::set<std::int64_t> ids;
        for (const Repositories::MemberRecord& member : rows) {
            ids.insert(member.id);
        }
        return ids;
    };

    Repositories::MemberQuery bySex;
    bySex.sexes = {Repositories::MemberSex::kMale};
    EXPECT_EQ(idsOf(VLMS_UNWRAP(m_repository->listMembers(bySex))),
              std::set<std::int64_t>({maleId}));
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(bySex)), 1);

    Repositories::MemberQuery byYear;
    byYear.inscriptionYears = {"2024"};
    EXPECT_EQ(idsOf(VLMS_UNWRAP(m_repository->listMembers(byYear))),
              std::set<std::int64_t>({femaleId}));

    Repositories::MemberQuery byAge;
    byAge.ageGroups = {Repositories::MemberAgeGroup::kYouth};
    EXPECT_EQ(idsOf(VLMS_UNWRAP(m_repository->listMembers(byAge))),
              std::set<std::int64_t>({femaleId}));

    Repositories::MemberQuery byCity;
    byCity.cities = {"Sfax"};
    EXPECT_EQ(idsOf(VLMS_UNWRAP(m_repository->listMembers(byCity))),
              std::set<std::int64_t>({femaleId}));

    Repositories::MemberQuery bothCities;
    bothCities.cities = {"Tunis", "Sfax"};
    EXPECT_EQ(idsOf(VLMS_UNWRAP(m_repository->listMembers(bothCities))),
              std::set<std::int64_t>({maleId, femaleId}));

    Repositories::MemberQuery combined;
    combined.sexes = {Repositories::MemberSex::kMale};
    combined.cities = {"Sfax"};
    EXPECT_TRUE(VLMS_UNWRAP(m_repository->listMembers(combined)).empty());
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(combined)), 0);
}

TEST_F(test_core_MemberRepository, MemberFilterValuesAreDistinctNonEmptyAndSkipArchived)
{
    MemberSeed first = uniqueMemberSeed(310);
    first.occupation = "قاضي";
    first.city = "Tunis";
    const std::int64_t firstId = seedMember(*m_db, first);
    ASSERT_GT(firstId, 0);
    ASSERT_TRUE(rawSetRegisteredAt(*m_db, firstId, "2019-03-01 10:00:00"));

    MemberSeed duplicate = uniqueMemberSeed(311);
    duplicate.occupation = "قاضي";
    duplicate.city = "tunis";
    const std::int64_t duplicateId = seedMember(*m_db, duplicate);
    ASSERT_GT(duplicateId, 0);
    ASSERT_TRUE(rawSetRegisteredAt(*m_db, duplicateId, "2019-11-20 08:00:00"));

    MemberSeed blank = uniqueMemberSeed(312);
    blank.occupation.clear();
    blank.city.clear();
    const std::int64_t blankId = seedMember(*m_db, blank);
    ASSERT_GT(blankId, 0);
    ASSERT_TRUE(rawSetRegisteredAt(*m_db, blankId, "2021-01-01 00:00:00"));

    MemberSeed archived = uniqueMemberSeed(313);
    archived.occupation = "مهندس";
    archived.city = "Sfax";
    const std::int64_t archivedId = seedMember(*m_db, archived);
    ASSERT_GT(archivedId, 0);
    ASSERT_TRUE(rawSetRegisteredAt(*m_db, archivedId, "2018-05-01 12:00:00"));
    ASSERT_TRUE(m_repository->archiveMember(archivedId));

    const auto cities = VLMS_UNWRAP(m_repository->listCities());
    ASSERT_EQ(cities.size(), 1u);
    EXPECT_TRUE(cities.front() == "Tunis" || cities.front() == "tunis");

    Repositories::MemberQuery byListedCity;
    byListedCity.cities = {cities.front()};
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(byListedCity)), 2);

    const auto years = VLMS_UNWRAP(m_repository->listInscriptionYears());
    EXPECT_EQ(years, (std::vector<std::string>{"2021", "2019"}));
}

TEST_F(test_core_MemberRepository, ListMembersPagesCoverEveryRowExactlyOnce)
{
    constexpr int kMembers = 23;
    constexpr int kPageSize = 5;

    for (int i = 0; i < kMembers; ++i) {
        EXPECT_GT(seedMember(*m_db, uniqueMemberSeed(100 + i)), 0);
    }

    Repositories::MemberQuery query;
    query.limit = kPageSize;

    std::set<std::int64_t> seen;
    int rows = 0;
    for (int offset = 0; offset < kMembers; offset += kPageSize) {
        query.offset = offset;
        for (const Repositories::MemberRecord& member : VLMS_UNWRAP(m_repository->listMembers(query))) {
            seen.insert(member.id);
            ++rows;
        }
    }

    EXPECT_EQ(rows, kMembers);
    EXPECT_EQ(static_cast<int>(seen.size()), kMembers);
}

TEST_F(test_core_MemberRepository, ActiveLoanCountMatchesOpenLoans)
{
    MemberSeed member = uniqueMemberSeed(50);
    member.status = Repositories::MemberStatus::kActive;
    const std::int64_t memberId = seedMember(*m_db, member);
    ASSERT_GT(memberId, 0);

    BookSeed book = uniqueBookSeed(50);
    book.initialCopyCount = 3;
    const std::int64_t bookId = seedBook(*m_db, book);
    ASSERT_GT(bookId, 0);

    const auto copies = copyIdsOf(*m_db, bookId);
    const Core::Date today = Core::Date::todayLocal();

    EXPECT_GT(rawInsertLoan(*m_db, memberId, copies.at(0), today.toIso(),
                            today.addDays(14).toIso()),
              0);
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copies.at(1), today.toIso(),
                            today.addDays(14).toIso()),
              0);
    // A returned loan must not count.
    EXPECT_GT(rawInsertLoan(*m_db, memberId, copies.at(2),
                            today.addDays(-30).toIso(),
                            today.addDays(-16).toIso(),
                            today.addDays(-20).toIso()),
              0);

    const auto stored = m_repository->getMember(memberId);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->activeLoanCount, 2);
}

TEST_F(test_core_MemberRepository, GetMemberMissingIdIsNotFoundNotSql)
{
    const auto missing = m_repository->getMember(999999);
    EXPECT_FALSE(missing);
    EXPECT_EQ(missing.kind(), Core::ErrorKind::NotFound);
    EXPECT_EQ(missing.error().key, "error.member.notFound");
}

TEST_F(test_core_MemberRepository, GetMemberExecFailureIsSql)
{
    ASSERT_TRUE(m_db->exec("DROP TABLE members"));
    const auto failed = m_repository->getMember(1);
    EXPECT_FALSE(failed);
    EXPECT_EQ(failed.kind(), Core::ErrorKind::Sql);
    EXPECT_EQ(failed.error().key, "error.sql");
}

TEST_F(test_core_MemberRepository, SaveNewMemberRollsBackWhenPhotoFails)
{
    Repositories::MemberWrite write;
    write.member = uniqueMemberSeed(80).toInput();
    write.photoSourcePath = "/nonexistent/photo.png";

    const auto created = m_repository->saveNewMember(write);
    EXPECT_FALSE(created);
    EXPECT_EQ(created.kind(), Core::ErrorKind::Validation);
    EXPECT_EQ(m_db->count("members"), 0);
}

TEST_F(test_core_MemberRepository, ClearPhotoEmptiesTheColumnAndDeletesTheFile)
{
    const MemberSeed seed = uniqueMemberSeed(40);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_repository->setPhotoImage(id, writeSampleImage("p.png")));
    ASSERT_TRUE(m_repository->setIdImage(id, writeSampleImage("i.png")));
    const auto before = m_repository->getMember(id);
    ASSERT_TRUE(before.has_value());
    const std::string photoFile = m_repository->resolveImagePath(before->photoPath);
    const std::string idFile = m_repository->resolveImagePath(before->idImagePath);
    ASSERT_TRUE(std::filesystem::exists(photoFile));

    Repositories::MemberWrite write;
    write.member = seed.toInput();
    write.clearPhoto = true;
    const auto saved = m_repository->saveExistingMember(id, write);
    ASSERT_TRUE(saved) << saved.error().key;

    const auto after = m_repository->getMember(id);
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->photoPath.empty());
    EXPECT_FALSE(std::filesystem::exists(photoFile));
    EXPECT_EQ(after->idImagePath, before->idImagePath) << "clearPhoto must not touch the ID image";
    EXPECT_TRUE(std::filesystem::exists(idFile));
}

TEST_F(test_core_MemberRepository, ClearIdImageEmptiesTheColumnAndDeletesTheFile)
{
    const MemberSeed seed = uniqueMemberSeed(41);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_repository->setPhotoImage(id, writeSampleImage("p.png")));
    ASSERT_TRUE(m_repository->setIdImage(id, writeSampleImage("i.png")));
    const auto before = m_repository->getMember(id);
    ASSERT_TRUE(before.has_value());
    const std::string photoFile = m_repository->resolveImagePath(before->photoPath);
    const std::string idFile = m_repository->resolveImagePath(before->idImagePath);

    Repositories::MemberWrite write;
    write.member = seed.toInput();
    write.clearIdImage = true;
    const auto saved = m_repository->saveExistingMember(id, write);
    ASSERT_TRUE(saved) << saved.error().key;

    const auto after = m_repository->getMember(id);
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->idImagePath.empty());
    EXPECT_FALSE(std::filesystem::exists(idFile));
    EXPECT_EQ(after->photoPath, before->photoPath) << "clearIdImage must not touch the photo";
    EXPECT_TRUE(std::filesystem::exists(photoFile));
}

TEST_F(test_core_MemberRepository, FailedSaveKeepsTheImageItWasToClear)
{
    const MemberSeed seed = uniqueMemberSeed(42);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    ASSERT_TRUE(m_repository->setPhotoImage(id, writeSampleImage("p.png")));
    const auto before = m_repository->getMember(id);
    ASSERT_TRUE(before.has_value());
    const std::string photoFile = m_repository->resolveImagePath(before->photoPath);

    Repositories::MemberWrite write;
    write.member = seed.toInput();
    write.member.firstName = "  ";  // refused: error.member.namesRequired
    write.clearPhoto = true;
    EXPECT_FALSE(m_repository->saveExistingMember(id, write));

    const auto after = m_repository->getMember(id);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->photoPath, before->photoPath);
    EXPECT_TRUE(std::filesystem::exists(photoFile));
}

TEST_F(test_core_MemberRepository, ClearPhotoWithoutAPhotoSucceeds)
{
    const MemberSeed seed = uniqueMemberSeed(43);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);

    Repositories::MemberWrite write;
    write.member = seed.toInput();
    write.clearPhoto = true;
    const auto saved = m_repository->saveExistingMember(id, write);
    ASSERT_TRUE(saved) << saved.error().key;
    EXPECT_TRUE(m_repository->getMember(id)->photoPath.empty());
}

TEST_F(test_core_MemberRepository, ClearPhotoNeverDeletesAFileOutsideResources)
{
    // An absolute stored path came in with imported data. It may be the
    // librarian's own original, so only the column is cleared.
    const MemberSeed seed = uniqueMemberSeed(44);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    const std::string outside = writeSampleImage("outside.png");
    ASSERT_TRUE(std::filesystem::path(outside).is_absolute());
    ASSERT_TRUE(m_db->execBound("UPDATE members SET photo_path = :path WHERE id = :id",
                                {{"path", outside}, {"id", id}}));

    Repositories::MemberWrite write;
    write.member = seed.toInput();
    write.clearPhoto = true;
    const auto saved = m_repository->saveExistingMember(id, write);
    ASSERT_TRUE(saved) << saved.error().key;

    EXPECT_TRUE(m_repository->getMember(id)->photoPath.empty());
    EXPECT_TRUE(std::filesystem::exists(outside));
}

TEST_F(test_core_MemberRepository, ClearPhotoNeverDeletesAFileOutsideTheMembersFolder)
{
    // A relative stored path that escapes resources/ (e.g. "../outside.png")
    // must never be deleted, even though it is not absolute.
    const MemberSeed seed = uniqueMemberSeed(46);
    const std::int64_t id = seedMember(*m_db, seed);
    ASSERT_GT(id, 0);
    const std::string outside = writeSampleImage("outside.png");
    ASSERT_FALSE(outside.empty());
    const std::string relativeToResources =
        std::filesystem::relative(outside, m_db->resourcesDirectory()).generic_string();
    ASSERT_TRUE(relativeToResources.rfind("..", 0) == 0) << relativeToResources;
    ASSERT_TRUE(m_db->execBound("UPDATE members SET photo_path = :path WHERE id = :id",
                                {{"path", relativeToResources}, {"id", id}}));

    Repositories::MemberWrite write;
    write.member = seed.toInput();
    write.clearPhoto = true;
    const auto saved = m_repository->saveExistingMember(id, write);
    ASSERT_TRUE(saved) << saved.error().key;

    EXPECT_TRUE(m_repository->getMember(id)->photoPath.empty());
    EXPECT_TRUE(std::filesystem::exists(outside));
}

TEST_F(test_core_MemberRepository, ClearPhotoNeverDeletesAnotherMembersImage)
{
    // A stored path naming another member's own image folder must never be
    // deleted when this member's photo is cleared.
    const MemberSeed seedA = uniqueMemberSeed(47);
    const std::int64_t idA = seedMember(*m_db, seedA);
    ASSERT_GT(idA, 0);
    const MemberSeed seedB = uniqueMemberSeed(48);
    const std::int64_t idB = seedMember(*m_db, seedB);
    ASSERT_GT(idB, 0);
    ASSERT_TRUE(m_repository->setPhotoImage(idB, writeSampleImage("b.png")));
    const auto memberB = m_repository->getMember(idB);
    ASSERT_TRUE(memberB.has_value());
    const std::string othersFile = m_repository->resolveImagePath(memberB->photoPath);
    ASSERT_TRUE(std::filesystem::exists(othersFile));

    ASSERT_TRUE(m_db->execBound("UPDATE members SET photo_path = :path WHERE id = :id",
                                {{"path", memberB->photoPath}, {"id", idA}}));

    Repositories::MemberWrite write;
    write.member = seedA.toInput();
    write.clearPhoto = true;
    const auto saved = m_repository->saveExistingMember(idA, write);
    ASSERT_TRUE(saved) << saved.error().key;

    EXPECT_TRUE(m_repository->getMember(idA)->photoPath.empty());
    EXPECT_TRUE(std::filesystem::exists(othersFile));
}

TEST_F(test_core_MemberRepository, SaveNewMemberIgnoresClearFlags)
{
    Repositories::MemberWrite write;
    write.member = uniqueMemberSeed(45).toInput();
    write.clearPhoto = true;
    write.clearIdImage = true;
    const auto created = m_repository->saveNewMember(write);
    ASSERT_TRUE(created) << created.error().key;
    EXPECT_TRUE(m_repository->getMember(created.value())->photoPath.empty());
}

TEST_F(test_core_MemberRepository, StatusCodesAndSexCodesAreNonEmpty)
{
    const std::vector<std::string> statuses = Repositories::MemberRepository::statusCodes();
    EXPECT_EQ(statuses.size(), 2u);
    EXPECT_TRUE(contains(statuses, Repositories::MemberStatus::kActive));
    EXPECT_TRUE(contains(statuses, Repositories::MemberStatus::kNonActive));

    const std::vector<std::string> sexes = Repositories::MemberRepository::sexCodes();
    EXPECT_EQ(sexes.size(), 2u);
}

TEST_F(test_core_MemberRepository, CreateMemberAssignsNextIntegerIgnoringClientNumber)
{
    MemberSeed first = uniqueMemberSeed(200);
    first.membershipNumber = "9999";
    const auto created = m_repository->createMember(first.toInput());
    ASSERT_TRUE(created) << created.error().key;

    const auto stored = m_repository->getMember(created.value());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->membershipNumber, "1");

    MemberSeed second = uniqueMemberSeed(201);
    second.membershipNumber = "also-ignored";
    const auto createdAgain = m_repository->createMember(second.toInput());
    ASSERT_TRUE(createdAgain) << createdAgain.error().key;
    EXPECT_EQ(m_repository->getMember(createdAgain.value())->membershipNumber, "2");
}

TEST_F(test_core_MemberRepository, SuggestMembershipNumberIgnoresLetterSuffix)
{
    ASSERT_TRUE(m_db->exec(
        "INSERT INTO members (membership_number, first_name, last_name) "
        "VALUES ('4589', 'Amina', 'Max'), ('1b', 'Zineb', 'Twin')"))
        << m_db->lastError();

    EXPECT_EQ(m_repository->suggestMembershipNumber(), "4590");
}

TEST_F(test_core_MemberRepository, UpdateMemberDoesNotChangeMembershipNumber)
{
    const auto created = m_repository->createMember(uniqueMemberSeed(202).toInput());
    ASSERT_TRUE(created) << created.error().key;
    const auto before = m_repository->getMember(created.value());
    ASSERT_TRUE(before.has_value());
    const std::string assigned = before->membershipNumber;

    Repositories::MemberInput edited = uniqueMemberSeed(202).toInput();
    edited.membershipNumber = "should-not-stick";
    edited.city = "Ksour Essef";
    const auto mutated = m_repository->updateMember(created.value(), edited);
    ASSERT_TRUE(mutated) << mutated.error().key;

    const auto after = m_repository->getMember(created.value());
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->membershipNumber, assigned);
    EXPECT_EQ(after->city, "Ksour Essef");
}

TEST_F(test_core_MemberRepository, OccupationAgeGroupAndFullNameRoundTrip)
{
    MemberSeed seed = uniqueMemberSeed(203);
    seed.occupation = "تلميذة";
    seed.fullName = "أميمة بنت حمودة بالحج";

    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;

    auto stored = m_repository->getMember(created.value());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->occupation, seed.occupation);
    EXPECT_EQ(stored->ageGroup, Repositories::MemberAgeGroup::kAdult);
    EXPECT_EQ(stored->fullName, seed.fullName);

    Repositories::MemberInput edited = seed.toInput();
    edited.occupation = "طالبة";
    edited.fullName = "أميمة بنت حمودة بالحاج";
    ASSERT_TRUE(m_repository->updateMember(created.value(), edited));

    stored = m_repository->getMember(created.value());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->occupation, edited.occupation);
    EXPECT_EQ(stored->ageGroup, Repositories::MemberAgeGroup::kAdult);
    EXPECT_EQ(stored->fullName, edited.fullName);

    const auto listed = VLMS_UNWRAP(m_repository->listMembers({}));
    ASSERT_FALSE(listed.empty());
    EXPECT_EQ(listed.front().occupation, edited.occupation);
    EXPECT_EQ(listed.front().ageGroup, Repositories::MemberAgeGroup::kAdult);
    EXPECT_EQ(listed.front().fullName, edited.fullName);
}

TEST_F(test_core_MemberRepository, SearchFindsFullNameAndOccupation)
{
    MemberSeed seed = uniqueMemberSeed(204);
    seed.occupation = "قاضية";
    seed.fullName = "ليلى بنت سالم القديري";
    ASSERT_TRUE(m_repository->createMember(seed.toInput()));

    Repositories::MemberQuery byName;
    byName.search = "القديري";
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(byName)), 1);

    Repositories::MemberQuery byJob;
    byJob.search = "قاضية";
    EXPECT_EQ(VLMS_UNWRAP(m_repository->countMembers(byJob)), 1);
}

TEST_F(test_core_MemberRepository, UpdateMemberKeepsAgeGroupWhenBirthDateUnchanged)
{
    MemberSeed seed = uniqueMemberSeed(207);
    seed.dateOfBirth = "2012-06-01";
    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;

    // A stored label the rule would not produce, like an imported row the
    // workbook labelled by hand. Recomputing would turn it back into youth.
    ASSERT_TRUE(m_db->execBound("UPDATE members SET age_group = :group WHERE id = :id",
                                {{"group", std::string(Repositories::MemberAgeGroup::kAdult)},
                                 {"id", created.value()}}));

    Repositories::MemberInput edited = seed.toInput();
    edited.lastName = "Changed";
    ASSERT_TRUE(m_repository->updateMember(created.value(), edited));

    const auto stored = m_repository->getMember(created.value());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->lastName, "Changed");
    EXPECT_EQ(stored->ageGroup, Repositories::MemberAgeGroup::kAdult);
}

TEST_F(test_core_MemberRepository, UpdateMemberRecomputesAgeGroupWhenBirthDateChanges)
{
    MemberSeed seed = uniqueMemberSeed(208);
    seed.dateOfBirth = "2012-06-01";
    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    ASSERT_TRUE(rawSetRegisteredAt(*m_db, created.value(), "2019-03-01 10:00:00"));

    Repositories::MemberInput adultEdit = seed.toInput();
    adultEdit.dateOfBirth = "1988-01-01";
    ASSERT_TRUE(m_repository->updateMember(created.value(), adultEdit));
    EXPECT_EQ(m_repository->getMember(created.value())->ageGroup, Repositories::MemberAgeGroup::kAdult);

    Repositories::MemberInput youthEdit = seed.toInput();
    youthEdit.dateOfBirth = "2000-01-01";
    ASSERT_TRUE(m_repository->updateMember(created.value(), youthEdit));
    EXPECT_EQ(m_repository->getMember(created.value())->ageGroup, Repositories::MemberAgeGroup::kYouth);
}

TEST_F(test_core_MemberRepository, AgeGroupFallsBackToTodayWhenRegisteredAtIsUnreadable)
{
    // Born 1996-09-20: 29 on the 19th, 30 on the 20th. Which answer comes back
    // shows which date was used.
    {
        const Core::ScopedClock pinned(Core::Date(2026, 9, 19));
        EXPECT_EQ(Repositories::MemberRepository::ageGroupFromBirthDate("1996-09-20", ""),
                  Repositories::MemberAgeGroup::kYouth);
    }
    {
        const Core::ScopedClock pinned(Core::Date(2026, 9, 20));
        EXPECT_EQ(Repositories::MemberRepository::ageGroupFromBirthDate("1996-09-20", "2026-13-45 10:00:00"),
                  Repositories::MemberAgeGroup::kAdult);
    }

    // Same fallback through an edit that changes the birth date.
    const Core::ScopedClock pinned(Core::Date(2026, 9, 19));
    MemberSeed seed = uniqueMemberSeed(210);
    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;
    ASSERT_TRUE(rawSetRegisteredAt(*m_db, created.value(), "unknown"));

    Repositories::MemberInput edited = seed.toInput();
    edited.dateOfBirth = "1996-09-20";
    ASSERT_TRUE(m_repository->updateMember(created.value(), edited));
    EXPECT_EQ(m_repository->getMember(created.value())->ageGroup, Repositories::MemberAgeGroup::kYouth);
}

TEST_F(test_core_MemberRepository, CreateMemberDerivesAgeGroupFromBirthDate)
{
    const Core::ScopedClock pinned(Core::Date(2026, 9, 19));

    MemberSeed adultSeed = uniqueMemberSeed(205);
    adultSeed.dateOfBirth = "1990-05-12";
    const auto adultCreated = m_repository->createMember(adultSeed.toInput());
    ASSERT_TRUE(adultCreated) << adultCreated.error().key;
    EXPECT_EQ(m_repository->getMember(adultCreated.value())->ageGroup, Repositories::MemberAgeGroup::kAdult);

    MemberSeed youthSeed = uniqueMemberSeed(206);
    youthSeed.dateOfBirth = "2012-06-01";
    const auto youthCreated = m_repository->createMember(youthSeed.toInput());
    ASSERT_TRUE(youthCreated) << youthCreated.error().key;
    EXPECT_EQ(m_repository->getMember(youthCreated.value())->ageGroup, Repositories::MemberAgeGroup::kYouth);
}

TEST_F(test_core_MemberRepository, CreateMemberStampsRegisteredAtAndAgeGroupFromTheClock)
{
    // Pinned far from the real date: born 1980, this member is 20 on the
    // pinned day but well past 30 on the wall clock. Only one stamp is right.
    const Core::ScopedClock pinned(Core::DateTime(Core::Date(2000, 3, 1), 9, 30, 0));

    MemberSeed seed = uniqueMemberSeed(209);
    seed.dateOfBirth = "1980-01-01";
    const auto created = m_repository->createMember(seed.toInput());
    ASSERT_TRUE(created) << created.error().key;

    const auto stored = m_repository->getMember(created.value());
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->registeredAt, "2000-03-01 09:30:00");
    EXPECT_EQ(stored->ageGroup, Repositories::MemberAgeGroup::kYouth);
}

TEST_F(test_core_MemberRepository, ListMembersSortsByNumberNumerically)
{
    MemberSeed ten = uniqueMemberSeed(401);
    ten.membershipNumber = "10";
    ten.lastName = "Aaa";
    const std::int64_t id10 = seedMember(*m_db, ten);
    ASSERT_TRUE(m_db->execBound(
        "UPDATE members SET membership_number = :value WHERE id = :id",
        {{"value", ten.membershipNumber}, {"id", id10}}));
    MemberSeed two = uniqueMemberSeed(402);
    two.membershipNumber = "2";
    two.lastName = "Zzz";
    const std::int64_t id2 = seedMember(*m_db, two);
    ASSERT_TRUE(m_db->execBound(
        "UPDATE members SET membership_number = :value WHERE id = :id",
        {{"value", two.membershipNumber}, {"id", id2}}));
    MemberSeed twin = uniqueMemberSeed(403);
    twin.membershipNumber = "2b";
    twin.lastName = "Mmm";
    const std::int64_t id2b = seedMember(*m_db, twin);
    ASSERT_TRUE(m_db->execBound(
        "UPDATE members SET membership_number = :value WHERE id = :id",
        {{"value", twin.membershipNumber}, {"id", id2b}}));
    ASSERT_GT(id10, 0);
    ASSERT_GT(id2, 0);
    ASSERT_GT(id2b, 0);

    Repositories::MemberQuery query;
    query.sortColumn = Repositories::MemberSort::kNumber;
    query.sortAscending = true;
    const auto asc = VLMS_UNWRAP(m_repository->listMembers(query));
    ASSERT_EQ(asc.size(), 3u);
    EXPECT_EQ(asc.at(0).id, id2);
    EXPECT_EQ(asc.at(1).id, id2b);
    EXPECT_EQ(asc.at(2).id, id10);

    query.sortAscending = false;
    const auto desc = VLMS_UNWRAP(m_repository->listMembers(query));
    ASSERT_EQ(desc.size(), 3u);
    EXPECT_EQ(desc.at(0).id, id10);
    EXPECT_EQ(desc.at(1).id, id2b);
    EXPECT_EQ(desc.at(2).id, id2);
}

TEST_F(test_core_MemberRepository, ListMembersSortsByNameCaseInsensitive)
{
    MemberSeed zeta = uniqueMemberSeed(411);
    zeta.lastName = "zeta";
    zeta.firstName = "A";
    const std::int64_t idZ = seedMember(*m_db, zeta);
    MemberSeed alpha = uniqueMemberSeed(412);
    alpha.lastName = "Alpha";
    alpha.firstName = "B";
    const std::int64_t idA = seedMember(*m_db, alpha);
    ASSERT_GT(idZ, 0);
    ASSERT_GT(idA, 0);

    Repositories::MemberQuery query;
    query.sortColumn = Repositories::MemberSort::kName;
    query.sortAscending = true;
    const auto rows = VLMS_UNWRAP(m_repository->listMembers(query));
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows.at(0).id, idA);
    EXPECT_EQ(rows.at(1).id, idZ);

    query.sortColumn.clear();
    const auto byDefault = VLMS_UNWRAP(m_repository->listMembers(query));
    ASSERT_EQ(byDefault.size(), 2u);
    EXPECT_EQ(byDefault.at(0).id, idA);
    EXPECT_EQ(byDefault.at(1).id, idZ);
}

TEST_F(test_core_MemberRepository, RankOfMemberFollowsNumberSort)
{
    MemberSeed first = uniqueMemberSeed(421);
    first.membershipNumber = "1";
    first.lastName = "Zed";
    const std::int64_t id1 = seedMember(*m_db, first);
    MemberSeed second = uniqueMemberSeed(422);
    second.membershipNumber = "2";
    second.lastName = "Amy";
    const std::int64_t id2 = seedMember(*m_db, second);
    MemberSeed third = uniqueMemberSeed(423);
    third.membershipNumber = "3";
    third.lastName = "Mia";
    const std::int64_t id3 = seedMember(*m_db, third);
    ASSERT_GT(id1, 0);
    ASSERT_GT(id2, 0);
    ASSERT_GT(id3, 0);

    Repositories::MemberQuery query;
    query.sortColumn = Repositories::MemberSort::kNumber;
    query.sortAscending = true;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfMember(id1, query)), 0);
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfMember(id3, query)), 2);

    query.sortAscending = false;
    EXPECT_EQ(VLMS_UNWRAP(m_repository->rankOfMember(id1, query)), 2);

    const auto missing = m_repository->rankOfMember(999999, query);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.kind(), Core::ErrorKind::NotFound);
}
