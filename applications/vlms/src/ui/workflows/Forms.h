#pragma once

#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Repositories/LoanTypes.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QString>

#include <vector>

namespace VLMS {

struct BookForm {
    Repositories::BookInput book;
    std::vector<Repositories::BookCopyInput> copies;
    QString coverSourcePath;
    bool coverChanged = false;
};

struct MemberForm {
    Repositories::MemberInput member;
};

struct LoanForm {
    Repositories::LoanInput loan;
    qint64 bookId = 0;
    qint64 memberId = 0;
};

struct ReturnForm {
    QString returnedAt;
    QString notes;
};

struct ExtendForm {
    QString dueAt;
};

struct CategoryEdits {
    bool applied = false;
};

struct BulkPlan {
    QString title;
    QString text;
};

}  // namespace VLMS
