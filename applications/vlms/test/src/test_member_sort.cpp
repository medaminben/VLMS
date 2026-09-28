#include "TestDatabase.h"
#include "TestSeed.h"

#include "ui/members/MembersPage.h"
#include "ui/TablePager.h"

#include <VLMS/Repositories/CirculationRepository.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>

#include <QHeaderView>
#include <QTableWidget>
#include <QTableWidgetItem>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

class test_ui_MemberSort : public ::testing::Test {
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

    std::int64_t seedMemberWithNumber(MemberSeed seed)
    {
        const std::int64_t id = seedMember(*m_db, seed);
        if (id <= 0) {
            return id;
        }
        EXPECT_TRUE(m_db->execBound(
            "UPDATE members SET membership_number = :value WHERE id = :id",
            {{"value", seed.membershipNumber}, {"id", id}}));
        return id;
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_members;
    std::unique_ptr<Repositories::CirculationRepository> m_circulation;
    std::unique_ptr<MembersPage> m_page;
};

TEST_F(test_ui_MemberSort, ClickingNumberHeaderSortsAscendingThenDescending)
{
    MemberSeed late = uniqueMemberSeed(1);
    late.membershipNumber = "10";
    late.lastName = "Aaa";
    ASSERT_GT(seedMemberWithNumber(late), 0);
    MemberSeed early = uniqueMemberSeed(2);
    early.membershipNumber = "2";
    early.lastName = "Zzz";
    ASSERT_GT(seedMemberWithNumber(early), 0);

    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* table = m_page->findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 2);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("10"));

    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("2"));
    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(table->item(0, 0)->text(), QStringLiteral("10"));
}

TEST_F(test_ui_MemberSort, NoSelectionReturnsToPageOne)
{
    for (int i = 0; i < 5; ++i) {
        MemberSeed seed = uniqueMemberSeed(10 + i);
        seed.membershipNumber = std::to_string(i + 1);
        seed.lastName = std::string(1, static_cast<char>('E' - i));
        ASSERT_GT(seedMemberWithNumber(seed), 0);
    }
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* table = m_page->findChild<QTableWidget*>();
    auto* pager = m_page->findChild<VLMS::TablePager*>();
    ASSERT_NE(table, nullptr);
    ASSERT_NE(pager, nullptr);
    pager->setPageSize(2);
    pager->setCurrentPage(2);
    table->clearSelection();
    table->setCurrentItem(nullptr);

    emit table->horizontalHeader()->sectionClicked(0);
    EXPECT_EQ(pager->currentPage(), 1);
}

TEST_F(test_ui_MemberSort, SelectedRowKeepsFocusAcrossPages)
{
    std::int64_t firstId = 0;
    for (int i = 0; i < 5; ++i) {
        MemberSeed seed = uniqueMemberSeed(20 + i);
        seed.membershipNumber = std::to_string(i + 1);
        seed.lastName = std::string(1, static_cast<char>('A' + i));
        const std::int64_t id = seedMemberWithNumber(seed);
        ASSERT_GT(id, 0);
        if (i == 0) {
            firstId = id;
        }
    }
    m_page = std::make_unique<MembersPage>(*m_members, *m_circulation);
    auto* table = m_page->findChild<QTableWidget*>();
    auto* pager = m_page->findChild<VLMS::TablePager*>();
    ASSERT_NE(table, nullptr);
    ASSERT_NE(pager, nullptr);
    pager->setPageSize(2);

    // default last-name order: A=1 is row 0 and selected
    EXPECT_EQ(table->item(0, 0)->data(Qt::UserRole).toLongLong(), firstId);

    emit table->horizontalHeader()->sectionClicked(0); // number asc: 1,2,3,4,5 — still page 1
    emit table->horizontalHeader()->sectionClicked(0); // number desc: 5,4,3,2,1 — A=1 is last
    EXPECT_EQ(pager->currentPage(), 3);
    bool found = false;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->data(Qt::UserRole).toLongLong() == firstId) {
            found = table->item(row, 0)->isSelected();
        }
    }
    EXPECT_TRUE(found);
}
