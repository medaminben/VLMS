add_library(vlms_testsupport_database STATIC
    support/SqlValue.h
    support/TestDatabase.cpp
    support/TestDatabase.h
)

target_include_directories(vlms_testsupport_database
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/support
)

target_link_libraries(vlms_testsupport_database
    PUBLIC
        vlms_testsupport_core
        ${CMAKE_ROOT_NAME}::Database
        GTest::gtest
)

vlms_configure_target(vlms_testsupport_database)
set_target_properties(vlms_testsupport_database PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

set(TST_SOURCES
    src/test_sql_text.cpp
    src/test_sqlite_session.cpp
    src/test_sql_script.cpp
    src/test_database_schema.cpp
    src/test_database_migrations.cpp
    src/test_database_file.cpp
    src/test_database_realdb.cpp
)

build_gtest_executable(
    NAME test_vlms_database
    SRC ${TST_SOURCES}
    DEPENDS vlms_testsupport_database
    DISCOVER OFF
    GTEST_FILTER -test_core_RealDb.*
    LABELS database
    TIMEOUT 300
)

set(VLMS_TEST_REAL_DB "${CMAKE_SOURCE_DIR}/database/vlms.db.bak-20260814-232925"
    CACHE FILEPATH "A real VLMS database to verify migrations against")

if(VLMS_TEST_REAL_DB AND EXISTS "${VLMS_TEST_REAL_DB}")
    set(_vlms_test_timeout 300)
    set(_vlms_test_labels "realdb;dates")
    set(_vlms_test_environment "VLMS_TEST_REAL_DB=${VLMS_TEST_REAL_DB}")
    _vlms_add_gtest_entry(test_vlms_database_realdb test_vlms_database "Africa/Tunis"
        --gtest_filter=test_core_RealDb.*)
else()
    message(STATUS
        "test_vlms_database RealDb: not registered, no database at '${VLMS_TEST_REAL_DB}'")
endif()
