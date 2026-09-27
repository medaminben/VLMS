#include "TestDatabase.h"
#include "TestEnv.h"
#include "TestSeed.h"

#include <VLMS/Core/CatalogRepository.h>
#include <VLMS/Core/CatalogTypes.h>
#include <VLMS/Core/CirculationRepository.h>
#include <VLMS/Core/LoanTypes.h>
#include <VLMS/Core/MemberRepository.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

using namespace VLMS::Test;

namespace {

/// The Members page's filters narrow the loan lists by borrower, and the
/// Catalogue's narrow the Archive's copy list by the title a copy came from.
class test_core_ListFacets : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_db = std::make_unique<TestDatabase>();
        ASSERT_TRUE(m_db->isValid()) << m_db->lastError();
        m_circulation = std::make_unique<CirculationRepository>(m_db->session());
        m_catalog =
            std::make_unique<CatalogRepository>(m_db->session(), m_db->resourcesDirectory());
        m_members =
            std::make_unique<MemberRepository>(m_db->session(), m_db->resourcesDirectory());

        MemberSeed tunis = uniqueMemberSeed(1);
        tunis.sex = "male";
        tunis.city = "Tunis";
        m_tunisId = seedMember(*m_db, tunis);
        ASSERT_GT(m_tunisId, 0);
        ASSERT_TRUE(rawSetRegisteredAt(*m_db, m_tunisId, "2019-03-01 10:00:00"));
        ASSERT_TRUE(m_db->exec("UPDATE members SET photo_path = 'members/1/photo.jpg' WHERE id = "
                               + std::to_string(m_tunisId)));

        MemberSeed sfax = uniqueMemberSeed(2);
        sfax.sex = "female";
        sfax.city = "Sfax";
        m_sfaxId = seedMember(*m_db, sfax);
        ASSERT_GT(m_sfaxId, 0);
        ASSERT_TRUE(rawSetRegisteredAt(*m_db, m_sfaxId, "2024-06-15 09:00:00"));

        m_historyId = seedCategory(*m_db, "HIS", "History");
        BookSeed arabic = uniqueBookSeed(1);
        arabic.language = "ar";
        arabic.categoryId = m_historyId;
        m_arabicBookId = seedBook(*m_db, arabic);
        ASSERT_GT(m_arabicBookId, 0);
        ASSERT_TRUE(m_db->exec("UPDATE books SET cover_image_path = 'books/1/cover.jpg' WHERE id = "
                               + std::to_string(m_arabicBookId)));

        BookSeed french = uniqueBookSeed(2);
        french.language = "fr";
        m_frenchBookId = seedBook(*m_db, french);
        ASSERT_GT(m_frenchBookId, 0);

        const auto arabicCopy = copyIdsOf(*m_db, m_arabicBookId).at(0);
        const auto frenchCopy = copyIdsOf(*m_db, m_frenchBookId).at(0);
        ASSERT_GT(rawInsertLoan(*m_db, m_tunisId, arabicCopy, "2025-01-10", "2025-01-24",
                                "2025-01-20"), 0);
        ASSERT_GT(rawInsertLoan(*m_db, m_sfaxId, frenchCopy, "2025-02-10", "2025-02-24",
                                "2025-02-20"), 0);
    }

    void TearDown() override
    {
        m_members.reset();
        m_catalog.reset();
        m_circulation.reset();
        m_db.reset();
    }

    std::unique_ptr<TestDatabase> m_db;
    std::unique_ptr<CirculationRepository> m_circulation;
    std::unique_ptr<CatalogRepository> m_catalog;
    std::unique_ptr<MemberRepository> m_members;
    std::int64_t m_tunisId = 0;
    std::int64_t m_sfaxId = 0;
    std::int64_t m_historyId = 0;
    std::int64_t m_arabicBookId = 0;
    std::int64_t m_frenchBookId = 0;
};

TEST_F(test_core_ListFacets, LoansFollowTheBorrowersSexCityAndYear)
{
    LoanQuery bySex;
    bySex.member.sexes = {"female"};
    const auto female = m_circulation->listLoans(bySex);
    ASSERT_TRUE(female.has_value());
    ASSERT_EQ(female.value().size(), 1U);
    EXPECT_EQ(female.value().front().memberId, m_sfaxId);
    EXPECT_EQ(m_circulation->countLoans(bySex).value(), 1);

    LoanQuery byCity;
    byCity.member.cities = {"tunis"};
    const auto tunis = m_circulation->listLoans(byCity);
    ASSERT_TRUE(tunis.has_value());
    ASSERT_EQ(tunis.value().size(), 1U);
    EXPECT_EQ(tunis.value().front().memberId, m_tunisId);

    LoanQuery byYear;
    byYear.member.inscriptionYears = {"2024"};
    byYear.member.cities = {"Tunis"};
    EXPECT_EQ(m_circulation->countLoans(byYear).value(), 0) << "dimensions are ANDed";

    LoanQuery none;
    EXPECT_EQ(m_circulation->countLoans(none).value(), 2);
}

TEST_F(test_core_ListFacets, LoanRowsCarryTheBorrowersPhoto)
{
    LoanQuery query;
    query.member.cities = {"Tunis"};
    const auto loans = m_circulation->listLoans(query);
    ASSERT_TRUE(loans.has_value());
    ASSERT_EQ(loans.value().size(), 1U);
    EXPECT_EQ(loans.value().front().memberPhotoPath, "members/1/photo.jpg");
}

TEST_F(test_core_ListFacets, CopiesFollowTheirTitlesLanguageCategoryAndCover)
{
    CopyQuery byLanguage;
    byLanguage.archive = ArchiveScope::Any;
    byLanguage.languages = {"fr"};
    const auto french = m_catalog->listCopyRows(byLanguage);
    ASSERT_TRUE(french.has_value());
    ASSERT_EQ(french.value().size(), 1U);
    EXPECT_EQ(french.value().front().bookId, m_frenchBookId);
    EXPECT_EQ(m_catalog->countCopyRows(byLanguage).value(), 1);

    CopyQuery byCategory;
    byCategory.archive = ArchiveScope::Any;
    byCategory.categoryCodes = {"HIS"};
    const auto history = m_catalog->listCopyRows(byCategory);
    ASSERT_TRUE(history.has_value());
    ASSERT_EQ(history.value().size(), 1U);
    EXPECT_EQ(history.value().front().bookId, m_arabicBookId);
    EXPECT_EQ(history.value().front().coverImagePath, "books/1/cover.jpg");

    CopyQuery withoutCover;
    withoutCover.archive = ArchiveScope::Any;
    withoutCover.coverFilter = CoverFilter::WithoutCover;
    EXPECT_EQ(m_catalog->countCopyRows(withoutCover).value(), 1);
}

TEST_F(test_core_ListFacets, ValueListsFollowTheScope)
{
    ASSERT_TRUE(m_db->exec("UPDATE members SET archived_at = '2026-09-23 11:41:50' WHERE id = "
                           + std::to_string(m_tunisId)));
    ASSERT_TRUE(m_db->exec("UPDATE books SET archived_at = '2026-09-23 11:41:50' WHERE id = "
                           + std::to_string(m_frenchBookId)));

    const auto liveYears = m_members->listInscriptionYears();
    ASSERT_TRUE(liveYears.has_value());
    EXPECT_EQ(liveYears.value(), (std::vector<std::string>{"2024"}));

    const auto archivedYears = m_members->listInscriptionYears(ArchiveScope::Archived);
    ASSERT_TRUE(archivedYears.has_value());
    EXPECT_EQ(archivedYears.value(), (std::vector<std::string>{"2019"}));

    const auto anyCities = m_members->listCities(ArchiveScope::Any);
    ASSERT_TRUE(anyCities.has_value());
    EXPECT_EQ(anyCities.value(), (std::vector<std::string>{"Sfax", "Tunis"}));

    const auto archivedLanguages = m_catalog->listBookLanguages(ArchiveScope::Archived);
    ASSERT_TRUE(archivedLanguages.has_value());
    ASSERT_EQ(archivedLanguages.value().size(), 1U);
    EXPECT_EQ(archivedLanguages.value().front().code, "fr");
}

TEST_F(test_core_ListFacets, ACopysHistoryHoldsOnlyItsOwnLoans)
{
    const auto arabicCopy = copyIdsOf(*m_db, m_arabicBookId).at(0);
    LoanQuery query;
    query.copyId = arabicCopy;
    query.archive = ArchiveScope::Any;
    const auto loans = m_circulation->listLoans(query);
    ASSERT_TRUE(loans.has_value());
    ASSERT_EQ(loans.value().size(), 1U);
    EXPECT_EQ(loans.value().front().bookCopyId, arabicCopy);
    EXPECT_EQ(m_circulation->countLoans(query).value(), 1);
}

TEST_F(test_core_ListFacets, LoansFollowTheYearTheyWereMade)
{
    // Registered 2019 and 2024, but both borrowed in 2025.
    LoanQuery byLoanYear;
    byLoanYear.loanYears = {"2025"};
    EXPECT_EQ(m_circulation->countLoans(byLoanYear).value(), 2);
    byLoanYear.loanYears = {"2019"};
    EXPECT_EQ(m_circulation->countLoans(byLoanYear).value(), 0)
        << "a registration year is not a loan year";

    const auto years = m_circulation->listLoanYears();
    ASSERT_TRUE(years.has_value());
    EXPECT_EQ(years.value(), (std::vector<std::string>{"2025"}));
}

}  // namespace
