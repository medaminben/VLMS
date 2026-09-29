#include <VLMS/Core/Paths.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

using namespace VLMS;

namespace fs = std::filesystem;

class test_core_Paths : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_previous = Core::Paths::projectRoot();
        m_root = fs::temp_directory_path()
                 / ("vlms-paths-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed())
                    + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(m_root);
        Core::Paths::setProjectRoot(m_root.string());
    }

    void TearDown() override
    {
        Core::Paths::setProjectRoot(m_previous);
        fs::remove_all(m_root);
    }

    fs::path m_root;
    std::string m_previous;
};

TEST_F(test_core_Paths, RootsHangOffTheProjectRoot)
{
    EXPECT_EQ(Core::Paths::projectRoot(), m_root.string());
    EXPECT_EQ(fs::path(Core::Paths::databaseDirectory()), m_root / "database");
    EXPECT_EQ(fs::path(Core::Paths::resourcesDirectory()), m_root / "resources");
    EXPECT_EQ(fs::path(Core::Paths::databasePath()), m_root / "database" / "vlms.db");
}

TEST_F(test_core_Paths, EnsureLayoutCreatesTheDataFolders)
{
    ASSERT_TRUE(Core::Paths::ensureLayout());

    EXPECT_TRUE(fs::is_directory(m_root / "database"));
    EXPECT_TRUE(fs::is_directory(m_root / "resources" / "books"));
    EXPECT_TRUE(fs::is_directory(m_root / "resources" / "members"));
}

TEST_F(test_core_Paths, EnsureLayoutKeepsWhatIsAlreadyThere)
{
    ASSERT_TRUE(Core::Paths::ensureLayout());
    const fs::path cover = m_root / "resources" / "books" / "1" / "cover.png";
    fs::create_directories(cover.parent_path());
    { std::FILE* f = std::fopen(cover.string().c_str(), "wb"); ASSERT_NE(f, nullptr); std::fclose(f); }

    ASSERT_TRUE(Core::Paths::ensureLayout());

    EXPECT_TRUE(fs::exists(cover));
}

TEST_F(test_core_Paths, EnsureLayoutFailsWhenTheRootIsAFile)
{
    fs::create_directories(m_root.parent_path());
    { std::FILE* f = std::fopen(m_root.string().c_str(), "wb"); ASSERT_NE(f, nullptr); std::fclose(f); }

    EXPECT_FALSE(Core::Paths::ensureLayout());
}
