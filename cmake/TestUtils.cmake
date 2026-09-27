include(GTestSupport)

# Internal helper; do not call directly.
function(_vlms_add_gtest_entry test_name executable timezone)
    add_test(NAME ${test_name} COMMAND ${executable} ${ARGN})

    set(_env
        "QT_QPA_PLATFORM=offscreen"
        "VLMS_SCHEMA_PATH=${CMAKE_SOURCE_DIR}/database/schema.sql"
        "VLMS_TEST_DATA_DIR=${CMAKE_SOURCE_DIR}/libraries/Core/test/data"
        "VLMS_TEST_OCR_DATA_DIR=${CMAKE_SOURCE_DIR}/libraries/Ocr/test/data"
        "TZ=${timezone}"
        "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/${test_name}"
    )
    list(APPEND _env ${_vlms_test_environment})

    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "${_env}"
        TIMEOUT ${_vlms_test_timeout}
        LABELS "${_vlms_test_labels}"
    )
endfunction()

# build_gtest_executable(
#   NAME test_vlms_core
#   SRC  test/src/test_result.cpp ...
#   DEPENDS VLMS::Core vlms_testsupport
#   DISCOVER ON|OFF
#   NO_GTEST_MAIN   — target provides its own main (UI)
# )
function(build_gtest_executable)
    set(options NO_GTEST_MAIN)
    set(single_value_args NAME DISCOVER TIMEOUT GTEST_FILTER TIMEZONE_FILTER)
    set(list_args SRC DEPENDS LABELS ENVIRONMENT TIMEZONES)
    cmake_parse_arguments(PARSE_ARGV 0 test
        "${options}" "${single_value_args}" "${list_args}")

    foreach(arg IN LISTS test_UNPARSED_ARGUMENTS)
        message(WARNING "unparsed argument: ${arg}")
    endforeach()

    if(NOT "${test_NAME}" MATCHES "^test_")
        message(FATAL_ERROR "${test_NAME}: test executable name must start with test_")
    endif()

    if("${test_NAME}" MATCHES "[A-Z]")
        message(FATAL_ERROR "${test_NAME}: test executable name must be lowercase")
    endif()

    if("${test_SRC}" STREQUAL "")
        message(FATAL_ERROR "${test_NAME}: missing SRC files")
    endif()

    add_executable(${test_NAME} ${test_SRC})
    set(_gtest_libs GTest::gtest GTest::gmock)
    if(NOT test_NO_GTEST_MAIN)
        list(APPEND _gtest_libs GTest::gtest_main)
    endif()
    target_link_libraries(${test_NAME} PRIVATE ${test_DEPENDS} ${_gtest_libs})
    vlms_configure_target(${test_NAME})
    set_target_properties(${test_NAME} PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${test_NAME} PRIVATE -Wall -Wextra -Wno-error)
    endif()

    if(NOT DEFINED test_TIMEOUT OR test_TIMEOUT STREQUAL "")
        set(test_TIMEOUT 60)
    endif()
    set(_vlms_test_timeout ${test_TIMEOUT})
    set(_vlms_test_labels "${test_LABELS}")
    set(_vlms_test_environment "${test_ENVIRONMENT}")

    set(_default_args)
    if(test_GTEST_FILTER)
        list(APPEND _default_args "--gtest_filter=${test_GTEST_FILTER}")
    endif()

    if(test_DISCOVER AND NOT test_TIMEZONES AND NOT test_GTEST_FILTER)
        # PROPERTIES ENVIRONMENT is applied when ctest *runs* tests. Discovery
        # is a separate POST_BUILD launch of the same binary (--gtest_list_tests).
        # test_vlms_ui constructs QApplication in main, so that launch also
        # needs offscreen or a headless runner dies on xcb (CI default/sanitizers).
        # TEST_LAUNCHER (CMake 3.29+) prefixes both discovery and the later runs.
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.29")
            set_property(TARGET ${test_NAME} PROPERTY TEST_LAUNCHER
                "${CMAKE_COMMAND};-E;env;QT_QPA_PLATFORM=offscreen")
        endif()
        gtest_discover_tests(${test_NAME}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            PROPERTIES
                LABELS "${test_LABELS}"
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;VLMS_SCHEMA_PATH=${CMAKE_SOURCE_DIR}/database/schema.sql;VLMS_TEST_DATA_DIR=${CMAKE_SOURCE_DIR}/libraries/Core/test/data;VLMS_TEST_OCR_DATA_DIR=${CMAKE_SOURCE_DIR}/libraries/Ocr/test/data;TZ=Africa/Tunis;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/${test_NAME}"
                TIMEOUT ${test_TIMEOUT}
        )
    else()
        _vlms_add_gtest_entry(${test_NAME} ${test_NAME} "Africa/Tunis" ${_default_args})
    endif()

    if(NOT test_TIMEZONES)
        return()
    endif()

    if(NOT UNIX)
        message(STATUS
            "build_gtest_executable(${test_NAME}): TZ is ignored by the C library "
            "date path on this platform; timezone CTest entries are not registered")
        return()
    endif()

    foreach(_tz IN LISTS test_TIMEZONES)
        string(REPLACE "/" "_" _suffix "${_tz}")
        string(TOLOWER "${_suffix}" _suffix)
        set(_tz_args)
        if(test_TIMEZONE_FILTER)
            list(APPEND _tz_args "--gtest_filter=${test_TIMEZONE_FILTER}")
        endif()
        _vlms_add_gtest_entry(${test_NAME}_${_suffix} ${test_NAME} "${_tz}" ${_tz_args})
    endforeach()
endfunction()
