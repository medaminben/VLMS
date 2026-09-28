#include "ModalTest.h"
#include "TestDatabase.h"
#include "TestSeed.h"
#include "UiTest.h"

#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/archive/ArchivePage.h"
#include "ui/archive/ReuseNumberFlow.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Core/Strings.h>

#include <QAbstractButton>
#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>

#include <gtest/gtest.h>

#include <memory>

using VLMS::Locale;
using VLMS::Strings;
using namespace VLMS;
using namespace Test;

namespace {

bool selectType(QListWidget* list, const QString& code)
{
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        if (item->data(Qt::UserRole).toString() == code) {
            list->setCurrentItem(item);
            item->setSelected(true);
            QApplication::processEvents();
            return true;
        }
    }
    return false;
}

QPushButton* buttonWithText(QWidget* root, const QString& text)
{
    for (QPushButton* button : root->findChildren<QPushButton*>()) {
        if (button->text() == text) {
            return button;
        }
    }
    return nullptr;
}

/// Presses Yes on the next message box; polled because the box becomes the
/// active modal only once exec() has shown it.
void answerNextBoxYes()
{
    auto* poll = new QTimer;
    poll->setInterval(10);
    QObject::connect(poll, &QTimer::timeout, [poll] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (box == nullptr || !box->isVisible()) {
            return;
        }
        poll->stop();
        poll->deleteLater();
        if (QAbstractButton* yes = box->button(QMessageBox::Yes)) {
            yes->click();
        } else {
            box->done(0);
        }
    });
    poll->start();
}

}  // namespace

class test_ui_ArchivePage : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }
    static void TearDownTestSuite() { Locale::setCode(Locale::kDefaultCode); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());

        m_memberId = seedMember(*m_db, uniqueMemberSeed(1));
        ASSERT_TRUE(m_members->archiveMember(m_memberId));

        BookSeed book = uniqueBookSeed(1);
        book.initialCopyCount = 2;
        m_bookId = seedBook(*m_db, book);
        ASSERT_TRUE(m_catalog->archiveBook(m_bookId));
        const auto copies = copyIdsOf(*m_db, m_bookId);
        m_numberedCopyId = copies.at(0);
        m_numberlessCopyId = copies.at(1);
        ASSERT_TRUE(m_db->exec("UPDATE book_copies SET local_id = NULL, global_copy_id = NULL "
                               "WHERE id = " + std::to_string(m_numberlessCopyId)));

        const std::int64_t liveBook = seedBook(*m_db, uniqueBookSeed(2));
        const std::int64_t borrower = seedMember(*m_db, uniqueMemberSeed(2));
        m_loanId = rawInsertLoan(*m_db, borrower, copyIdsOf(*m_db, liveBook).front(),
                                 "2026-09-01", "2026-09-15", "2026-09-10");
        ASSERT_TRUE(m_circulation->archiveLoan(m_loanId));

        m_page = std::make_unique<ArchivePage>(*m_members, *m_catalog, *m_circulation);
        m_typeList = m_page->findChild<QListWidget*>(QStringLiteral("archiveType"));
        m_table = m_page->findChild<QTableWidget*>();
        m_restore = buttonWithText(m_page.get(), QStringLiteral("Restore"));
        m_reuse = buttonWithText(m_page.get(), QStringLiteral("Reuse local number"));
        m_purge = buttonWithText(m_page.get(), QStringLiteral("Permanently remove"));
        ASSERT_NE(m_typeList, nullptr);
        ASSERT_NE(m_table, nullptr);
        ASSERT_NE(m_restore, nullptr);
        ASSERT_NE(m_reuse, nullptr);
        ASSERT_NE(m_purge, nullptr);
    }

    void TearDown() override
    {
        m_page.reset();
        m_circulation.reset();
        m_catalog.reset();
        m_members.reset();
        m_db.reset();
    }

    int rowOf(qint64 id) const
    {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (m_table->item(row, 0)->data(Qt::UserRole).toLongLong() == id) {
                return row;
            }
        }
        return -1;
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<MemberRepository> m_members;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::unique_ptr<ArchivePage> m_page;
    QListWidget* m_typeList = nullptr;
    QTableWidget* m_table = nullptr;
    QPushButton* m_restore = nullptr;
    QPushButton* m_reuse = nullptr;
    QPushButton* m_purge = nullptr;
    std::int64_t m_memberId = 0;
    std::int64_t m_bookId = 0;
    std::int64_t m_numberedCopyId = 0;
    std::int64_t m_numberlessCopyId = 0;
    std::int64_t m_loanId = 0;
};

TEST_F(test_ui_ArchivePage, TypeFilterOffersFourTypesAndStartsOnMembers)
{
    ASSERT_EQ(m_typeList->count(), 4);
    EXPECT_EQ(m_typeList->item(0)->data(Qt::UserRole).toString(), QStringLiteral("members"));
    EXPECT_EQ(m_typeList->item(3)->data(Qt::UserRole).toString(), QStringLiteral("loans"));
    EXPECT_EQ(m_page->currentType(), ArchivePage::Type::Members);
    EXPECT_EQ(m_table->columnCount(), 6);
    EXPECT_EQ(m_table->horizontalHeaderItem(4)->text(), QStringLiteral("Loans"));
    EXPECT_EQ(m_table->horizontalHeaderItem(5)->text(), QStringLiteral("Archived"));
    ASSERT_EQ(m_table->rowCount(), 1);
    EXPECT_EQ(rowOf(m_memberId), 0);
}

TEST_F(test_ui_ArchivePage, EachTypeListsOnlyItsArchivedRowsWithItsOwnColumns)
{
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("books")));
    EXPECT_EQ(m_page->currentType(), ArchivePage::Type::Books);
    EXPECT_EQ(m_table->columnCount(), 4);
    EXPECT_EQ(m_table->horizontalHeaderItem(0)->text(), QStringLiteral("Title"));
    ASSERT_EQ(m_table->rowCount(), 1);
    EXPECT_EQ(rowOf(m_bookId), 0);
    EXPECT_EQ(m_table->item(0, 2)->text(), QStringLiteral("2"));

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    EXPECT_EQ(m_table->rowCount(), 2);
    EXPECT_GE(rowOf(m_numberedCopyId), 0);
    EXPECT_GE(rowOf(m_numberlessCopyId), 0);

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("loans")));
    ASSERT_EQ(m_table->rowCount(), 1);
    EXPECT_EQ(rowOf(m_loanId), 0);
}

TEST_F(test_ui_ArchivePage, RestoreIsEnabledOnlyWithASelection)
{
    EXPECT_TRUE(m_restore->isEnabled());  // first row selected on load
    m_table->clearSelection();
    QApplication::processEvents();
    EXPECT_FALSE(m_restore->isEnabled());
    m_table->selectRow(0);
    EXPECT_TRUE(m_restore->isEnabled());
}

TEST_F(test_ui_ArchivePage, ReuseShowsOnlyOnCopiesAndNeedsANumber)
{
    EXPECT_TRUE(m_reuse->isHidden());

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    EXPECT_FALSE(m_reuse->isHidden());
    m_table->selectRow(rowOf(m_numberedCopyId));
    EXPECT_TRUE(m_reuse->isEnabled());
    m_table->selectRow(rowOf(m_numberlessCopyId));
    EXPECT_FALSE(m_reuse->isEnabled());

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("loans")));
    EXPECT_TRUE(m_reuse->isHidden());
}

TEST_F(test_ui_ArchivePage, ReuseHidesOnLoansWhenTheSelectedRowIndexStaysTheSame)
{
    // Copies opens on row 0 and so does Loans: the table is rebuilt but the
    // selection signal never fires, which left Reuse showing on Loans.
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    m_table->selectRow(0);
    ASSERT_FALSE(m_reuse->isHidden());

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("loans")));
    EXPECT_TRUE(m_reuse->isHidden());
}

TEST_F(test_ui_ArchivePage, ReuseComesBeforeRestoreOnTheButtonPad)
{
    auto* pad = m_reuse->parentWidget();
    ASSERT_EQ(pad, m_restore->parentWidget());
    const int reuseAt = pad->layout()->indexOf(m_reuse);
    const int restoreAt = pad->layout()->indexOf(m_restore);
    const int purgeAt = pad->layout()->indexOf(m_purge);
    EXPECT_LT(reuseAt, restoreAt);
    EXPECT_LT(restoreAt, purgeAt);
}

TEST_F(test_ui_ArchivePage, RestoreSendsTheMemberBackToTheLiveList)
{
    QSignalSpy restored(m_page.get(), &ArchivePage::recordRestored);
    answerNextBoxYes();
    m_restore->click();

    EXPECT_EQ(m_table->rowCount(), 0);
    EXPECT_TRUE(m_members->getMember(m_memberId)->archivedAt.empty());
    EXPECT_EQ(restored.count(), 1);
}

TEST_F(test_ui_ArchivePage, ReuseOpensTheChooserAndCancelKeepsTheNumber)
{
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    m_table->selectRow(rowOf(m_numberedCopyId));
    const std::string number =
        m_db->scalar("SELECT local_id FROM book_copies WHERE id = " + std::to_string(m_numberedCopyId))
            .toString();
    QSignalSpy requested(m_page.get(), &ArchivePage::reuseNumberRequested);

    // On the stack, so a dialog that never opens cannot leave the poll running
    // into the next test.
    bool chooserOpened = false;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr || !dialog->isVisible()) {
            return;
        }
        poll.stop();
        chooserOpened = qobject_cast<ReuseBookChooser*>(dialog) != nullptr;
        dialog->reject();
    });
    poll.start();
    m_reuse->click();
    poll.stop();

    EXPECT_TRUE(chooserOpened);
    ASSERT_EQ(requested.count(), 1);
    EXPECT_EQ(requested.at(0).at(0).toLongLong(), m_numberedCopyId);
    EXPECT_EQ(m_db->scalar("SELECT local_id FROM book_copies WHERE id = "
                           + std::to_string(m_numberedCopyId))
                  .toString(),
              number);
    EXPECT_GE(rowOf(m_numberedCopyId), 0);  // still archived, still listed
}

TEST(test_ui_ArchiveNavigation, TheHeaderHasAnArchiveButtonThatOpensTheArchive)
{
    // Without an Application instance MainWindow builds placeholder pages,
    // which is enough to check the button and the page it opens.
    Locale::setCode("en");
    MainWindow window;
    QPushButton* nav = nullptr;
    for (QPushButton* button : window.findChildren<QPushButton*>()) {
        if (button->property("navKey").toString() == QStringLiteral("archive")) {
            nav = button;
        }
    }
    ASSERT_NE(nav, nullptr);
    EXPECT_EQ(nav->text(), QStringLiteral("Archive"));

    nav->click();
    auto* stack = window.findChild<QStackedWidget*>();
    ASSERT_NE(stack, nullptr);
    bool showsArchive = false;
    for (const QLabel* label : stack->currentWidget()->findChildren<QLabel*>()) {
        if (label->property("i18nKey").toString() == QStringLiteral("page.archive.title")) {
            showsArchive = true;
        }
    }
    EXPECT_TRUE(showsArchive);
    Locale::setCode(Locale::kDefaultCode);
}

TEST_F(test_ui_ArchivePage, EveryTypeStaysVisibleWhateverTypeIsPicked)
{
    // The list has no scrollbar, so a row that does not fit is simply gone:
    // picking Loans used to scroll Members off the top. It showed with the
    // theme's frame and a desktop-sized font (31px rows), so use both here.
    qApp->setStyleSheet(VLMS::applicationStylesheet());
    QFont big = m_typeList->font();
    big.setPixelSize(22);
    m_typeList->setFont(big);
    m_page->retranslateUi();
    m_page->resize(1200, 800);
    m_page->show();
    QApplication::processEvents();

    for (int picked = 0; picked < m_typeList->count(); ++picked) {
        ASSERT_TRUE(selectType(m_typeList, m_typeList->item(picked)->data(Qt::UserRole).toString()));
        const QRect viewport = m_typeList->viewport()->rect();
        for (int row = 0; row < m_typeList->count(); ++row) {
            EXPECT_TRUE(viewport.contains(m_typeList->visualItemRect(m_typeList->item(row))))
                << "row " << row << " hidden after picking row " << picked;
        }
    }
    qApp->setStyleSheet({});
}


TEST_F(test_ui_ArchivePage, TypeFilterSitsAtTheTopOfItsColumn)
{
    m_page->resize(1200, 800);
    m_page->show();
    QApplication::processEvents();

    EXPECT_EQ(m_typeList->y(), 0);
}

TEST_F(test_ui_ArchivePage, PermanentRemovalIsOfferedOnlyWhenTheRowsGateIsOpen)
{
    // Members: the seeded archived member never borrowed, so nothing holds them.
    m_table->selectRow(rowOf(m_memberId));
    EXPECT_TRUE(m_purge->isEnabled());

    // Books: the title still has its two archived copies.
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("books")));
    m_table->selectRow(rowOf(m_bookId));
    EXPECT_FALSE(m_purge->isEnabled());

    // Copies: neither seeded copy was ever borrowed.
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    m_table->selectRow(rowOf(m_numberedCopyId));
    EXPECT_TRUE(m_purge->isEnabled());

    // Loans: nothing sits beneath a loan.
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("loans")));
    m_table->selectRow(rowOf(m_loanId));
    EXPECT_TRUE(m_purge->isEnabled());
}

TEST_F(test_ui_ArchivePage, TheLoansColumnSaysWhyAMemberCannotGo)
{
    const std::int64_t borrower = seedMember(*m_db, uniqueMemberSeed(9));
    const std::int64_t bookId = seedBook(*m_db, uniqueBookSeed(9));
    const std::int64_t loanId = rawInsertLoan(*m_db, borrower, copyIdsOf(*m_db, bookId).front(),
                                              "2026-09-01", "2026-09-15", "2026-09-10");
    ASSERT_GT(loanId, 0);
    ASSERT_TRUE(m_circulation->archiveLoan(loanId));
    ASSERT_TRUE(m_members->archiveMember(borrower));
    m_page->refresh();

    const int row = rowOf(borrower);
    ASSERT_GE(row, 0);
    // The column counts the archived loan, because the archived loan blocks.
    EXPECT_EQ(m_table->item(row, 4)->text(), QStringLiteral("1"));
    m_table->selectRow(row);
    EXPECT_FALSE(m_purge->isEnabled());

    // And the one who never borrowed still reads 0 and still goes.
    m_table->selectRow(rowOf(m_memberId));
    EXPECT_EQ(m_table->item(rowOf(m_memberId), 4)->text(), QStringLiteral("0"));
    EXPECT_TRUE(m_purge->isEnabled());
}

TEST_F(test_ui_ArchivePage, PermanentlyRemovingACopyTakesItOffTheListAndLeavesTheOther)
{
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    ASSERT_EQ(m_table->rowCount(), 2);
    m_table->selectRow(rowOf(m_numberedCopyId));

    answerNextBoxYes();
    m_purge->click();
    QApplication::processEvents();

    EXPECT_EQ(m_table->rowCount(), 1);
    EXPECT_EQ(rowOf(m_numberedCopyId), -1);
    EXPECT_GE(rowOf(m_numberlessCopyId), 0);
}

namespace {

void tickId(QTableWidget* table, qint64 id)
{
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem* item = table->item(row, 0);
        if (item != nullptr && item->data(Qt::UserRole).toLongLong() == id) {
            item->setCheckState(Qt::Checked);
            return;
        }
    }
    FAIL() << "row " << id << " is not on the archive page";
}

}  // namespace

TEST_F(test_ui_ArchivePage, TwoTickedMembersAreRestoredAndTheSignalFiresOnce)
{
    const std::int64_t second = seedMember(*m_db, uniqueMemberSeed(9));
    ASSERT_TRUE(m_members->archiveMember(second));
    m_page->refresh();
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("members")));
    ASSERT_GE(rowOf(m_memberId), 0);
    ASSERT_GE(rowOf(second), 0);
    tickId(m_table, m_memberId);
    tickId(m_table, second);

    QSignalSpy spy(m_page.get(), &ArchivePage::recordRestored);
    const ModalOutcome outcome =
        runAndAnswerModal([this]() { m_restore->click(); }, qs(Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, QStringLiteral("Restore 2 members?"));
    EXPECT_EQ(spy.count(), 1);
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId))
                    .isNull());
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(second))
                    .isNull());
}

TEST_F(test_ui_ArchivePage, NothingTickedRestoresOnlyTheHighlightedMember)
{
    const std::int64_t second = seedMember(*m_db, uniqueMemberSeed(8));
    ASSERT_TRUE(m_members->archiveMember(second));
    m_page->refresh();
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("members")));
    m_table->selectRow(rowOf(m_memberId));

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { m_restore->click(); }, qs(Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(outcome.text, qs(Strings::t("archive.restoreConfirm")));
    EXPECT_TRUE(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(m_memberId))
                    .isNull());
    EXPECT_FALSE(m_db->scalar("SELECT archived_at FROM members WHERE id = " + std::to_string(second))
                     .isNull());
}

TEST_F(test_ui_ArchivePage, TwoTickedLoansArePurgedAndTheThirdStays)
{
    const std::int64_t book = seedBook(*m_db, uniqueBookSeed(8));
    const std::int64_t member = seedMember(*m_db, uniqueMemberSeed(7));
    const std::int64_t second =
        rawInsertLoan(*m_db, member, copyIdsOf(*m_db, book).front(), "2026-08-01", "2026-08-15",
                      "2026-08-10");
    ASSERT_TRUE(m_circulation->archiveLoan(second));
    const std::int64_t book3 = seedBook(*m_db, uniqueBookSeed(9));
    const std::int64_t third =
        rawInsertLoan(*m_db, member, copyIdsOf(*m_db, book3).front(), "2026-07-01", "2026-07-15",
                      "2026-07-10");
    ASSERT_TRUE(m_circulation->archiveLoan(third));
    m_page->refresh();
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("loans")));
    tickId(m_table, m_loanId);
    tickId(m_table, second);
    m_table->selectRow(rowOf(third));

    const ModalOutcome outcome =
        runAndAnswerModal([this]() { m_purge->click(); }, qs(Strings::t("common.yes")));

    ASSERT_TRUE(outcome.appeared);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM loans WHERE id = " + std::to_string(m_loanId)).toInt(),
              0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM loans WHERE id = " + std::to_string(second)).toInt(), 0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM loans WHERE id = " + std::to_string(third)).toInt(), 1);
}

TEST_F(test_ui_ArchivePage, ATickedBookWithCopiesIsRefusedAndALoanFreeCopyIsPurged)
{
    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("books")));
    tickId(m_table, m_bookId);

    const ModalOutcome blocked =
        runAndAnswerModal([this]() { m_purge->click(); }, qs(Strings::t("common.ok")));
    ASSERT_TRUE(blocked.appeared);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM books WHERE id = " + std::to_string(m_bookId)).toInt(),
              1);

    ASSERT_TRUE(selectType(m_typeList, QStringLiteral("copies")));
    tickId(m_table, m_numberedCopyId);
    const ModalOutcome purged =
        runAndAnswerModal([this]() { m_purge->click(); }, qs(Strings::t("common.yes")));
    ASSERT_TRUE(purged.appeared);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = "
                           + std::to_string(m_numberedCopyId))
                  .toInt(),
              0);
    EXPECT_EQ(m_db->scalar("SELECT COUNT(*) FROM book_copies WHERE id = "
                           + std::to_string(m_numberlessCopyId))
                  .toInt(),
              1);
}
