add_library(vlms_testsupport STATIC
    support/SqlValue.h
    support/TestDatabase.cpp
    support/TestDatabase.h
    support/TestSeed.cpp
    support/TestSeed.h
    support/TestEnv.cpp
    support/TestEnv.h
)

target_include_directories(vlms_testsupport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/support
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libraries/Core/src
)

target_link_libraries(vlms_testsupport
    PUBLIC
        ${CMAKE_ROOT_NAME}::Core
        GTest::gtest
)

vlms_configure_target(vlms_testsupport)
set_target_properties(vlms_testsupport PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

set(TST_SOURCES
    src/test_result.cpp
    src/test_clock.cpp
    src/test_loan_policy.cpp
    src/test_date_text.cpp
    src/test_sql_text.cpp
    src/test_database_schema.cpp
    src/test_database_migrations.cpp
    src/test_database_file.cpp
    src/test_strings_parity.cpp
    src/test_licence_strings.cpp
    src/test_catalog_repository.cpp
    src/test_member_repository.cpp
    src/test_member_status_rule.cpp
    src/test_member_status_expiry.cpp
    src/test_circulation_repository.cpp
    src/test_circulation_dates.cpp
    src/test_loan_book_filter.cpp
    src/test_list_facets.cpp
    src/test_metrics_repository.cpp
    src/test_catalog_injection.cpp
    src/test_member_injection.cpp
    src/test_circulation_injection.cpp
    src/test_metrics_windows.cpp
    src/test_circulation_timezone.cpp
    src/test_sql_script.cpp
    src/test_sqlite_session.cpp
    src/test_database_realdb.cpp
    src/test_archive_catalog.cpp
    src/test_archive_circulation.cpp
    src/test_archive_purge.cpp
    src/test_free_local_numbers.cpp
    src/test_archive_members.cpp
    src/test_archive_reuse.cpp
    src/test_can_remove.cpp
)

build_gtest_executable(
    NAME test_vlms_core
    SRC ${TST_SOURCES}
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
target_include_directories(test_vlms_core PRIVATE ${CMAKE_SOURCE_DIR}/libraries/Core/src)

set(VLMS_TEST_REAL_DB "${CMAKE_SOURCE_DIR}/database/vlms.db.bak-20260814-232925"
    CACHE FILEPATH "A real VLMS database to verify migrations against")

if(VLMS_TEST_REAL_DB AND EXISTS "${VLMS_TEST_REAL_DB}")
    set(_vlms_test_timeout 300)
    set(_vlms_test_labels "realdb;dates")
    set(_vlms_test_environment "VLMS_TEST_REAL_DB=${VLMS_TEST_REAL_DB}")
    _vlms_add_gtest_entry(test_vlms_core_realdb test_vlms_core "Africa/Tunis"
        --gtest_filter=test_core_RealDb.*)
else()
    message(STATUS
        "test_vlms_core RealDb: not registered, no database at '${VLMS_TEST_REAL_DB}'")
endif()
