# Development builds resolve database/schema and the project root from the
# source tree. Core reads the root and Database the schema, so the option
# lives here where both see the same value.
option(VLMS_DEV_PATHS "Use source-tree paths for database and schema (development)" ON)
if(WIN32 AND CMAKE_BUILD_TYPE STREQUAL "Release")
    set(VLMS_DEV_PATHS OFF)
endif()

add_subdirectory(Ocr)
add_subdirectory(Core)
add_subdirectory(Database)
add_subdirectory(Repositories)
