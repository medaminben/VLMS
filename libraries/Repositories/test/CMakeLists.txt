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
