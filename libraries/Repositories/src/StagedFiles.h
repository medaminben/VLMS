#pragma once

#include <filesystem>
#include <vector>

namespace VLMS::Repositories {

/// Image files copied in during a save, replaced in two phases so that disk and
/// database cannot disagree. Each copy lands beside its target as
/// `<target>.part`. swap() then puts the copies in place as the last step inside
/// the save's transaction, keeping the previous file as `<target>.old`, and
/// finish() drops those backups once the transaction has committed. A save that
/// is refused, fails in SQL, fails to swap or fails at COMMIT leaves every
/// target as it was: discard(), or the destructor, restores the backups and
/// removes the copies. A swap done inside the transaction is what makes a failed
/// swap an ordinary rollback instead of an error reported for a saved row. A
/// stale `.old` left by a crash between swap and commit is replaced by the next
/// save of that slot.
class StagedFiles {
public:
    StagedFiles() = default;
    StagedFiles(const StagedFiles&) = delete;
    StagedFiles& operator=(const StagedFiles&) = delete;
    ~StagedFiles();

    /// Copies `source` to `target` + ".part". False when the copy failed;
    /// nothing is left behind then.
    [[nodiscard]] bool stage(const std::filesystem::path& source,
                             const std::filesystem::path& target);

    /// Moves every existing target aside to `<target>.old` and renames each copy
    /// to its target; no target is ever deleted. False when a step failed, after
    /// every swap done so far has been undone and the copies removed.
    [[nodiscard]] bool swap();

    /// Called once the transaction has committed: removes the `.old` backups, so
    /// the new files stay for good.
    void finish();

    /// Undoes a swap that was not finished (backups come back, targets that had
    /// no previous file are removed) and removes the remaining copies.
    void discard();

private:
    struct Entry {
        std::filesystem::path part;
        std::filesystem::path target;
        std::filesystem::path backup;
        bool backedUp = false;  // the previous target now lives at `backup`
        bool placed = false;    // the copy now lives at `target`
    };

    void undoSwap();

    std::vector<Entry> m_files;
};

}  // namespace VLMS::Repositories
