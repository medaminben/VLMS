#include "WorkflowTestSupport.h"

using namespace Test;

class ArchiveWorkflow : public WorkflowFixture {
protected:
    void SetUp() override
    {
        WorkflowFixture::SetUp();
        memberId = seedMember(*db, uniqueMemberSeed(1));
        ASSERT_TRUE(memberRepo->archiveMember(memberId));
    }

    bool memberArchived(qint64 id) const
    {
        return !scalarIsNull("SELECT archived_at FROM members WHERE id = " + std::to_string(id));
    }

    /// An archived book's copy, and the reuse form that keeps its number.
    struct ArchivedCopy {
        qint64 copyId = 0;
        VLMS::Repositories::BookCopyInput number;
    };

    ArchivedCopy archivedCopy(int index)
    {
        const qint64 bookId = seedBook(*db, uniqueBookSeed(index));
        ArchivedCopy copy;
        copy.copyId = copyIdsOf(*db, bookId).front();
        const std::string where = " FROM book_copies WHERE id = " + std::to_string(copy.copyId);
        copy.number.source = scalarText("SELECT source" + where);
        copy.number.localId = scalarText("SELECT local_id" + where);
        copy.number.globalCopyId = scalarText("SELECT global_copy_id" + where);
        EXPECT_TRUE(catalog->archiveBook(bookId));
        return copy;
    }

    VLMS::BookForm reuseForm(const ArchivedCopy& copy, const std::string& title) const
    {
        VLMS::BookForm form;
        form.book = uniqueBookSeed(90).toInput();
        form.book.title = title;
        form.copies.push_back(copy.number);
        return form;
    }

    std::string archivedLocalId(const ArchivedCopy& copy) const
    {
        return scalarText("SELECT local_id FROM book_copies WHERE id = " + std::to_string(copy.copyId));
    }

    qint64 memberId = 0;
};

TEST_F(ArchiveWorkflow, RestoreDeclinedEmitsNothing)
{
    dialogs.askYesNoResult = false;

    ASSERT_TRUE(archive->restore(&parent, VLMS::ArchiveType::Members, {memberId},
                                 {QStringLiteral("Amina")}));

    EXPECT_EQ(dialogs.count(QStringLiteral("askYesNo")), 1);
    EXPECT_EQ(changed->count(), 0);
    EXPECT_TRUE(memberArchived(memberId));
}

TEST_F(ArchiveWorkflow, RestoreConfirmMemberEmitsMembersChanged)
{
    dialogs.askYesNoResult = true;

    ASSERT_TRUE(archive->restore(&parent, VLMS::ArchiveType::Members, {memberId},
                                 {QStringLiteral("Amina")}));

    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Members);
    EXPECT_EQ(changedIds(0), QList<qint64>({memberId}));
    EXPECT_FALSE(memberArchived(memberId));
}

TEST_F(ArchiveWorkflow, RestoreManyGoesThroughConfirmBulk)
{
    const qint64 second = seedMember(*db, uniqueMemberSeed(2));
    ASSERT_TRUE(memberRepo->archiveMember(second));
    dialogs.confirmBulkResult = true;

    ASSERT_TRUE(archive->restore(&parent, VLMS::ArchiveType::Members, {memberId, second},
                                 {QStringLiteral("Amina"), QStringLiteral("Second")}));

    EXPECT_EQ(dialogs.count(QStringLiteral("confirmBulk")), 1);
    EXPECT_FALSE(memberArchived(memberId));
    EXPECT_FALSE(memberArchived(second));
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedIds(0), QList<qint64>({memberId, second}));
}

TEST_F(ArchiveWorkflow, PurgeDeclinedEmitsNothing)
{
    dialogs.askYesNoResult = false;

    ASSERT_TRUE(archive->purge(&parent, VLMS::ArchiveType::Members, {memberId},
                               {QStringLiteral("Amina")}));

    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(db->count("members"), 1);
}

TEST_F(ArchiveWorkflow, PurgeConfirmedDeletesAndEmitsMembers)
{
    dialogs.askYesNoResult = true;

    ASSERT_TRUE(archive->purge(&parent, VLMS::ArchiveType::Members, {memberId},
                               {QStringLiteral("Amina")}));

    EXPECT_EQ(db->count("members"), 0);
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Members);
}

TEST_F(ArchiveWorkflow, ShowLoansEarlyExitIsOkWithoutCrash)
{
    EXPECT_TRUE(archive->showLoans(&parent, VLMS::ArchiveType::Members, 0, QStringLiteral("x")));
    EXPECT_TRUE(archive->showLoans(&parent, VLMS::ArchiveType::Loans, 1, QStringLiteral("x")));
    EXPECT_TRUE(dialogs.historyRequests.empty());
}

TEST_F(ArchiveWorkflow, ShowLoansOpensTheArchivedMembersHistory)
{
    ASSERT_TRUE(archive->showLoans(&parent, VLMS::ArchiveType::Members, memberId,
                                   QStringLiteral("Amina")));

    ASSERT_EQ(dialogs.historyRequests.size(), 1U);
    EXPECT_EQ(dialogs.historyRequests.front().kind, VLMS::LoanHistoryKind::ArchivedMember);
    EXPECT_EQ(dialogs.historyRequests.front().id, memberId);
}

TEST_F(ArchiveWorkflow, ReuseNumberZeroIdIsOkWithoutCrash)
{
    EXPECT_TRUE(archive->reuseNumber(&parent, 0));
    EXPECT_EQ(changed->count(), 0);
    EXPECT_TRUE(dialogs.calls.empty());
}

TEST_F(ArchiveWorkflow, ReuseNumberCancelledAtTheChooserLeavesTheNumber)
{
    const ArchivedCopy copy = archivedCopy(20);

    const auto status = archive->reuseNumber(&parent, copy.copyId);

    EXPECT_TRUE(status);
    EXPECT_EQ(dialogs.count(QStringLiteral("chooseReuseTarget")), 1);
    EXPECT_EQ(dialogs.count(QStringLiteral("editBook")), 0);
    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(archivedLocalId(copy), copy.number.localId);
}

TEST_F(ArchiveWorkflow, ReuseNumberCancelledInTheEditorLeavesTheNumber)
{
    const ArchivedCopy copy = archivedCopy(21);
    dialogs.nextReuseTarget = 0;

    const auto status = archive->reuseNumber(&parent, copy.copyId);

    EXPECT_TRUE(status);
    ASSERT_EQ(dialogs.bookRequests.size(), 1U);
    ASSERT_TRUE(dialogs.bookRequests.front().reserveNumberFrom.has_value());
    EXPECT_EQ(dialogs.bookRequests.front().reserveNumberFrom->id, copy.copyId);
    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(archivedLocalId(copy), copy.number.localId);
}

TEST_F(ArchiveWorkflow, ReuseNumberOnANewBookMovesTheNumberAndEmits)
{
    const ArchivedCopy copy = archivedCopy(22);
    dialogs.nextReuseTarget = 0;
    dialogs.nextBook = reuseForm(copy, "Reused Number Book");

    const auto status = archive->reuseNumber(&parent, copy.copyId);

    EXPECT_TRUE(status) << (errors.calls.empty() ? "" : errors.calls.front().key);
    EXPECT_NE(archivedLocalId(copy), copy.number.localId);
    EXPECT_EQ(scalarText("SELECT COUNT(*) FROM book_copies WHERE archived_at IS NULL AND local_id = '"
                         + copy.number.localId + "'"),
              "1");
    ASSERT_EQ(changed->count(), 1);
    EXPECT_EQ(changedWhat(0), VLMS::Entity::Books | VLMS::Entity::Copies);
}

TEST_F(ArchiveWorkflow, ReuseNumberWithAFailedSaveReportsItAndKeepsTheNumber)
{
    const ArchivedCopy copy = archivedCopy(23);
    dialogs.nextReuseTarget = 0;
    dialogs.nextBook = reuseForm(copy, "");

    const auto status = archive->reuseNumber(&parent, copy.copyId);

    EXPECT_FALSE(status);
    EXPECT_EQ(errorKeys(), std::vector<std::string>{"error.book.titleRequired"});
    EXPECT_EQ(changed->count(), 0);
    EXPECT_EQ(archivedLocalId(copy), copy.number.localId);
}
