set(TARGET_NAME Ocr)
set(RAW_TARGET_NAME vlms_ocr)

set(OCR_HEADERS
    include/VLMS/Ocr/Ocr.h
)

add_library(${RAW_TARGET_NAME}
    ${OCR_HEADERS}
    src/Tessdata.h
    src/Tessdata.cpp
    src/Ocr.cpp
    src/Job.cpp
)
add_library(${CMAKE_ROOT_NAME}::${TARGET_NAME} ALIAS ${RAW_TARGET_NAME})

target_include_directories(${RAW_TARGET_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# No Qt, deliberately, and no Core either.
#
# This target links std and Tesseract and nothing else, which is what makes
# "the OCR module does not depend on Qt" a link error instead of a rule in a
# comment -- the same device as Core linking sqlite3, not Qt. It is also the
# whole reason the module can be lifted into its own repository later: cut the
# directory out, keep OcrBundle.cmake, and nothing else has to move.
#
# Threads is required rather than optional: Job runs on a std::thread, and on
# MinGW that pulls in winpthread -- already shipped beside the OCR DLLs, see
# the note in scripts/fetch_windows_ocr.ps1.
find_package(Threads REQUIRED)
target_link_libraries(${RAW_TARGET_NAME}
    PUBLIC
        Threads::Threads
)

vlms_configure_target(${RAW_TARGET_NAME})

# QtSupport turns AUTOMOC on for the whole project. There is no Q_OBJECT here
# and there never should be, so scanning for one is wasted build time and a
# misleading signal to anyone reading the target.
set_target_properties(${RAW_TARGET_NAME} PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

# Tesseract, Leptonica, and the runtime tessdata/DLL bundle. Included here
# rather than from the application so that a tests-only build (BUILD_APPS=OFF)
# links a real engine instead of silently exercising the unavailable path.
#
# Building without Tesseract stays supported: every entry point then reports
# Status::Unavailable and the UI disables the button.
set(VLMS_OCR_TARGET ${RAW_TARGET_NAME})
include(${CMAKE_SOURCE_DIR}/cmake/OcrBundle.cmake)

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
