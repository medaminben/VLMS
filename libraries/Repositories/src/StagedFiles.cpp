#include "StagedFiles.h"

namespace VLMS::Repositories {

namespace {

std::filesystem::path withSuffix(const std::filesystem::path& target, const char* suffix)
{
    std::filesystem::path result = target;
    result += suffix;
    return result;
}

}  // namespace

StagedFiles::~StagedFiles()
{
    discard();
}

bool StagedFiles::stage(const std::filesystem::path& source, const std::filesystem::path& target)
{
    const std::filesystem::path part = withSuffix(target, ".part");
    std::error_code error;
    std::filesystem::remove(part, error);
    if (!std::filesystem::copy_file(source, part, error)) {
        std::filesystem::remove(part, error);
        return false;
    }
    m_files.push_back({part, target, withSuffix(target, ".old"), false, false});
    return true;
}

bool StagedFiles::swap()
{
    for (Entry& entry : m_files) {
        std::error_code error;
        const std::filesystem::file_status status =
            std::filesystem::symlink_status(entry.target, error);
        // A failed stat is not "no previous file": renaming over the target
        // without a backup would replace the old image for good on Windows.
        if (error && error != std::errc::no_such_file_or_directory) {
            discard();
            return false;
        }
        const bool exists = !error && status.type() != std::filesystem::file_type::not_found;
        error.clear();
        // Only a regular file is moved aside: anything else at the target is not
        // an image this repository wrote, and renaming it away could hide data.
        if (exists && !std::filesystem::is_regular_file(status)) {
            discard();
            return false;
        }
        if (exists) {
            std::filesystem::remove(entry.backup, error);
            error.clear();
            std::filesystem::rename(entry.target, entry.backup, error);
            if (error) {
                discard();
                return false;
            }
            entry.backedUp = true;
        }
        std::filesystem::rename(entry.part, entry.target, error);
        if (error) {
            discard();
            return false;
        }
        entry.placed = true;
    }
    return true;
}

void StagedFiles::finish()
{
    for (const Entry& entry : m_files) {
        if (entry.backedUp) {
            std::error_code ignored;
            std::filesystem::remove(entry.backup, ignored);
        }
    }
    m_files.clear();
}

void StagedFiles::undoSwap()
{
    // Reverse order, so the state before swap() comes back step by step.
    for (auto it = m_files.rbegin(); it != m_files.rend(); ++it) {
        std::error_code ignored;
        if (it->placed) {
            std::filesystem::remove(it->target, ignored);
            it->placed = false;
        }
        if (it->backedUp) {
            std::filesystem::rename(it->backup, it->target, ignored);
            it->backedUp = false;
        }
    }
}

void StagedFiles::discard()
{
    undoSwap();
    for (const Entry& entry : m_files) {
        std::error_code ignored;
        std::filesystem::remove(entry.part, ignored);
    }
    m_files.clear();
}

}  // namespace VLMS::Repositories
