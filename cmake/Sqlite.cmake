# SQLite for Core. Prefers a development package (SQLite::SQLite3). Falls
# back to the runtime .so (libsqlite3.so.0) plus the declarations in
# libraries/Core/src/sqlite3_api.h, which is enough to call the C API without
# libsqlite3-dev or a vendored amalgamation.

find_package(SQLite3 QUIET)

if(SQLite3_FOUND)
    set(VLMS_SQLITE3_TARGET SQLite::SQLite3)
    set(VLMS_SQLITE3_USE_SYSTEM_HEADER ON)
    return()
endif()

find_library(VLMS_SQLITE3_LIB NAMES sqlite3 libsqlite3 libsqlite3-0 libsqlite3.so.0)
if(NOT VLMS_SQLITE3_LIB)
    message(FATAL_ERROR
        "SQLite 3 is required for Core. Install libsqlite3-dev (Linux) or "
        "MSYS2 mingw-w64-x86_64-sqlite3 (Windows MinGW) so find_package(SQLite3) succeeds.")
endif()

add_library(vlms_sqlite3 INTERFACE)
target_link_libraries(vlms_sqlite3 INTERFACE ${VLMS_SQLITE3_LIB})
set(VLMS_SQLITE3_TARGET vlms_sqlite3)
set(VLMS_SQLITE3_USE_SYSTEM_HEADER OFF)
message(STATUS "SQLite 3: ${VLMS_SQLITE3_LIB} (runtime library, bundled API header)")
