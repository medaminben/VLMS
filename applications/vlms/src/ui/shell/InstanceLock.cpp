#include "ui/shell/InstanceLock.h"

#include <QDir>
#include <QSysInfo>

namespace VLMS {

InstanceLock::InstanceLock(const QString& directory)
    : m_lock(QDir(directory).filePath(QStringLiteral("vlms.lock")))
{
    // Never stale by age: the default 30 seconds would hand the lock to a
    // second copy while the first is still open. A lock whose process is gone
    // is still recognised as stale and taken over.
    m_lock.setStaleLockTime(0);
}

bool InstanceLock::heldByAnotherCopy()
{
    if (m_lock.tryLock(0)) {
        return false;
    }
    if (m_lock.error() != QLockFile::LockFailedError) {
        return false;
    }

    // QLockFile asks whether the owner is still alive only when the lock names
    // this computer. A lock written on another one (a restored backup, a renamed
    // computer) can never belong to a copy running here, and with the stale time
    // at zero it would otherwise block VLMS for good.
    qint64 pid = 0;
    QString hostname;
    QString appname;
    if (m_lock.getLockInfo(&pid, &hostname, &appname) && !hostname.isEmpty()
        && hostname != QSysInfo::machineHostName()) {
        m_lock.removeStaleLockFile();
        if (m_lock.tryLock(0)) {
            return false;
        }
    }
    return m_lock.error() == QLockFile::LockFailedError;
}

}  // namespace VLMS
