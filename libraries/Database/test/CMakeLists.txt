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
