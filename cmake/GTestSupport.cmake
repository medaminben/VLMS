include(FetchContent)

# Pin GoogleTest for reproducible builds (do not track `main`).
set(VLMS_GTEST_VERSION "v1.15.2" CACHE STRING "GoogleTest git tag")

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        ${VLMS_GTEST_VERSION}
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)
