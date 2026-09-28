#include <VLMS/Core/Paths.h>

#include <cstdlib>
#include <filesystem>

namespace VLMS::Core {

namespace {

std::string& rootStorage()
{
    static std::string root;
#ifdef VLMS_PROJECT_ROOT
    if (root.empty()) {
        root = VLMS_PROJECT_ROOT;
    }
#endif
    return root;
}

}  // namespace

void Paths::setProjectRoot(std::string root)
{
    rootStorage() = std::move(root);
}

const std::string& Paths::projectRoot()
{
    return rootStorage();
}

std::string Paths::databaseDirectory()
{
    return (std::filesystem::path(projectRoot()) / "database").string();
}

std::string Paths::resourcesDirectory()
{
    return (std::filesystem::path(projectRoot()) / "resources").string();
}

std::string Paths::databasePath()
{
    return (std::filesystem::path(databaseDirectory()) / "vlms.db").string();
}

bool Paths::ensureLayout()
{
    std::error_code error;
    std::filesystem::create_directories(databaseDirectory(), error);
    if (error) {
        return false;
    }
    std::filesystem::create_directories(std::filesystem::path(resourcesDirectory()) / "books",
                                        error);
    if (error) {
        return false;
    }
    std::filesystem::create_directories(std::filesystem::path(resourcesDirectory()) / "members",
                                        error);
    return !error;
}

}  // namespace VLMS::Core
