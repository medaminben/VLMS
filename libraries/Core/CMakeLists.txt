set(TARGET_NAME Core)
set(RAW_TARGET_NAME vlms_core)

set(CORE_HEADERS
    include/VLMS/Core/Clock.h
    include/VLMS/Core/Date.h
    include/VLMS/Core/DateText.h
    include/VLMS/Core/Locale.h
    include/VLMS/Core/Paths.h
    include/VLMS/Core/Result.h
    include/VLMS/Core/Strings.h
    include/VLMS/Core/Text.h
)

add_library(${RAW_TARGET_NAME}
    ${CORE_HEADERS}
    src/Clock.cpp
    src/Date.cpp
    src/DateText.cpp
    src/Locale.cpp
    src/Paths.cpp
    src/Strings.cpp
)
add_library(${CMAKE_ROOT_NAME}::${TARGET_NAME} ALIAS ${RAW_TARGET_NAME})

target_include_directories(${RAW_TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# No links, deliberately: Core is std only. SQLite belongs to VLMS::Database,
# and Qt to the application.

vlms_configure_target(${RAW_TARGET_NAME})

# QtSupport turns AUTOMOC on for the whole project. Core has no Q_OBJECT.
set_target_properties(${RAW_TARGET_NAME} PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

if(VLMS_DEV_PATHS)
    target_compile_definitions(${RAW_TARGET_NAME} PRIVATE
        VLMS_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    )
endif()

install(TARGETS ${RAW_TARGET_NAME}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
