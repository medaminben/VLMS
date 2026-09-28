#pragma once

#include <VLMS/Core/Date.h>
#include <VLMS/Repositories/MemberTypes.h>
#include <VLMS/Core/Result.h>

#include <cstdint>
#include <string>
#include <vector>

namespace VLMS {
class SqliteSession;
}

class MemberRepository {
public:
    MemberRepository(VLMS::SqliteSession& session, std::string resourcesDirectory);

    [[nodiscard]] VLMS::Result<std::vector<MemberRecord>> listMembers(
        const MemberQuery& query) const;
    [[nodiscard]] VLMS::Result<int> rankOfMember(std::int64_t id,
                                                       const MemberQuery& query) const;
    [[nodiscard]] VLMS::Result<int> countMembers(const MemberQuery& query) const;
    [[nodiscard]] VLMS::Result<MemberRecord> getMember(std::int64_t id) const;

    enum class MemberRemovalBlock {
        None,
        OpenLoans,
        LoanHistory,
    };

    [[nodiscard]] VLMS::Result<std::int64_t> createMember(const MemberInput& input);
    [[nodiscard]] VLMS::Status updateMember(std::int64_t id, const MemberInput& input);
    [[nodiscard]] VLMS::Result<std::int64_t> saveNewMember(const MemberWrite& write);
    [[nodiscard]] VLMS::Status saveExistingMember(std::int64_t id, const MemberWrite& write);

    [[nodiscard]] VLMS::Result<MemberRemovalBlock> removalBlock(std::int64_t id) const;
    [[nodiscard]] VLMS::Status archiveMember(std::int64_t id);
    /// The same answer archiveMember gives before it writes.
    [[nodiscard]] VLMS::Status canArchiveMember(std::int64_t id) const;
    [[nodiscard]] VLMS::Status restoreMember(std::int64_t id);
    [[nodiscard]] VLMS::Status purgeMember(std::int64_t id);
    /// The same answer purgeMember gives before it writes. A live member is
    /// refused for being live, before any loan of theirs is considered.
    [[nodiscard]] VLMS::Status canPurgeMember(std::int64_t id) const;
    [[nodiscard]] VLMS::Status setPhotoImage(std::int64_t memberId,
                                                   const std::string& sourceFilePath);
    [[nodiscard]] VLMS::Status setIdImage(std::int64_t memberId,
                                                const std::string& sourceFilePath);

    [[nodiscard]] std::string resolveImagePath(const std::string& storedPath) const;
    [[nodiscard]] std::string suggestMembershipNumber() const;

    [[nodiscard]] VLMS::Result<std::vector<std::string>> listCities(ArchiveScope scope = ArchiveScope::Live) const;
    [[nodiscard]] VLMS::Result<std::vector<std::string>> listInscriptionYears(ArchiveScope scope = ArchiveScope::Live) const;

    [[nodiscard]] static std::vector<std::string> statusCodes();
    [[nodiscard]] static std::vector<std::string> sexCodes();
    [[nodiscard]] static std::vector<std::string> ageGroupCodes();
    [[nodiscard]] static bool isValidEmail(const std::string& email);
    [[nodiscard]] static bool isValidDateOfBirth(const std::string& dateOfBirth);
    [[nodiscard]] static bool isDateOfBirthInFuture(const std::string& dateOfBirth);
    [[nodiscard]] static std::string ageGroupFromBirthDate(const std::string& dateOfBirth,
                                                           const std::string& registeredAt);
    /// Active while activeUntil (the member's last active day) is today or
    /// later. Empty or unreadable is not active.
    [[nodiscard]] static std::string statusOn(const std::string& activeUntil,
                                              const VLMS::Date& today);
    /// The last active day to store once a librarian has chosen chosenStatus.
    /// Unchanged status keeps the date; Active starts a year today (a new
    /// member, or a renewal); Not active ends yesterday. An empty
    /// currentActiveUntil is a member not yet saved.
    [[nodiscard]] static std::string activeUntilFor(const std::string& currentActiveUntil,
                                                    const std::string& chosenStatus,
                                                    const VLMS::Date& today);

private:
    enum class ImageSlot { Photo, IdCard };

    [[nodiscard]] VLMS::Status recordStatusChange(std::int64_t memberId,
                                                        const std::string& oldStatus,
                                                        const std::string& newStatus,
                                                        const std::string& note = {});
    [[nodiscard]] VLMS::Status storeMemberImage(std::int64_t memberId,
                                                      const std::string& sourceFilePath,
                                                      ImageSlot slot);
    /// Sets the slot's column to NULL and appends the path it held to
    /// `clearedPaths`, so the caller can delete the file once the
    /// transaction has committed. A slot already empty is left alone.
    [[nodiscard]] VLMS::Status clearMemberImage(std::int64_t memberId,
                                                ImageSlot slot,
                                                std::vector<std::string>& clearedPaths);
    [[nodiscard]] bool isValidStatus(const std::string& status) const;
    [[nodiscard]] bool isValidSex(const std::string& sex) const;
    [[nodiscard]] VLMS::Status validateInput(const MemberInput& input) const;
    [[nodiscard]] VLMS::Result<std::int64_t> insertMemberRow(const MemberInput& input);
    [[nodiscard]] VLMS::Status applyMemberFields(std::int64_t id, const MemberInput& input);

    VLMS::SqliteSession& m_session;
    std::string m_resourcesDirectory;
};
