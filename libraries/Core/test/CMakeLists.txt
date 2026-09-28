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

set(TST_SOURCES
    src/test_result.cpp
    src/test_clock.cpp
    src/test_date_text.cpp
    src/test_licence_strings.cpp
)

build_gtest_executable(
    NAME test_vlms_core
    SRC ${TST_SOURCES}
    DEPENDS vlms_testsupport_core
    DISCOVER OFF
    LABELS core
)
