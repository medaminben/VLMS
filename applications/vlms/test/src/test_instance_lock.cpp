#include "ui/shell/InstanceLock.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSysInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

// Two copies on one vlms.db each see the other's writes fail with "database is
// locked", so a second copy of the same installation must not start.
TEST(InstanceLock, ASecondCopyIsRefusedWhileTheFirstRuns)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    VLMS::InstanceLock first(dir.path());
    ASSERT_FALSE(first.heldByAnotherCopy());

    VLMS::InstanceLock second(dir.path());
    EXPECT_TRUE(second.heldByAnotherCopy());
}

TEST(InstanceLock, TheLockIsFreeAgainOnceTheFirstCopyExits)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    {
        VLMS::InstanceLock first(dir.path());
        ASSERT_FALSE(first.heldByAnotherCopy());
    }
    VLMS::InstanceLock next(dir.path());
    EXPECT_FALSE(next.heldByAnotherCopy());
}

// QLockFile's default treats a lock older than 30 seconds as stale even while
// its owner runs, which would let a second copy start once the first had been
// open for half a minute.
TEST(InstanceLock, AnOldLockOfARunningCopyIsNotTakenOver)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    VLMS::InstanceLock first(dir.path());
    ASSERT_FALSE(first.heldByAnotherCopy());

    QFile lockFile(QDir(dir.path()).filePath(QStringLiteral("vlms.lock")));
    ASSERT_TRUE(lockFile.exists());
    ASSERT_TRUE(lockFile.open(QIODevice::ReadWrite));
    ASSERT_TRUE(lockFile.setFileTime(QDateTime::currentDateTime().addSecs(-3600),
                                     QFileDevice::FileModificationTime));
    lockFile.close();

    VLMS::InstanceLock second(dir.path());
    EXPECT_TRUE(second.heldByAnotherCopy());
}

// A folder that cannot hold the lock file says nothing about another copy:
// the database cannot open there either, and its own error explains why.
TEST(InstanceLock, AFolderThatCannotHoldTheLockIsNotReportedAsAnotherCopy)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    VLMS::InstanceLock lock(dir.filePath(QStringLiteral("missing")));
    EXPECT_FALSE(lock.heldByAnotherCopy());
}

namespace {

void writeLockFile(const QString& directory, const QByteArray& content)
{
    QFile file(QDir(directory).filePath(QStringLiteral("vlms.lock")));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(content);
}

}  // namespace

// QLockFile only checks whether the owner is alive when the lock names this
// computer, so a lock copied here by a restored backup would otherwise block
// VLMS for good.
TEST(InstanceLock, ALockLeftOnAnotherComputerIsTakenOver)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeLockFile(dir.path(), "999999\nvlms\nOTHER-PC\n");

    VLMS::InstanceLock lock(dir.path());
    EXPECT_FALSE(lock.heldByAnotherCopy());
}

TEST(InstanceLock, ALockLeftByACrashedCopyIsTakenOver)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    writeLockFile(dir.path(),
                  "999999\nvlms\n" + QSysInfo::machineHostName().toUtf8() + "\n");

    VLMS::InstanceLock lock(dir.path());
    EXPECT_FALSE(lock.heldByAnotherCopy());
}
