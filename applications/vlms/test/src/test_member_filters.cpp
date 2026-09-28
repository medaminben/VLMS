#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/members/MembersPage.h"
#include "ui/TablePager.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

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

QString tableText(QTableWidget* table, int row, int column)
{
    QTableWidgetItem* item = table->item(row, column);
    return item == nullptr ? QString() : item->text();
}

}  // namespace

class test_ui_MemberFilters : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_members = std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
        m_circulation = std::make_unique<Repositories::CirculationRepository>(m_db->session());
    }

    void TearDown() override
    {
        m_page.reset();
        m_circulation.reset();
        m_members.reset();
        m_db.reset();
    }

    void seedTunisAndSfax()
    {
        // Pinned so the 2010-born seed is youth on create whatever year this runs.
        const Core::ScopedClock pinned(Core::Date(2026, 9, 19));

        MemberSeed male = uniqueMemberSeed(1);
        male.sex = Repositories::MemberSex::kMale;
        male.city = "Tunis";
        male.occupation = "قاضي";
        m_maleId = seedMember(*m_db, male);
        ASSERT_GT(m_maleId, 0);
        ASSERT_TRUE(rawSetRegisteredAt(*m_db, m_maleId, "2019-03-01 10:00:00"));

        MemberSeed female = uniqueMemberSeed(2);
        female.sex = Repositories::MemberSex::kFemale;
        female.city = "Sfax";
        female.occupation = "تلميذة";
        female.dateOfBirth = "2010-06-15";
        m_femaleId = seedMember(*m_db, female);
        ASSERT_GT(m_femaleId, 0);
        ASSERT_TRUE(rawSetRegisteredAt(*m_db, m_femaleId, "2024-06-15 09:00:00"));
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<MembersPage> m_page;
    qint64 m_maleId = 0;
    qint64 m_femaleId = 0;
};

TEST_F(test_ui_MemberFilters, FilterListsArePresent)
{
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);

    EXPECT_NE(m_page->findChild<QListWidget*>(QStringLiteral("statusFilter")), nullptr);
    EXPECT_NE(m_page->findChild<QListWidget*>(QStringLiteral("sexFilter")), nullptr);
    EXPECT_NE(m_page->findChild<QListWidget*>(QStringLiteral("yearFilter")), nullptr);
    EXPECT_EQ(m_page->findChild<QListWidget*>(QStringLiteral("occupationFilter")), nullptr);
    EXPECT_NE(m_page->findChild<QListWidget*>(QStringLiteral("ageGroupFilter")), nullptr);
    EXPECT_NE(m_page->findChild<QListWidget*>(QStringLiteral("cityFilter")), nullptr);
}

TEST_F(test_ui_MemberFilters, SexFilterKeepsOnlyTheMatchingRow)
{
    seedTunisAndSfax();
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);

    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 2);

    ASSERT_TRUE(selectFilterCode(m_page->findChild<QListWidget*>(QStringLiteral("sexFilter")),
                                 QString::fromLatin1(Repositories::MemberSex::kMale)));

    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(tableText(table, 0, 3), QStringLiteral("Tunis"));
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), m_maleId);
}

TEST_F(test_ui_MemberFilters, CityAndSexFiltersCombineWithAnd)
{
    seedTunisAndSfax();
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);

    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);

    ASSERT_TRUE(selectFilterCode(m_page->findChild<QListWidget*>(QStringLiteral("sexFilter")),
                                 QString::fromLatin1(Repositories::MemberSex::kMale)));
    ASSERT_TRUE(selectFilterCode(m_page->findChild<QListWidget*>(QStringLiteral("cityFilter")),
                                 QStringLiteral("Sfax")));

    EXPECT_EQ(table->rowCount(), 0);
}

TEST_F(test_ui_MemberFilters, SelectingAllClearsTheDimension)
{
    seedTunisAndSfax();
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);

    auto* table = m_page->findChild<QTableWidget*>();
    auto* sexFilter = m_page->findChild<QListWidget*>(QStringLiteral("sexFilter"));
    ASSERT_NE(table, nullptr);
    ASSERT_NE(sexFilter, nullptr);

    ASSERT_TRUE(selectFilterCode(sexFilter, QString::fromLatin1(Repositories::MemberSex::kMale)));
    ASSERT_EQ(table->rowCount(), 1);

    ASSERT_TRUE(selectFilterCode(sexFilter, QString()));
    EXPECT_EQ(table->rowCount(), 2);
}

TEST_F(test_ui_MemberFilters, YearAndAgeGroupNarrowTheTable)
{
    seedTunisAndSfax();
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);

    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);

    ASSERT_TRUE(selectFilterCode(m_page->findChild<QListWidget*>(QStringLiteral("yearFilter")),
                                 QStringLiteral("2024")));
    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), m_femaleId);

    ASSERT_TRUE(selectFilterCode(m_page->findChild<QListWidget*>(QStringLiteral("yearFilter")),
                                 QString()));
    ASSERT_TRUE(selectFilterCode(m_page->findChild<QListWidget*>(QStringLiteral("ageGroupFilter")),
                                 QString::fromLatin1(Repositories::MemberAgeGroup::kYouth)));
    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), m_femaleId);
}

TEST_F(test_ui_MemberFilters, SearchStillFindsMembersByOccupation)
{
    seedTunisAndSfax();
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);

    auto* table = m_page->findChild<QTableWidget*>();
    auto* search = m_page->findChild<QLineEdit*>(QStringLiteral("listSearch"));
    ASSERT_NE(table, nullptr);
    ASSERT_NE(search, nullptr);
    ASSERT_EQ(table->rowCount(), 2);

    search->setText(QStringLiteral("قاضي"));
    QApplication::processEvents();

    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), m_maleId);
}

TEST_F(test_ui_MemberFilters, PagerNextLoadsTheSecondPage)
{
    for (int i = 1; i <= 60; ++i) {
        ASSERT_GT(seedMember(*m_db, uniqueMemberSeed(i)), 0) << i;
    }

    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    m_page->show();
    QApplication::processEvents();

    auto* pager = m_page->findChild<VLMS::TablePager*>();
    ASSERT_NE(pager, nullptr);
    EXPECT_EQ(pager->totalCount(), 60);
    // The pager opens on ALL, so there is nothing to page through until a size
    // is picked. Pick it through the combo rather than setPageSize: that is
    // what a librarian does, and it is what makes the page reload.
    EXPECT_EQ(pager->pageCount(), 1);

    auto* pageSize = pager->findChild<QComboBox*>();
    ASSERT_NE(pageSize, nullptr);
    const int fifty = pageSize->findData(50);
    ASSERT_GE(fifty, 0);
    pageSize->setCurrentIndex(fifty);
    QApplication::processEvents();

    EXPECT_EQ(pager->pageSize(), 50);
    EXPECT_EQ(pager->pageCount(), 2);

    auto* next = pager->findChild<QPushButton*>(QStringLiteral("pagerNext"));
    ASSERT_NE(next, nullptr);
    ASSERT_TRUE(next->isEnabled());
    next->click();
    QApplication::processEvents();

    EXPECT_EQ(pager->currentPage(), 2);
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rowCount(), 10);
}

TEST_F(test_ui_MemberFilters, StatusFilterOffersActiveAndNotActive)
{
    seedTunisAndSfax();
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* list = m_page->findChild<QListWidget*>(QStringLiteral("statusFilter"));
    ASSERT_NE(list, nullptr);

    QStringList codes;
    for (int row = 0; row < list->count(); ++row) {
        const QString code = list->item(row)->data(Qt::UserRole).toString();
        if (!code.isEmpty()) {
            codes << code;
        }
    }
    EXPECT_EQ(codes, (QStringList{QStringLiteral("active"), QStringLiteral("non_active")}));
}

TEST_F(test_ui_MemberFilters, NotActiveKeepsOnlyTheMemberWhoseYearEnded)
{
    seedTunisAndSfax();
    ASSERT_TRUE(m_db->execBound("UPDATE members SET active_until = '2026-01-01' WHERE id = :id",
                                {{"id", static_cast<std::int64_t>(m_maleId)}}));
    const Core::ScopedClock pinned(Core::Date(2026, 9, 23));
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 2);

    ASSERT_TRUE(selectFilterCode(m_page->findChild<QListWidget*>(QStringLiteral("statusFilter")),
                                 QStringLiteral("non_active")));
    ASSERT_EQ(table->rowCount(), 1);
    EXPECT_EQ(tableText(table, 0, 4), QStringLiteral("Not active"));
}

TEST_F(test_ui_MemberFilters, DetailsShowTheLastActiveDay)
{
    seedTunisAndSfax();
    ASSERT_TRUE(m_db->execBound("UPDATE members SET active_until = '2027-04-30' WHERE id = :id",
                                {{"id", static_cast<std::int64_t>(m_femaleId)}}));
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* search = m_page->findChild<QLineEdit*>(QStringLiteral("listSearch"));
    ASSERT_NE(search, nullptr);
    search->setText(QStringLiteral("Sfax"));
    QApplication::processEvents();
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_EQ(table->rowCount(), 1);
    table->selectRow(0);
    QApplication::processEvents();

    QString value;
    for (QLabel* label : m_page->findChildren<QLabel*>(QStringLiteral("bookDetailLabel"))) {
        if (label->text() != QStringLiteral("Active until:")) {
            continue;
        }
        for (QLabel* sibling : label->parentWidget()->findChildren<QLabel*>()) {
            if (sibling != label) {
                value = sibling->text();
            }
        }
    }
    EXPECT_EQ(value, QStringLiteral("2027-04-30"));
}
