#pragma once

#include <QLockFile>
#include <QString>

namespace VLMS {

/// One running copy per installation. Two copies on one vlms.db would each
/// see the other's writes fail with "database is locked".
class InstanceLock {
public:
    /// The lock file is `vlms.lock` inside `directory`, the database folder.
    explicit InstanceLock(const QString& directory);

    /// True only while another running copy holds the lock. A lock left behind by
    /// a copy that crashed is taken over. When the lock file cannot be written at
    /// all (a read-only or missing folder) this is false too: the database cannot
    /// open either, and its own error tells the librarian why. When no other copy
    /// holds the lock, the call takes it for the lifetime of this object, so call
    /// it once, at startup.
    [[nodiscard]] bool heldByAnotherCopy();

private:
    QLockFile m_lock;
};

}  // namespace VLMS
