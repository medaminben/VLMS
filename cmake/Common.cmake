set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(BuildUtils)
include(BuildLocation)

if(BUILD_QT_UI)
    include(QtSupport)
endif()

if(BUILD_TESTING)
    include(TestUtils)
    if(BUILD_QT_UI AND QT_VERSION_MAJOR)
        find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test QUIET)
    endif()
endif()
