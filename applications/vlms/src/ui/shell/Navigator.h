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

}  // namespace VLMS
