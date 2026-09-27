include(CompilerWarnings)

function(vlms_configure_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_20)
    vlms_set_warnings(${target_name})
endfunction()

function(create_application)
    set(options)
    set(single_value_args NAME ENTRY UI)
    set(list_args HEADERS SOURCES RESOURCES BUILD_ARGS DEPENDENCIES)
    cmake_parse_arguments(PARSE_ARGV 0 app
        "${options}" "${single_value_args}" "${list_args}")

    foreach(arg IN LISTS app_UNPARSED_ARGUMENTS)
        message(WARNING "create_application: unparsed argument: ${arg}")
    endforeach()

    if(NOT DEFINED app_ENTRY OR app_ENTRY STREQUAL "")
        message(FATAL_ERROR "create_application: missing ENTRY [Console|QT_ui]")
    endif()

    set(PROJECT_FILES ${app_HEADERS} ${app_SOURCES} ${app_UI} ${app_RESOURCES})

    if(app_ENTRY STREQUAL "Console")
        add_executable(${app_NAME} ${app_BUILD_ARGS} ${PROJECT_FILES})
        if(app_DEPENDENCIES)
            target_link_libraries(${app_NAME} PRIVATE ${app_DEPENDENCIES})
        endif()
        vlms_configure_target(${app_NAME})

    elseif(BUILD_QT_UI AND app_ENTRY STREQUAL "QT_ui")
        qt_add_executable(${app_NAME} ${app_BUILD_ARGS} ${PROJECT_FILES})
        if(app_DEPENDENCIES)
            target_link_libraries(${app_NAME} PRIVATE ${app_DEPENDENCIES})
        endif()
        set_target_properties(${app_NAME} PROPERTIES
            WIN32_EXECUTABLE TRUE
            MACOSX_BUNDLE TRUE
        )
        vlms_configure_target(${app_NAME})
    endif()
endfunction()
