#pragma once

#include <string>

namespace VLMS {

/** Project-local data layout:
 *   database/vlms.db
 *   resources/books/
 *   resources/members/
 *
 * Roots are injected by the application. Core never reads QCoreApplication.
 */
class Paths {
public:
    static void setProjectRoot(std::string root);
    [[nodiscard]] static const std::string& projectRoot();
    [[nodiscard]] static std::string databaseDirectory();
    [[nodiscard]] static std::string resourcesDirectory();
    [[nodiscard]] static std::string databasePath();

    static bool ensureLayout();
};

}  // namespace VLMS
