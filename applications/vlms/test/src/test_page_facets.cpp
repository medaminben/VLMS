#include "TestDatabase.h"
#include "TestSeed.h"

#include "QtBridge.h"
#include "ui/Theme.h"
#include "ui/archive/ArchivePage.h"
#include "ui/catalog/BookFacetFilters.h"
#include "ui/circulation/CirculationPage.h"
#include "ui/members/MemberFacetFilters.h"
#include "ui/members/MembersPage.h"

#include <VLMS/Repositories/CatalogRepository.h>
#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <QTableWidget>

#include <gtest/gtest.h>

#include <memory>

using VLMS::Locale;
using namespace VLMS;
using namespace Test;

namespace {

bool pick(QListWidget* list, const QString& code)
{
    if (list == nullptr) {
        return false;
    }
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

template <typename T>
T* named(QWidget* root, const char* name)
{
    return root->findChild<T*>(QString::fromLatin1(name));
}

}  // namespace

/// The Members page's filters on every page that lists members or loans, the
/// Catalogue's on the Archive's titles and copies, and the loan viewers' two
/// images.
class test_ui_PageFacets : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_catalog = std::make_unique<Repositories::CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());

        MemberSeed male = uniqueMemberSeed(1);
        male.sex = Repositories::MemberSex::kMale;
        m_maleId = seedMember(*m_db, male);
        MemberSeed female = uniqueMemberSeed(2);
        female.sex = Repositories::MemberSex::kFemale;
        m_femaleId = seedMember(*m_db, female);
        ASSERT_GT(m_maleId, 0);
        ASSERT_GT(m_femaleId, 0);

        BookSeed arabic = uniqueBookSeed(1);
        arabic.language = "ar";
        m_arabicBookId = seedBook(*m_db, arabic);
        BookSeed french = uniqueBookSeed(2);
        french.language = "fr";
        m_frenchBookId = seedBook(*m_db, french);
        ASSERT_GT(m_arabicBookId, 0);
        ASSERT_GT(m_frenchBookId, 0);

        m_maleLoan = rawInsertLoan(*m_db, m_maleId, copyIdsOf(*m_db, m_arabicBookId).front(),
                                   "2026-09-01", "2026-09-15", "2026-09-10");
        m_femaleLoan = rawInsertLoan(*m_db, m_femaleId, copyIdsOf(*m_db, m_frenchBookId).front(),
                                     "2026-09-02", "2026-09-16", "2026-09-11");
        ASSERT_GT(m_maleLoan, 0);
        ASSERT_GT(m_femaleLoan, 0);
    }

    void TearDown() override
    {
        m_circulation.reset();
        m_catalog.reset();
        m_members.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CatalogRepository> m_catalog;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::int64_t m_maleId = 0;
    std::int64_t m_femaleId = 0;
    std::int64_t m_arabicBookId = 0;
    std::int64_t m_frenchBookId = 0;
    std::int64_t m_maleLoan = 0;
    std::int64_t m_femaleLoan = 0;
};

TEST_F(test_ui_PageFacets, MemberFiltersCarryNoHeadings)
{
    MembersPage page(*m_members, *m_circulation);
    auto* filters = named<QWidget>(&page, "memberFilters");
    ASSERT_NE(filters, nullptr);
    EXPECT_TRUE(filters->findChildren<QLabel*>().isEmpty())
        << "each list's own All row names it";
}

TEST_F(test_ui_PageFacets, CirculationFiltersLoansByTheBorrowersSex)
{
    CirculationPage page(*m_circulation, *m_catalog, *m_members);
    auto* table = named<QTableWidget>(&page, "listTable");
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 2);

    ASSERT_TRUE(pick(named<QListWidget>(&page, "sexFilter"), QString::fromLatin1(Repositories::MemberSex::kFemale)));
    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), m_femaleLoan);
}

TEST_F(test_ui_PageFacets, CirculationShowsTheCoverBesideTheBorrowersPhoto)
{
    CirculationPage page(*m_circulation, *m_catalog, *m_members);
    auto* cover = named<QLabel>(&page, "bookCover");
    auto* photo = named<QLabel>(&page, "loanMemberPhoto");
    ASSERT_NE(cover, nullptr);
    ASSERT_NE(photo, nullptr);
    EXPECT_FALSE(photo->isHidden());
    EXPECT_FALSE(cover->pixmap().isNull()) << "the placeholder cover when a title has none";
    EXPECT_EQ(photo->text(), VLMS::T("members.noPhoto"));
    EXPECT_EQ(photo->size(), cover->size()) << "the two boxes are one size";
}

TEST_F(test_ui_PageFacets, ArchiveShowsTheFiltersOfTheTypeOnScreen)
{
    ArchivePage page(*m_members, *m_catalog, *m_circulation);
    auto* types = named<QListWidget>(&page, "archiveType");
    auto* memberFilters = page.findChild<VLMS::MemberFacetFilters*>();
    auto* bookFilters = page.findChild<VLMS::BookFacetFilters*>();
    auto* photo = named<QLabel>(&page, "archiveMemberPhoto");
    ASSERT_NE(types, nullptr);
    ASSERT_NE(memberFilters, nullptr);
    ASSERT_NE(bookFilters, nullptr);
    ASSERT_NE(photo, nullptr);

    EXPECT_FALSE(memberFilters->isHidden());
    EXPECT_TRUE(bookFilters->isHidden());
    EXPECT_TRUE(photo->isHidden()) << "a member has one image, their photo";

    for (const char* code : {"books", "copies"}) {
        ASSERT_TRUE(pick(types, QString::fromLatin1(code)));
        EXPECT_TRUE(memberFilters->isHidden()) << code;
        EXPECT_FALSE(bookFilters->isHidden()) << code;
        EXPECT_TRUE(photo->isHidden()) << code;
    }

    ASSERT_TRUE(pick(types, QStringLiteral("loans")));
    EXPECT_FALSE(memberFilters->isHidden());
    EXPECT_TRUE(bookFilters->isHidden());
    EXPECT_FALSE(photo->isHidden()) << "a loan shows the cover and the borrower";
    EXPECT_EQ(photo->size(), named<QLabel>(&page, "archiveImage")->size());
}

TEST_F(test_ui_PageFacets, ArchiveFiltersCopiesByTheirTitlesLanguage)
{
    ASSERT_TRUE(m_catalog->saveCopies(m_arabicBookId, {}));
    ASSERT_TRUE(m_catalog->saveCopies(m_frenchBookId, {}));

    ArchivePage page(*m_members, *m_catalog, *m_circulation);
    auto* table = named<QTableWidget>(&page, "listTable");
    ASSERT_TRUE(pick(named<QListWidget>(&page, "archiveType"), QStringLiteral("copies")));
    ASSERT_EQ(table->rowCount(), 2);

    auto* bookFilters = page.findChild<VLMS::BookFacetFilters*>();
    ASSERT_NE(bookFilters, nullptr);
    ASSERT_TRUE(pick(named<QListWidget>(bookFilters, "languageFilter"), QStringLiteral("fr")));
    ASSERT_EQ(table->rowCount(), 1);
}

TEST_F(test_ui_PageFacets, ArchiveFiltersLoansByTheBorrowersSex)
{
    ASSERT_TRUE(m_circulation->archiveLoan(m_maleLoan));
    ASSERT_TRUE(m_circulation->archiveLoan(m_femaleLoan));

    ArchivePage page(*m_members, *m_catalog, *m_circulation);
    auto* table = named<QTableWidget>(&page, "listTable");
    ASSERT_TRUE(pick(named<QListWidget>(&page, "archiveType"), QStringLiteral("loans")));
    ASSERT_EQ(table->rowCount(), 2);

    ASSERT_TRUE(pick(named<QListWidget>(&page, "sexFilter"), QString::fromLatin1(Repositories::MemberSex::kMale)));
    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), m_maleLoan);
}

TEST(test_ui_ManualButtonIcon, RightToLeftDrawsTheArabicQuestionMark)
{
    using VLMS::ThemeMode;
    const QImage ltr = VLMS::manualButtonIcon(ThemeMode::Light, 36, 1.0, Qt::LeftToRight)
                           .pixmap(36, 36).toImage();
    const QImage rtl = VLMS::manualButtonIcon(ThemeMode::Light, 36, 1.0, Qt::RightToLeft)
                           .pixmap(36, 36).toImage();
    ASSERT_FALSE(ltr.isNull());
    ASSERT_FALSE(rtl.isNull());
    EXPECT_NE(ltr, rtl) << "right to left must not draw the Latin ?";
}

TEST_F(test_ui_PageFacets, ArchivedLoanDetailsShowTheMembershipNumberAndNotes)
{
    ASSERT_TRUE(m_db->exec("UPDATE loans SET notes = 'Returned with a torn cover' WHERE id = "
                           + std::to_string(m_femaleLoan)));
    ASSERT_TRUE(m_circulation->archiveLoan(m_femaleLoan));
    const auto loan = m_circulation->getLoan(m_femaleLoan);
    ASSERT_TRUE(loan.has_value());

    ArchivePage page(*m_members, *m_catalog, *m_circulation);
    ASSERT_TRUE(pick(named<QListWidget>(&page, "archiveType"), QStringLiteral("loans")));
    auto* table = named<QTableWidget>(&page, "listTable");
    ASSERT_EQ(table->rowCount(), 1);

    QStringList texts;
    for (const QLabel* label : page.findChildren<QLabel*>()) {
        texts.append(label->text());
    }
    EXPECT_TRUE(texts.contains(VLMS::T("member.field.number")));
    EXPECT_TRUE(texts.contains(VLMS::qs(loan->membershipNumber)));
    EXPECT_TRUE(texts.contains(VLMS::T("loan.field.notes")));
    // One Notes field, a scrolling box as in Circulation.
    int notesLabels = 0;
    for (const QString& text : texts) {
        notesLabels += text == VLMS::T("loan.field.notes") ? 1 : 0;
    }
    EXPECT_EQ(notesLabels, 1);
    const auto boxes = page.findChildren<QPlainTextEdit*>(QStringLiteral("bookDetailValue"));
    ASSERT_EQ(boxes.size(), 1);
    EXPECT_EQ(boxes.front()->toPlainText(), QStringLiteral("Returned with a torn cover"));
}

namespace {

QPushButton* archiveLoansButton(QWidget* page)
{
    for (QPushButton* button : page->findChildren<QPushButton*>()) {
        if (button->text() == VLMS::T("archive.loans")) {
            return button;
        }
    }
    return nullptr;
}

}  // namespace

TEST_F(test_ui_PageFacets, ArchiveLoansButtonFollowsTheTypeAndTheHistory)
{
    ASSERT_TRUE(m_members->archiveMember(m_maleId));
    // A title nobody has borrowed, archived with its one copy.
    const std::int64_t unread = seedBook(*m_db, uniqueBookSeed(3));
    ASSERT_TRUE(m_catalog->saveCopies(unread, {}));

    ArchivePage page(*m_members, *m_catalog, *m_circulation);
    auto* types = named<QListWidget>(&page, "archiveType");
    QPushButton* loans = archiveLoansButton(&page);
    ASSERT_NE(loans, nullptr);

    EXPECT_FALSE(loans->isHidden());
    EXPECT_TRUE(loans->isEnabled()) << "the archived member borrowed once";

    ASSERT_TRUE(pick(types, QStringLiteral("copies")));
    EXPECT_FALSE(loans->isHidden());
    EXPECT_FALSE(loans->isEnabled()) << "that copy was never lent";

    ASSERT_TRUE(pick(types, QStringLiteral("loans")));
    EXPECT_TRUE(loans->isHidden()) << "a loan is its own history";
}

TEST_F(test_ui_PageFacets, ArchiveLoansButtonOpensTheMembersHistory)
{
    ASSERT_TRUE(m_members->archiveMember(m_maleId));
    ArchivePage page(*m_members, *m_catalog, *m_circulation);
    QPushButton* loans = archiveLoansButton(&page);
    ASSERT_NE(loans, nullptr);
    ASSERT_TRUE(loans->isEnabled());

    int rows = -1;
    QString title;
    QTimer::singleShot(0, [&rows, &title]() {
        QWidget* modal = nullptr;
        for (int attempt = 0; attempt < 100 && modal == nullptr; ++attempt) {
            modal = QApplication::activeModalWidget();
            if (modal == nullptr) {
                QTest::qWait(10);
            }
        }
        if (modal == nullptr) {
            return;
        }
        title = modal->windowTitle();
        if (auto* table = modal->findChild<QTableWidget*>(QStringLiteral("archiveLoanHistory"))) {
            rows = table->rowCount();
        }
        modal->close();
    });
    loans->click();

    EXPECT_EQ(rows, 1) << "the one loan the member ever had";
    EXPECT_FALSE(title.isEmpty());
}

TEST_F(test_ui_PageFacets, CirculationYearIsTheYearOfTheLoan)
{
    const std::int64_t older = rawInsertLoan(*m_db, m_femaleId,
                                             copyIdsOf(*m_db, m_arabicBookId).front(),
                                             "2024-05-01", "2024-05-15", "2024-05-10");
    ASSERT_GT(older, 0);

    CirculationPage page(*m_circulation, *m_catalog, *m_members);
    auto* table = named<QTableWidget>(&page, "listTable");
    auto* years = named<QListWidget>(&page, "yearFilter");
    ASSERT_NE(years, nullptr);
    ASSERT_EQ(table->rowCount(), 3);
    EXPECT_EQ(years->item(0)->text(), VLMS::T("circulation.allLoanYears"));

    // Nobody registered in 2024; a loan was made then.
    ASSERT_TRUE(pick(years, QStringLiteral("2024")));
    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), older);
}

TEST_F(test_ui_PageFacets, MembersYearIsStillTheYearOfRegistration)
{
    MembersPage page(*m_members, *m_circulation);
    auto* years = named<QListWidget>(&page, "yearFilter");
    ASSERT_NE(years, nullptr);
    EXPECT_EQ(years->item(0)->text(), VLMS::T("members.allYears"));
}
