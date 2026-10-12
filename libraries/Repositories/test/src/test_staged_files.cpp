#include "StagedFiles.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

using VLMS::Repositories::StagedFiles;
namespace fs = std::filesystem;

namespace {

class StagedFilesTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        static std::atomic<int> counter{0};
        m_dir = fs::temp_directory_path()
            / ("vlms_staged_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_"
               + std::to_string(counter++) + "_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(m_dir);
        fs::create_directories(m_dir);
    }

    void TearDown() override
    {
        std::error_code ignored;
        fs::remove_all(m_dir, ignored);
    }

    fs::path path(const std::string& name) const { return m_dir / name; }

    void write(const fs::path& file, const std::string& bytes) const
    {
        std::ofstream out(file, std::ios::binary);
        out << bytes;
    }

    static std::string read(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    // Every regular file or directory name below the test directory that is a
    // leftover of a swap.
    int leftovers() const
    {
        int count = 0;
        for (const auto& entry : fs::recursive_directory_iterator(m_dir)) {
            const std::string ext = entry.path().extension().string();
            if (ext == ".part" || ext == ".old") {
                ++count;
            }
        }
        return count;
    }

    fs::path m_dir;
};

}  // namespace

TEST_F(StagedFilesTest, ASwapThatIsNotFinishedRestoresTheOldFile)
{
    write(path("source.jpg"), "new");
    write(path("target.jpg"), "old");
    {
        StagedFiles staged;
        ASSERT_TRUE(staged.stage(path("source.jpg"), path("target.jpg")));
        ASSERT_TRUE(staged.swap());
        EXPECT_EQ(read(path("target.jpg")), "new");
    }
    EXPECT_EQ(read(path("target.jpg")), "old");
    EXPECT_EQ(leftovers(), 0);
}

TEST_F(StagedFilesTest, AFinishedSwapKeepsTheNewFileAndLeavesNoBackup)
{
    write(path("source.jpg"), "new");
    write(path("target.jpg"), "old");
    {
        StagedFiles staged;
        ASSERT_TRUE(staged.stage(path("source.jpg"), path("target.jpg")));
        ASSERT_TRUE(staged.swap());
        staged.finish();
    }
    EXPECT_EQ(read(path("target.jpg")), "new");
    EXPECT_EQ(leftovers(), 0);
}

TEST_F(StagedFilesTest, AnUnfinishedSwapRemovesATargetThatDidNotExistBefore)
{
    write(path("source.jpg"), "new");
    {
        StagedFiles staged;
        ASSERT_TRUE(staged.stage(path("source.jpg"), path("target.jpg")));
        ASSERT_TRUE(staged.swap());
        EXPECT_TRUE(fs::exists(path("target.jpg")));
    }
    EXPECT_FALSE(fs::exists(path("target.jpg")));
    EXPECT_EQ(leftovers(), 0);
}

TEST_F(StagedFilesTest, AFailedSwapRestoresTheFilesAlreadySwapped)
{
    write(path("source1.jpg"), "new1");
    write(path("source2.jpg"), "new2");
    write(path("first.jpg"), "old");
    // A non-empty directory cannot be replaced by a file, so the second swap fails.
    fs::create_directories(path("second.jpg"));
    write(path("second.jpg") / "keep.txt", "keep");

    StagedFiles staged;
    ASSERT_TRUE(staged.stage(path("source1.jpg"), path("first.jpg")));
    ASSERT_TRUE(staged.stage(path("source2.jpg"), path("second.jpg")));
    EXPECT_FALSE(staged.swap());

    EXPECT_EQ(read(path("first.jpg")), "old");
    EXPECT_TRUE(fs::is_directory(path("second.jpg")));
    EXPECT_EQ(read(path("second.jpg") / "keep.txt"), "keep");
    EXPECT_EQ(leftovers(), 0);
}

TEST_F(StagedFilesTest, DiscardBeforeSwapLeavesTargetsUntouched)
{
    write(path("source.jpg"), "new");
    write(path("target.jpg"), "old");
    StagedFiles staged;
    ASSERT_TRUE(staged.stage(path("source.jpg"), path("target.jpg")));
    staged.discard();
    EXPECT_EQ(read(path("target.jpg")), "old");
    EXPECT_EQ(leftovers(), 0);
}
