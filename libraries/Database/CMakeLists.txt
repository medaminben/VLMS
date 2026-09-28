set(TARGET_NAME Database)
set(RAW_TARGET_NAME vlms_database)

include(${CMAKE_SOURCE_DIR}/cmake/Sqlite.cmake)

set(DATABASE_HEADERS
    include/VLMS/Database/Connection.h
    include/VLMS/Database/SqliteSession.h
    include/VLMS/Database/SqlText.h
)

add_library(${RAW_TARGET_NAME}
    ${DATABASE_HEADERS}
    src/sqlite3_api.h
    src/Connection.cpp
    src/SqliteSession.cpp
    src/SqlText.cpp
)
add_library(${CMAKE_ROOT_NAME}::${TARGET_NAME} ALIAS ${RAW_TARGET_NAME})

target_include_directories(${RAW_TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# sqlite3 is PRIVATE: SqliteSession.h only forward-declares sqlite3, so no
# consumer of this library ever sees the C API.
target_link_libraries(${RAW_TARGET_NAME}
    PUBLIC
        ${CMAKE_ROOT_NAME}::Core
    PRIVATE
        ${VLMS_SQLITE3_TARGET}
)

if(VLMS_SQLITE3_USE_SYSTEM_HEADER)
    target_compile_definitions(${RAW_TARGET_NAME} PRIVATE VLMS_HAS_SQLITE3_H)
endif()

vlms_configure_target(${RAW_TARGET_NAME})

set_target_properties(${RAW_TARGET_NAME} PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

if(VLMS_DEV_PATHS)
    target_compile_definitions(${RAW_TARGET_NAME} PRIVATE
        VLMS_SCHEMA_PATH="${CMAKE_SOURCE_DIR}/database/schema.sql"
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
