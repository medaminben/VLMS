add_library(vlms_testsupport_core STATIC
    support/TestEnv.cpp
    support/TestEnv.h
)

target_include_directories(vlms_testsupport_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/support
)

target_link_libraries(vlms_testsupport_core
    PUBLIC
        ${CMAKE_ROOT_NAME}::Core
        GTest::gtest
)

vlms_configure_target(vlms_testsupport_core)
set_target_properties(vlms_testsupport_core PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

set(CORE_TST_SOURCES
    src/test_result.cpp
    src/test_clock.cpp
    src/test_date_text.cpp
    src/test_licence_strings.cpp
)

# --- Database tests: moved to libraries/Database/test by the Database test task ---

# --- Repositories tests: moved to libraries/Repositories/test by the Repositories test task ---

# --- Until both blocks above have moved, this binary runs all three sets. ---

build_gtest_executable(
    NAME test_vlms_core
    SRC ${CORE_TST_SOURCES} ${DATABASE_TST_SOURCES} ${REPOSITORIES_TST_SOURCES}
    DEPENDS vlms_testsupport
    DISCOVER OFF
    GTEST_FILTER -test_core_Timezone.*:test_core_RealDb.*
    LABELS core
    # One ctest entry runs every test in the binary. The default 60s is that
    # whole budget; a slow runner spent it inside the catalogue search tests.
    TIMEOUT 300
    TIMEZONES Pacific/Kiritimati Pacific/Midway
    TIMEZONE_FILTER test_core_Timezone.*
)

# --- Real database: moved to libraries/Database/test by the Database test task ---
