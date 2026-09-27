#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/circulation/CirculationPage.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/MemberRepository.h>
#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Core/LoanTypes.h>

#include <QApplication>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <gtest/gtest.h>

#include <memory>

using VLMS::Date;
using VLMS::Locale;
using VLMS::ScopedClock;
using namespace VLMS::Test;

namespace {

bool selectFilterCode(QListWidget* list, const QString& code)
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

QStringList filterCodesOnList(QListWidget* list)
{
    QStringList codes;
    if (list == nullptr) {
        return codes;
    }
    for (int row = 0; row < list->count(); ++row) {
        codes.append(list->item(row)->data(Qt::UserRole).toString());
    }
    return codes;
}

QStringList selectedFilterCodes(QListWidget* list)
{
    QStringList codes;
    if (list == nullptr) {
        return codes;
    }
    for (const QListWidgetItem* item : list->selectedItems()) {
        codes.append(item->data(Qt::UserRole).toString());
    }
    return codes;
}

}  // namespace

class test_ui_CirculationFilters : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Locale::setCode("en"); }

    void SetUp() override
    {
        // Pinned for the whole test, not just the seeding: which loans are
        // Open and which are Overdue depends on what day the page thinks it is.
        m_clock = std::make_unique<ScopedClock>(Date(2026, 9, 19));
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_catalog = std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_memberRepo =
            std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_page.reset();
        m_circulation.reset();
        m_memberRepo.reset();
        m_catalog.reset();
        m_db.reset();
        m_clock.reset();
    }

    void seedOpenOverdueAndReturned()
    {
        MemberSeed member = uniqueMemberSeed(1);
        member.status = MemberStatus::kActive;
        m_memberId = seedMember(*m_db, member);
        ASSERT_GT(m_memberId, 0);

        BookSeed book = uniqueBookSeed(1);
        book.initialCopyCount = 3;
        const std::int64_t bookId = seedBook(*m_db, book);
        ASSERT_GT(bookId, 0);
        const auto copies = copyIdsOf(*m_db, bookId);
        ASSERT_EQ(copies.size(), 3u);

        const Date today = Date(2026, 9, 19);
        m_openId = rawInsertLoan(*m_db, m_memberId, copies.at(0),
                                 today.addDays(-2).toIso(), today.addDays(12).toIso());
        m_overdueId = rawInsertLoan(*m_db, m_memberId, copies.at(1),
                                    today.addDays(-40).toIso(), today.addDays(-26).toIso());
        m_returnedId = rawInsertLoan(*m_db, m_memberId, copies.at(2),
                                     today.addDays(-60).toIso(), today.addDays(-46).toIso(),
                                     today.addDays(-50).toIso());
        ASSERT_GT(m_openId, 0);
        ASSERT_GT(m_overdueId, 0);
        ASSERT_GT(m_returnedId, 0);
    }

    std::unique_ptr<ScopedClock> m_clock;
    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<MemberRepository> m_memberRepo;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::unique_ptr<CirculationPage> m_page;
    std::int64_t m_memberId = 0;
    std::int64_t m_openId = 0;
    std::int64_t m_overdueId = 0;
    std::int64_t m_returnedId = 0;
};

TEST_F(test_ui_CirculationFilters, FilterListIncludesEveryStateAndDefaultsToAll)
{
    seedOpenOverdueAndReturned();
    m_page = std::make_unique<CirculationPage>(*m_circulation, *m_catalog, *m_memberRepo);

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    ASSERT_NE(list, nullptr);
    const QStringList codes = filterCodesOnList(list);
    EXPECT_TRUE(codes.contains(QString::fromLatin1(LoanFilter::kOpen)));
    EXPECT_TRUE(codes.contains(QString::fromLatin1(LoanFilter::kOverdue)));
    EXPECT_TRUE(codes.contains(QString::fromLatin1(LoanFilter::kReturned)));

    // All is the first row and carries an empty code.
    ASSERT_EQ(list->selectedItems().size(), 1);
    EXPECT_EQ(list->row(list->selectedItems().first()), 0);
    EXPECT_TRUE(list->selectedItems().first()->data(Qt::UserRole).toString().isEmpty());

    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 3);
}

TEST_F(test_ui_CirculationFilters, SelectingOverdueShowsOnlyLoansPastTheirDueDate)
{
    seedOpenOverdueAndReturned();
    m_page = std::make_unique<CirculationPage>(*m_circulation, *m_catalog, *m_memberRepo);

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(list, nullptr);
    ASSERT_NE(table, nullptr);

    ASSERT_TRUE(selectFilterCode(list, QString::fromLatin1(LoanFilter::kOverdue)));

    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), m_overdueId);
}

TEST_F(test_ui_CirculationFilters, SelectingReturnedShowsOnlyReturnedLoans)
{
    seedOpenOverdueAndReturned();
    m_page = std::make_unique<CirculationPage>(*m_circulation, *m_catalog, *m_memberRepo);

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(list, nullptr);
    ASSERT_NE(table, nullptr);

    ASSERT_TRUE(selectFilterCode(list, QString::fromLatin1(LoanFilter::kReturned)));

    EXPECT_EQ(selectedFilterCodes(list), QStringList{QString::fromLatin1(LoanFilter::kReturned)});
    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), m_returnedId);
}

TEST_F(test_ui_CirculationFilters, SelectingOpenAgainShowsOnlyOpenLoans)
{
    seedOpenOverdueAndReturned();
    m_page = std::make_unique<CirculationPage>(*m_circulation, *m_catalog, *m_memberRepo);

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(list, nullptr);
    ASSERT_NE(table, nullptr);

    ASSERT_TRUE(selectFilterCode(list, QString::fromLatin1(LoanFilter::kReturned)));
    ASSERT_TRUE(selectFilterCode(list, QString::fromLatin1(LoanFilter::kOpen)));

    EXPECT_EQ(selectedFilterCodes(list), QStringList{QString::fromLatin1(LoanFilter::kOpen)});
    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), m_openId);
}

TEST_F(test_ui_CirculationFilters, SelectingAllShowsEveryLoan)
{
    seedOpenOverdueAndReturned();
    m_page = std::make_unique<CirculationPage>(*m_circulation, *m_catalog, *m_memberRepo);

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(list, nullptr);
    ASSERT_NE(table, nullptr);

    bool selectedAll = false;
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem* item = list->item(row);
        if (item->data(Qt::UserRole).toString().isEmpty()
            || item->data(Qt::UserRole).toString() == QLatin1String(LoanFilter::kAll)) {
            list->setCurrentItem(item);
            item->setSelected(true);
            QApplication::processEvents();
            selectedAll = true;
            break;
        }
    }
    ASSERT_TRUE(selectedAll);
    EXPECT_EQ(table->rowCount(), 3);
}

TEST_F(test_ui_CirculationFilters, JumpingFromMembersShowsOverdueLoansToo)
{
    // Members sends the librarian here when a member still has books out.
    // Overdue books are out as well, and are usually the ones that matter.
    seedOpenOverdueAndReturned();
    m_page = std::make_unique<CirculationPage>(*m_circulation, *m_catalog, *m_memberRepo);
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);

    const std::string number =
        m_db->scalar("SELECT membership_number FROM members WHERE id = " + std::to_string(m_memberId))
            .toString();
    m_page->focusMemberLoans(QString::fromStdString(number));

    QList<qint64> listed;
    for (int row = 0; row < table->rowCount(); ++row) {
        listed.append(table->item(row, 0)->data(Qt::UserRole).toLongLong());
    }
    EXPECT_TRUE(listed.contains(m_openId));
    EXPECT_TRUE(listed.contains(m_overdueId));
    ASSERT_GE(listed.size(), 2);
    // Still out comes first; history (returned) after.
    EXPECT_NE(listed.at(0), m_returnedId);
    EXPECT_NE(listed.at(1), m_returnedId);
}

TEST_F(test_ui_CirculationFilters, FilterSitsAtTheTopOfItsColumn)
{
    seedOpenOverdueAndReturned();
    m_page = std::make_unique<CirculationPage>(*m_circulation, *m_catalog, *m_memberRepo);
    m_page->resize(1200, 800);
    m_page->show();
    QApplication::processEvents();

    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("loanFilter"));
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->y(), 0);
}

TEST_F(test_ui_CirculationFilters, ReturnSitsBetweenTheSearchAndCheckout)
{
    m_page = std::make_unique<CirculationPage>(*m_circulation, *m_catalog, *m_memberRepo);
    QPushButton* returnButton = nullptr;
    QPushButton* checkoutButton = nullptr;
    for (QPushButton* button : m_page->findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("Return")) {
            returnButton = button;
        } else if (button->text() == QStringLiteral("Checkout")) {
            checkoutButton = button;
        }
    }
    ASSERT_NE(returnButton, nullptr);
    ASSERT_NE(checkoutButton, nullptr);
    QLayout* pad = returnButton->parentWidget()->layout();
    ASSERT_EQ(checkoutButton->parentWidget()->layout(), pad);
    auto* search = m_page->findChild<QLineEdit*>(QStringLiteral("listSearch"));
    ASSERT_NE(search, nullptr);
    EXPECT_EQ(pad->indexOf(search), 0);
    EXPECT_EQ(pad->indexOf(returnButton), 1);
    EXPECT_EQ(pad->indexOf(checkoutButton), 2);
}
