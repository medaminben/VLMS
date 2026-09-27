# Locate Tesseract + Leptonica for in-process OCR (DLL/.so, no CLI).
#
# Search order:
#   1. third_party/tesseract/<platform>  (Windows customer/build SDK tree)
#   2. CMake package / pkg-config (Linux developer packages)
#
# Windows third_party layout (build machine):
#
#   windows/
#     include/...   lib/*.lib          # build only — NOT installed
#     *.dll                            # runtime
#     tessdata/{ara,fra,eng}.traineddata
#
# Windows customer app folder (self-contained, no system pollution):
#
#   <app>/
#     vlms.exe
#     tesseract*.dll
#     leptonica-*.dll
#     (other OCR dependency DLLs)
#     tessdata/
#       ara.traineddata
#       fra.traineddata
#       eng.traineddata
#
# Nothing is written to System32, PATH, or a machine-wide Tesseract install.

# Build as though Tesseract were not installed, even when it is.
#
# The no-engine path is what a customer without language packs runs, and it is
# the one nobody exercises by hand. Without this switch it cannot be tested on
# a development machine at all: clearing the find_library cache entries just
# makes CMake find the system copy again on the next configure.
option(VLMS_DISABLE_OCR "Build without Tesseract even if it is installed" OFF)

set(VLMS_OCR_THIRD_PARTY_ROOT "${CMAKE_SOURCE_DIR}/third_party/tesseract")
if(WIN32)
  set(VLMS_OCR_PLATFORM_DIR "${VLMS_OCR_THIRD_PARTY_ROOT}/windows")
else()
  set(VLMS_OCR_PLATFORM_DIR "${VLMS_OCR_THIRD_PARTY_ROOT}/linux")
endif()

set(VLMS_TESSERACT_FOUND FALSE)
set(VLMS_TESSERACT_INCLUDE_DIRS "")
set(VLMS_TESSERACT_LIBRARIES "")

# --- Windows / staged SDK tree ---
if(VLMS_DISABLE_OCR)
  message(STATUS "VLMS OCR: disabled by VLMS_DISABLE_OCR")
elseif(EXISTS "${VLMS_OCR_PLATFORM_DIR}/include/tesseract/baseapi.h")
  set(_ocr_inc "${VLMS_OCR_PLATFORM_DIR}/include")
  set(_ocr_libdir "${VLMS_OCR_PLATFORM_DIR}/lib")
  if(NOT EXISTS "${_ocr_libdir}")
    set(_ocr_libdir "${VLMS_OCR_PLATFORM_DIR}")
  endif()

  if(MINGW)
    find_library(VLMS_TESSERACT_LIB
      NAMES libtesseract.dll.a tesseract.dll.a tesseract
      PATHS "${_ocr_libdir}" "${VLMS_OCR_PLATFORM_DIR}"
      NO_DEFAULT_PATH)
    find_library(VLMS_LEPTONICA_LIB
      NAMES libleptonica.dll.a leptonica.dll.a leptonica lept lept-4
      PATHS "${_ocr_libdir}" "${VLMS_OCR_PLATFORM_DIR}"
      NO_DEFAULT_PATH)
  else()
    find_library(VLMS_TESSERACT_LIB
      NAMES tesseract tesseract55 tesseract54 tesseract53 tesseract52 tesseract51 tesseract50
      PATHS "${_ocr_libdir}" "${VLMS_OCR_PLATFORM_DIR}"
      NO_DEFAULT_PATH)
    find_library(VLMS_LEPTONICA_LIB
      NAMES leptonica lept lept-4 libleptonica
      PATHS "${_ocr_libdir}" "${VLMS_OCR_PLATFORM_DIR}"
      NO_DEFAULT_PATH)
  endif()

  if(VLMS_TESSERACT_LIB AND VLMS_LEPTONICA_LIB)
    set(VLMS_TESSERACT_FOUND TRUE)
    set(VLMS_TESSERACT_INCLUDE_DIRS "${_ocr_inc}")
    set(VLMS_TESSERACT_LIBRARIES "${VLMS_TESSERACT_LIB}" "${VLMS_LEPTONICA_LIB}")
    message(STATUS "VLMS OCR: using third_party SDK at ${VLMS_OCR_PLATFORM_DIR}")
  endif()
endif()

# --- System packages (Linux developer only) ---
if(NOT VLMS_TESSERACT_FOUND AND NOT WIN32 AND NOT VLMS_DISABLE_OCR)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(PC_TESSERACT QUIET tesseract)
    pkg_check_modules(PC_LEPT QUIET lept)
  endif()

  find_path(VLMS_TESSERACT_INCLUDE_DIR tesseract/baseapi.h
    HINTS ${PC_TESSERACT_INCLUDE_DIRS})
  find_library(VLMS_TESSERACT_LIB_SYS NAMES tesseract
    HINTS ${PC_TESSERACT_LIBRARY_DIRS})
  find_library(VLMS_LEPTONICA_LIB_SYS NAMES lept leptonica
    HINTS ${PC_LEPT_LIBRARY_DIRS})

  if(VLMS_TESSERACT_INCLUDE_DIR AND VLMS_TESSERACT_LIB_SYS AND VLMS_LEPTONICA_LIB_SYS)
    set(VLMS_TESSERACT_FOUND TRUE)
    set(VLMS_TESSERACT_INCLUDE_DIRS "${VLMS_TESSERACT_INCLUDE_DIR}")
    set(VLMS_TESSERACT_LIBRARIES "${VLMS_TESSERACT_LIB_SYS}" "${VLMS_LEPTONICA_LIB_SYS}")
    message(STATUS "VLMS OCR: using system Tesseract (${VLMS_TESSERACT_LIB_SYS})")
  endif()
endif()

if(NOT DEFINED VLMS_OCR_TARGET)
  set(VLMS_OCR_TARGET vlms_ocr)
endif()

if(VLMS_TESSERACT_FOUND)
  target_compile_definitions(${VLMS_OCR_TARGET} PRIVATE VLMS_HAS_TESSERACT=1)
  target_include_directories(${VLMS_OCR_TARGET} PRIVATE ${VLMS_TESSERACT_INCLUDE_DIRS})
  target_link_libraries(${VLMS_OCR_TARGET} PRIVATE ${VLMS_TESSERACT_LIBRARIES})
elseif(VLMS_DISABLE_OCR)
  message(STATUS "VLMS OCR: not linked (VLMS_DISABLE_OCR) — OCR UI will be disabled.")
else()
  message(WARNING "VLMS OCR: Tesseract libraries not found — OCR UI will be disabled.")
  if(WIN32)
    if(MINGW)
      message(STATUS "  MinGW: scripts/fetch_windows_ocr.ps1 -Toolchain MinGW (MSYS2 packages)")
    else()
      message(STATUS "  MSVC: scripts/fetch_windows_ocr.ps1 -SourceDir 'C:\\Program Files\\Tesseract-OCR'")
    endif()
    message(STATUS "  Or see third_party/tesseract/README.md")
  else()
    message(STATUS "  Install: libtesseract-dev libleptonica-dev")
    message(STATUS "  and language packs: tesseract-ocr-ara tesseract-ocr-fra tesseract-ocr-eng")
  endif()
endif()

# --- Runtime bundle: DLLs + tessdata only when OCR was linked successfully ---
set(VLMS_OCR_BUNDLE_DIR "")
if(VLMS_TESSERACT_FOUND AND EXISTS "${VLMS_OCR_PLATFORM_DIR}/tessdata/eng.traineddata")
  set(VLMS_OCR_BUNDLE_DIR "${VLMS_OCR_PLATFORM_DIR}")
endif()

if(VLMS_OCR_BUNDLE_DIR)
  message(STATUS "VLMS OCR tessdata bundle: ${VLMS_OCR_BUNDLE_DIR}")
  target_compile_definitions(${VLMS_OCR_TARGET} PRIVATE
    VLMS_OCR_ROOT="${VLMS_OCR_BUNDLE_DIR}"
  )

  if(WIN32)
    # Portable app folder: same directory as vlms.exe (see VLMS_WIN_APP_DIR).
    set(_vlms_win_app_dir ".")
    if(DEFINED VLMS_WIN_APP_DIR)
      set(_vlms_win_app_dir "${VLMS_WIN_APP_DIR}")
    endif()

    file(GLOB VLMS_OCR_RUNTIME_DLLS "${VLMS_OCR_BUNDLE_DIR}/*.dll")
    if(VLMS_OCR_RUNTIME_DLLS)
      install(FILES ${VLMS_OCR_RUNTIME_DLLS} DESTINATION "${_vlms_win_app_dir}")
    endif()

    install(DIRECTORY "${VLMS_OCR_BUNDLE_DIR}/tessdata"
            DESTINATION "${_vlms_win_app_dir}"
            FILES_MATCHING
              PATTERN "ara.traineddata"
              PATTERN "fra.traineddata"
              PATTERN "eng.traineddata")
  else()
    install(DIRECTORY "${VLMS_OCR_BUNDLE_DIR}/tessdata"
            DESTINATION ocr
            FILES_MATCHING
              PATTERN "ara.traineddata"
              PATTERN "fra.traineddata"
              PATTERN "eng.traineddata")
    if(EXISTS "${VLMS_OCR_BUNDLE_DIR}/lib")
      install(DIRECTORY "${VLMS_OCR_BUNDLE_DIR}/lib"
              DESTINATION ocr
              USE_SOURCE_PERMISSIONS)
    endif()
  endif()
else()
  message(STATUS "VLMS OCR tessdata not found at ${VLMS_OCR_PLATFORM_DIR}/tessdata")
  if(NOT VLMS_TESSERACT_FOUND AND WIN32 AND EXISTS "${VLMS_OCR_PLATFORM_DIR}/tessdata/eng.traineddata")
    message(WARNING
      "VLMS OCR: tessdata present but libraries not linked — MSVC OCR DLLs are NOT installed.")
  endif()
  if(WIN32)
    message(STATUS "  See third_party/tesseract/README.md and scripts/fetch_windows_ocr.ps1")
  else()
    message(STATUS "  Run scripts/stage_linux_ocr.sh to copy ara/fra/eng tessdata for local runs,")
    message(STATUS "  or rely on system /usr/share/tesseract-ocr/*/tessdata.")
  endif()
endif()
