# vlms_testsupport keeps its old name: applications/vlms/test links it.
add_library(vlms_testsupport STATIC
    support/TestSeed.cpp
    support/TestSeed.h
)

target_include_directories(vlms_testsupport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/support
)

target_link_libraries(vlms_testsupport
    PUBLIC
        vlms_testsupport_database
        ${CMAKE_ROOT_NAME}::Repositories
        GTest::gtest
)

vlms_configure_target(vlms_testsupport)
set_target_properties(vlms_testsupport PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

set(TST_SOURCES
    src/test_loan_policy.cpp
    src/test_strings_parity.cpp
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
    src/test_archive_catalog.cpp
    src/test_archive_circulation.cpp
    src/test_archive_purge.cpp
    src/test_free_local_numbers.cpp
    src/test_archive_members.cpp
    src/test_archive_reuse.cpp
    src/test_can_remove.cpp
)

build_gtest_executable(
    NAME test_vlms_repositories
    SRC ${TST_SOURCES}
    DEPENDS vlms_testsupport
    DISCOVER OFF
    GTEST_FILTER -test_core_Timezone.*
    LABELS repositories
    # One ctest entry runs every test in the binary. The default 60s is that
    # whole budget; a slow runner spent it inside the catalogue search tests.
    TIMEOUT 300
    TIMEZONES Pacific/Kiritimati Pacific/Midway
    TIMEZONE_FILTER test_core_Timezone.*
)
