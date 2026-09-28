#include "TestDatabase.h"
#include "UiTest.h"

#include "ui/members/MemberEditorDialog.h"

#include <VLMS/Core/Clock.h>
#include <VLMS/Core/Date.h>
#include <VLMS/Core/Locale.h>
#include <VLMS/Repositories/MemberRepository.h>
#include <VLMS/Repositories/MemberTypes.h>
#include <VLMS/Core/Strings.h>

#include <QComboBox>
#include <QLabel>

#include <gtest/gtest.h>

#include <memory>

using namespace VLMS;
using namespace Test;

namespace {

Repositories::MemberRecord memberUntil(const std::string& activeUntil, const std::string& status)
{
    Repositories::MemberRecord member;
    member.id = 1;
    member.membershipNumber = "1";
    member.firstName = "Amina";
    member.lastName = "Ben Salah";
    member.dateOfBirth = "1990-05-12";
    member.status = status;
    member.activeUntil = activeUntil;
    return member;
}

bool pick(QComboBox* combo, const char* code)
{
    const int index = combo->findData(QString::fromLatin1(code));
    if (index < 0) {
        return false;
    }
    combo->setCurrentIndex(index);
    return true;
}

}  // namespace

class test_ui_MemberEditorStatus : public ::testing::Test {
protected:
    static void SetUpTestSuite() { Core::Locale::setCode("en"); }

    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_repository = std::make_unique<Repositories::MemberRepository>(m_db->session(), m_db->resourcesDirectory());
    }

    void TearDown() override
    {
        m_repository.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<Repositories::MemberRepository> m_repository;
};

TEST_F(test_ui_MemberEditorStatus, TheComboOffersActiveAndNotActiveOnly)
{
    MemberEditorDialog dialog(*m_repository);
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("memberStatusCombo"));
    ASSERT_NE(combo, nullptr);
    ASSERT_EQ(combo->count(), 2);
    EXPECT_EQ(combo->itemData(0).toString(), QString::fromLatin1(Repositories::MemberStatus::kActive));
    EXPECT_EQ(combo->itemData(1).toString(), QString::fromLatin1(Repositories::MemberStatus::kNonActive));
    EXPECT_EQ(combo->currentData().toString(), QString::fromLatin1(Repositories::MemberStatus::kActive));
}

TEST_F(test_ui_MemberEditorStatus, ANewMemberShowsAYearFromToday)
{
    const Core::ScopedClock pinned(Core::Date(2026, 9, 23));
    MemberEditorDialog dialog(*m_repository);
    auto* until = dialog.findChild<QLabel*>(QStringLiteral("memberActiveUntilValue"));
    ASSERT_NE(until, nullptr);
    EXPECT_EQ(until->text(), QStringLiteral("2027-09-22"));
}

TEST_F(test_ui_MemberEditorStatus, PickingActiveForAnExpiredMemberShowsTheRenewal)
{
    const Core::ScopedClock pinned(Core::Date(2026, 9, 23));
    MemberEditorDialog dialog(*m_repository, memberUntil("2026-01-09", Repositories::MemberStatus::kNonActive));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("memberStatusCombo"));
    auto* until = dialog.findChild<QLabel*>(QStringLiteral("memberActiveUntilValue"));
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(until, nullptr);

    EXPECT_EQ(combo->currentData().toString(), QString::fromLatin1(Repositories::MemberStatus::kNonActive));
    EXPECT_EQ(until->text(), QStringLiteral("2026-01-09"));

    ASSERT_TRUE(pick(combo, Repositories::MemberStatus::kActive));
    EXPECT_EQ(until->text(), QStringLiteral("2027-09-22"));
    EXPECT_EQ(dialog.memberInput().status, Repositories::MemberStatus::kActive);
}

TEST_F(test_ui_MemberEditorStatus, PickingNotActiveForAnActiveMemberShowsYesterday)
{
    const Core::ScopedClock pinned(Core::Date(2026, 9, 23));
    MemberEditorDialog dialog(*m_repository, memberUntil("2027-03-01", Repositories::MemberStatus::kActive));
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("memberStatusCombo"));
    auto* until = dialog.findChild<QLabel*>(QStringLiteral("memberActiveUntilValue"));
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(until, nullptr);

    ASSERT_TRUE(pick(combo, Repositories::MemberStatus::kNonActive));
    EXPECT_EQ(until->text(), QStringLiteral("2026-09-22"));

    ASSERT_TRUE(pick(combo, Repositories::MemberStatus::kActive));
    EXPECT_EQ(until->text(), QStringLiteral("2027-03-01")) << "back to the saved status keeps the date";
}

TEST_F(test_ui_MemberEditorStatus, TheRowIsLabelledInEveryLanguage)
{
    for (const char* locale : {"ar", "fr", "en"}) {
        SCOPED_TRACE(locale);
        Core::Locale::setCode(locale);
        MemberEditorDialog dialog(*m_repository);
        bool found = false;
        for (QLabel* label : dialog.findChildren<QLabel*>()) {
            if (label->text().startsWith(qs(Core::Strings::t("member.field.activeUntil")))) {
                found = true;
            }
        }
        EXPECT_TRUE(found);
    }
    Core::Locale::setCode("en");
}
