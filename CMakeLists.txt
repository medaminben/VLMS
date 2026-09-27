cmake_minimum_required(VERSION 3.21)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# The version is the latest vX.Y.Z tag, cut by .github/workflows/release.yml
# from the diff since the previous one. Nothing here is edited by hand.
include(GitVersion)
vlms_git_version("${CMAKE_CURRENT_SOURCE_DIR}" VLMS_VERSION_NUMERIC VLMS_VERSION_FULL)
project(VLMS VERSION ${VLMS_VERSION_NUMERIC} LANGUAGES CXX)
message(STATUS "VLMS version ${VLMS_VERSION_FULL}")

# Read by scripts/build_windows_installer.ps1: the installer's version is the
# one this configure chose, not a second copy parsed out of this file.
file(WRITE "${CMAKE_BINARY_DIR}/vlms_version.txt" "${PROJECT_VERSION}\n")

set(CMAKE_ROOT_NAME ${PROJECT_NAME})

option(BUILD_SHARED_LIBS "Build project libraries as shared libraries" OFF)
option(BUILD_APPS "Build applications" ON)
option(BUILD_QT_UI "Build Qt-based applications" ON)
option(VLMS_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
# UnitPro-style name; kept in sync with VLMS_BUILD_TESTS so existing CI flags work.
set(BUILD_TESTING ${VLMS_BUILD_TESTS})

include(Common)

# enable_testing() must be called from the top-level list file so that
# `ctest --test-dir build` finds the suite. Deliberately not include(CTest),
# which would pull in CDash/Dart targets we have no use for.
if(BUILD_TESTING)
    enable_testing()
    add_test(NAME cmake_GitVersion
             COMMAND ${CMAKE_COMMAND}
                     -DGIT_VERSION_MODULE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/GitVersion.cmake
                     -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/git-version-test
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test/GitVersionTest.cmake)
endif()

add_subdirectory(libraries)

if(BUILD_APPS)
    add_subdirectory(applications)
endif()
