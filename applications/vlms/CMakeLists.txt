# Everything except main.cpp lives in a static library so the dialogs can be
# linked into test executables. The .qrc files stay on the executable: Qt
# resources in a static library need Q_INIT_RESOURCE, and no test exercises
# Theme::setupFonts.
add_library(vlms_ui STATIC
    src/Application.cpp
    src/ui/Theme.cpp
    src/ui/FlagIcons.cpp
    src/ui/LanguageSelector.cpp
    src/ui/ClickableLabel.cpp
    src/ui/LicenceDialog.cpp
    src/ui/ImageViewerDialog.cpp
    src/ui/ManualLocation.cpp
    src/ui/UiHelpers.cpp
    src/ui/TablePager.cpp
    src/ui/TableHeaderSort.cpp
    src/ui/TableRowChecks.cpp
    src/ui/BulkAction.cpp
    src/ui/ListPageFrame.cpp
    src/ui/FacetList.cpp
    src/ui/PreviewImages.cpp
    src/ui/MainWindow.cpp
    src/ui/catalog/CatalogPage.cpp
    src/ui/catalog/BookFacetFilters.cpp
    src/ui/catalog/BookEditorDialog.cpp
    src/ui/catalog/BookCopiesTable.cpp
    src/ui/catalog/BookOcrController.cpp
    src/ui/catalog/CategoryManagerDialog.cpp
    src/ui/catalog/BookLoansDialog.cpp
    src/ui/catalog/LocalNumberDelegate.cpp
    src/ui/catalog/FreeLocalNumberDelegate.cpp
    src/ui/members/MembersPage.cpp
    src/ui/members/MemberFacetFilters.cpp
    src/ui/members/MemberEditorDialog.cpp
    src/ui/members/BirthDateEdit.cpp
    src/ui/members/MemberLoansDialog.cpp
    src/ui/circulation/CirculationPage.cpp
    src/ui/circulation/LoanCheckoutDialog.cpp
    src/ui/circulation/LoanExtendDialog.cpp
    src/ui/circulation/LoanReturnDialog.cpp
    src/ui/circulation/LoanHistoryActions.cpp
    src/ui/metrics/MetricsPage.cpp
    src/ui/archive/ArchivePage.cpp
    src/ui/archive/ArchiveLoansDialog.cpp
    src/ui/archive/ReuseNumberFlow.cpp

    src/Application.h
    src/ui/Theme.h
    src/ui/FlagIcons.h
    src/ui/LanguageSelector.h
    src/ui/ClickableLabel.h
    src/ui/LicenceDialog.h
    src/ui/ImageViewerDialog.h
    src/ui/ManualLocation.h
    src/ui/UiHelpers.h
    src/ui/TablePager.h
    src/ui/TableHeaderSort.h
    src/ui/TableRowChecks.h
    src/ui/BulkAction.h
    src/ui/ListPageFrame.h
    src/ui/FacetList.h
    src/ui/PreviewImages.h
    src/ui/MainWindow.h
    src/ui/catalog/CatalogPage.h
    src/ui/catalog/BookFacetFilters.h
    src/ui/catalog/BookEditorDialog.h
    src/ui/catalog/BookCopiesTable.h
    src/ui/catalog/BookOcrController.h
    src/ui/catalog/CategoryManagerDialog.h
    src/ui/catalog/BookLoansDialog.h
    src/ui/catalog/LocalNumberDelegate.h
    src/ui/catalog/FreeLocalNumberDelegate.h
    src/ui/members/MembersPage.h
    src/ui/members/MemberFacetFilters.h
    src/ui/members/MemberEditorDialog.h
    src/ui/members/BirthDateEdit.h
    src/ui/members/MemberLoansDialog.h
    src/ui/circulation/CirculationPage.h
    src/ui/circulation/LoanCheckoutDialog.h
    src/ui/circulation/LoanExtendDialog.h
    src/ui/circulation/LoanReturnDialog.h
    src/ui/circulation/LoanHistoryActions.h
    src/ui/metrics/MetricsPage.h
    src/ui/archive/ArchivePage.h
    src/ui/archive/ArchiveLoansDialog.h
    src/ui/archive/ReuseNumberFlow.h
)

target_include_directories(vlms_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The version from the latest tag (cmake/GitVersion.cmake): the bare X.Y.Z on
# a release, X.Y.Z+N.gSHA on a commit past one, so a dev build never passes
# for a release in the About text or a bug report.
target_compile_definitions(vlms_ui PRIVATE VLMS_VERSION="${VLMS_VERSION_FULL}")

target_link_libraries(vlms_ui
    PUBLIC
        ${CMAKE_ROOT_NAME}::Core
        Qt${QT_VERSION_MAJOR}::Widgets
    PRIVATE
        # PRIVATE: BookEditorDialog is the only thing that reads a book cover,
        # and OCR has no business leaking into the rest of the UI.
        ${CMAKE_ROOT_NAME}::Ocr
)

vlms_configure_target(vlms_ui)

# VLMS_QT_CATALOGUE_QRC stays unset when the Qt in use ships no translations,
# and an unset variable expands to nothing in the RESOURCES list below.
include(${CMAKE_SOURCE_DIR}/cmake/QtCatalogue.cmake)
vlms_generate_qt_catalogue(VLMS_QT_CATALOGUE_QRC)

create_application(
    NAME vlms
    ENTRY QT_ui
    SOURCES
        src/main.cpp
    RESOURCES
        resources/fonts.qrc
        resources/images.qrc
        ${VLMS_QT_CATALOGUE_QRC}
    DEPENDENCIES
        vlms_ui
)

# The user manual's screenshot tool. Off by default: it is a maintainer tool, run by
# scripts/manual/capture.sh, never shipped.
option(VLMS_MANUAL_CAPTURE "Build the manual screenshot tool" OFF)
if(VLMS_MANUAL_CAPTURE)
    add_subdirectory(manual_capture)
endif()

if(WIN32)
    set(VLMS_WIN_APP_DIR ".")
    install(TARGETS vlms RUNTIME DESTINATION ${VLMS_WIN_APP_DIR})
    install(FILES ${CMAKE_SOURCE_DIR}/database/schema.sql DESTINATION ${VLMS_WIN_APP_DIR})
    # The user manual, opened by the ? beside the theme toggle. Only the generated pages and
    # their assets: the Markdown sources and the template stay in the repository.
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/docs/manual/
            DESTINATION ${VLMS_WIN_APP_DIR}/manual
            PATTERN "content" EXCLUDE
            PATTERN "template.html" EXCLUDE)
    if(MINGW)
        include(${CMAKE_SOURCE_DIR}/cmake/MinGWRuntime.cmake)
    endif()
else()
    install(TARGETS vlms RUNTIME DESTINATION bin)
    install(FILES ${CMAKE_SOURCE_DIR}/database/schema.sql DESTINATION share/vlms)

    # QApplication::setWindowIcon is enough for X11 and Windows, but a Wayland
    # compositor never sees a per-window icon: it matches the surface's app id
    # against an installed .desktop file and takes the icon from there. Without
    # these two, the window and the task bar get the shell's fallback icon.
    install(FILES resources/vlms.desktop DESTINATION share/applications)
    foreach(size 16 24 32 48 64 128 256)
        install(
            FILES resources/images/brand/icon-${size}.png
            DESTINATION share/icons/hicolor/${size}x${size}/apps
            RENAME vlms.png)
    endforeach()
endif()

# OcrBundle.cmake moved to libraries/Ocr: Tesseract now attaches to the Qt-free
# OCR library, and doing it there means a tests-only build (BUILD_APPS=OFF) gets
# a real engine instead of silently testing the unavailable path.

if(WIN32)
    include(${CMAKE_SOURCE_DIR}/cmake/WindowsPackaging.cmake)
endif()

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
