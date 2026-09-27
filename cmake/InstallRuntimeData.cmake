# Bundles gitignored runtime data into the install prefix when present.
# Invoked via install(SCRIPT ...) so paths are checked at install time, not configure time.

set(_vlms_data_root "${CMAKE_SOURCE_DIR}")
if(DEFINED ENV{VLMS_RUNTIME_DATA_ROOT})
  set(_vlms_data_root "$ENV{VLMS_RUNTIME_DATA_ROOT}")
endif()

set(_vlms_db "${_vlms_data_root}/database/vlms.db")
if(EXISTS "${_vlms_db}")
  file(INSTALL "${_vlms_db}" DESTINATION "${CMAKE_INSTALL_PREFIX}/database")
  message(STATUS "Installed runtime database: ${_vlms_db}")
endif()

foreach(_subdir IN ITEMS books members)
  set(_src "${_vlms_data_root}/resources/${_subdir}")
  if(NOT IS_DIRECTORY "${_src}")
    continue()
  endif()

  file(GLOB _resource_entries RELATIVE "${_src}" "${_src}/*")
  list(FILTER _resource_entries EXCLUDE REGEX "^\\.gitkeep$")
  if(NOT _resource_entries)
    message(STATUS "Skipping empty resources/${_subdir}")
    continue()
  endif()

  file(INSTALL "${_src}" DESTINATION "${CMAKE_INSTALL_PREFIX}/resources"
       USE_SOURCE_PERMISSIONS
       PATTERN ".gitkeep" EXCLUDE)
  message(STATUS "Installed resources/${_subdir} from ${_src}")
endforeach()
