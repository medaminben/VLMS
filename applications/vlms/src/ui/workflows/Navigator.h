#pragma once

#include <QString>

#include <cstdint>

namespace VLMS {

enum class PageId {
    Catalog,
    Members,
    Circulation,
    Archive,
    Metrics
};

enum class ArchiveType {
    Members,
    Books,
    Copies,
    Loans
};

class Navigator {
public:
    virtual ~Navigator() = default;
    virtual void show(PageId page) = 0;
    virtual void showMemberLoans(const QString& membershipNumber) = 0;
    virtual void showBook(qint64 bookId) = 0;
    virtual void showArchive(ArchiveType type) = 0;
};

class FakeNavigator final : public Navigator {
public:
    PageId lastPage = PageId::Catalog;
    QString lastMembershipNumber;
    qint64 lastBookId = 0;
    ArchiveType lastArchiveType = ArchiveType::Members;
    int showMemberLoansCount = 0;

    void show(PageId page) override { lastPage = page; }

    void showMemberLoans(const QString& membershipNumber) override
    {
        ++showMemberLoansCount;
        lastPage = PageId::Circulation;
        lastMembershipNumber = membershipNumber;
    }

    void showBook(qint64 bookId) override
    {
        lastPage = PageId::Catalog;
        lastBookId = bookId;
    }

    void showArchive(ArchiveType type) override
    {
        lastPage = PageId::Archive;
        lastArchiveType = type;
    }
};

}  // namespace VLMS
