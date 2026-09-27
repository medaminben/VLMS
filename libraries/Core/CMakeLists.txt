set(TARGET_NAME Core)
set(RAW_TARGET_NAME vlms_core)

include(${CMAKE_SOURCE_DIR}/cmake/Sqlite.cmake)

set(CORE_HEADERS
    include/VLMS/Core/Clock.h
    include/VLMS/Core/Date.h
    include/VLMS/Core/DateText.h
    include/VLMS/Core/Database.h
    include/VLMS/Core/Result.h
    include/VLMS/Core/CatalogRepository.h
    include/VLMS/Core/MemberRepository.h
    include/VLMS/Core/CirculationRepository.h
    include/VLMS/Core/MetricsRepository.h
    include/VLMS/Core/LoanPolicy.h
    include/VLMS/Core/SqlText.h
)

add_library(${RAW_TARGET_NAME}
    ${CORE_HEADERS}
    src/Clock.cpp
    src/Date.cpp
    src/DateText.cpp
    src/SqlText.cpp
    src/LoanPolicy.cpp
    src/LoanSql.h
    src/LoanSql.cpp
    src/BookSql.h
    src/BookSql.cpp
    src/MemberSql.h
    src/MemberSql.cpp
    src/RepoSql.h
    src/NamedEntityStore.h
    src/NamedEntityStore.cpp
    src/BookCopyStore.h
    src/BookCopyStore.cpp
    src/CategoryStore.h
    src/CategoryStore.cpp
    src/SqliteSession.h
    src/SqliteSession.cpp
    src/Database.cpp
    src/CatalogRepository.cpp
    src/MemberRepository.cpp
    src/CirculationRepository.cpp
    src/MetricsRepository.cpp
    src/Paths.cpp
    src/Locale.cpp
    src/Strings.cpp
)
add_library(${CMAKE_ROOT_NAME}::${TARGET_NAME} ALIAS ${RAW_TARGET_NAME})

target_include_directories(${RAW_TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${RAW_TARGET_NAME}
    PRIVATE
        ${VLMS_SQLITE3_TARGET}
)

if(VLMS_SQLITE3_USE_SYSTEM_HEADER)
    target_compile_definitions(${RAW_TARGET_NAME} PRIVATE VLMS_HAS_SQLITE3_H)
endif()

vlms_configure_target(${RAW_TARGET_NAME})

# QtSupport turns AUTOMOC on for the whole project. Core has no Q_OBJECT.
set_target_properties(${RAW_TARGET_NAME} PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# Development builds resolve database/schema from the source tree.
option(VLMS_DEV_PATHS "Use source-tree paths for database and schema (development)" ON)
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set(VLMS_DEV_PATHS OFF)
endif()
if(VLMS_DEV_PATHS)
    target_compile_definitions(${RAW_TARGET_NAME} PRIVATE
        VLMS_SCHEMA_PATH="${CMAKE_SOURCE_DIR}/database/schema.sql"
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
