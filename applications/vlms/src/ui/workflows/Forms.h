#pragma once

#include <VLMS/Repositories/ArchiveTypes.h>
#include <VLMS/Repositories/CatalogTypes.h>
#include <VLMS/Repositories/LoanTypes.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QWidget;

namespace VLMS {

class DataEvents;
class LoanListSpec;
class LoanWorkflow;

struct BookForm {
    Repositories::BookInput book;
    std::vector<Repositories::BookCopyInput> copies;
    QString coverSourcePath;
    bool coverChanged = false;
};

/// What the book editor opens on. No `book` means a new book.
/// `reserveNumberFrom` locks an archived copy's number into the copy list
/// (Archive -> Reuse number). `manageCategories` runs when the librarian
/// presses Categories inside the editor.
struct BookEditRequest {
    std::optional<Repositories::BookRecord> book;
    std::optional<Repositories::BookCopyRecord> reserveNumberFrom;
    std::function<void(QWidget*)> manageCategories;
};

struct MemberForm {
    Repositories::MemberInput member;
    QString photoSourcePath;
    QString idImageSourcePath;
    bool photoChanged = false;
    bool idImageChanged = false;
    bool photoRemoved = false;
    bool idImageRemoved = false;
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

struct BulkPlan {
    QString title;
    QString text;
};

enum class LoanHistoryKind { Book, Member, ArchivedMember, ArchivedBook, ArchivedCopy };

/// One loan history window, pinned to a book, member or copy.
struct LoanHistoryRequest {
    LoanHistoryKind kind = LoanHistoryKind::Book;
    qint64 id = 0;
    QString title;
    Repositories::ArchiveScope scope = Repositories::ArchiveScope::Any;
    LoanListSpec* spec = nullptr;
    LoanWorkflow* loans = nullptr;
    DataEvents* events = nullptr;
};

}  // namespace VLMS
