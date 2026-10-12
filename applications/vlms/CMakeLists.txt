# Everything except main.cpp lives in a static library so the dialogs can be
# linked into test executables. The .qrc files stay on the executable: Qt
# resources in a static library need Q_INIT_RESOURCE, and no test exercises
# Theme::setupFonts.
add_library(vlms_ui STATIC
    src/ui/shell/Application.cpp
    src/ui/shell/MainWindow.cpp
    src/ui/shell/LanguageSelector.cpp
    src/ui/shell/ManualLocation.cpp
    src/ui/services/Theme.cpp
    src/ui/services/OcrService.cpp
    src/ui/views/FlagIcons.cpp
    src/ui/views/ClickableLabel.cpp
    src/ui/views/LicenceDialog.cpp
    src/ui/views/ImageViewerDialog.cpp
    src/ui/views/UiHelpers.cpp
    src/ui/views/ListPagePager.cpp
    src/ui/views/WidgetTableSort.cpp
    src/ui/views/WidgetTableChecks.cpp
    src/ui/views/BulkAction.cpp
    src/ui/views/ListPageFrame.cpp
    src/ui/views/ActionBar.cpp
    src/ui/views/ListPage.cpp
    src/ui/views/FacetList.cpp
    src/ui/views/PreviewImages.cpp
    src/ui/views/BookFacetFilters.cpp
    src/ui/views/BookEditorDialog.cpp
    src/ui/views/BookCopiesTable.cpp
    src/ui/views/CategoryManagerDialog.cpp
    src/ui/views/LocalNumberDelegate.cpp
    src/ui/views/FreeLocalNumberDelegate.cpp
    src/ui/views/MemberFacetFilters.cpp
    src/ui/views/MemberEditorDialog.cpp
    src/ui/views/BirthDateEdit.cpp
    src/ui/views/LoanCheckoutDialog.cpp
    src/ui/views/LoanExtendDialog.cpp
    src/ui/views/LoanReturnDialog.cpp
    src/ui/views/LoanHistoryDialog.cpp
    src/ui/views/MetricsPage.cpp
    src/ui/views/ReuseNumberFlow.cpp
    src/ui/workflows/DataEvents.cpp
    src/ui/workflows/QtErrorPresenter.cpp
    src/ui/workflows/QtDialogFactory.cpp
    src/ui/presentation/ReadSources.cpp
    src/ui/workflows/BulkFlow.cpp
    src/ui/workflows/BulkRunner.cpp
    src/ui/workflows/BookWorkflow.cpp
    src/ui/workflows/MemberWorkflow.cpp
    src/ui/workflows/LoanWorkflow.cpp
    src/ui/workflows/ArchiveWorkflow.cpp
    src/ui/presentation/PagedQueryModel.cpp
    src/ui/presentation/MemberListSpec.cpp
    src/ui/presentation/BookListSpec.cpp
    src/ui/presentation/LoanListSpec.cpp
    src/ui/presentation/ArchivedMemberSpec.cpp
    src/ui/presentation/ArchivedBookSpec.cpp
    src/ui/presentation/ArchivedCopySpec.cpp
    src/ui/presentation/ArchivedLoanSpec.cpp
    src/ui/presentation/QtCatalogLookups.cpp
    src/ui/presentation/QtMemberLookups.cpp
    src/ui/presentation/QtCirculationLookups.cpp

    src/ui/shell/Application.h
    src/ui/shell/MainWindow.h
    src/ui/shell/Navigator.h
    src/ui/shell/LanguageSelector.h
    src/ui/shell/ManualLocation.h
    src/ui/services/Theme.h
    src/ui/services/OcrService.h
    src/ui/views/FlagIcons.h
    src/ui/views/ClickableLabel.h
    src/ui/views/LicenceDialog.h
    src/ui/views/ImageViewerDialog.h
    src/ui/views/UiHelpers.h
    src/ui/views/ListPagePager.h
    src/ui/views/WidgetTableSort.h
    src/ui/views/WidgetTableChecks.h
    src/ui/views/BulkAction.h
    src/ui/views/ListPageFrame.h
    src/ui/views/ActionBar.h
    src/ui/views/ListPage.h
    src/ui/views/FacetList.h
    src/ui/views/PreviewImages.h
    src/ui/views/BookFacetFilters.h
    src/ui/views/BookEditorDialog.h
    src/ui/views/BookCopiesTable.h
    src/ui/views/CategoryManagerDialog.h
    src/ui/views/LocalNumberDelegate.h
    src/ui/views/FreeLocalNumberDelegate.h
    src/ui/views/MemberFacetFilters.h
    src/ui/views/MemberEditorDialog.h
    src/ui/views/BirthDateEdit.h
    src/ui/views/LoanCheckoutDialog.h
    src/ui/views/LoanExtendDialog.h
    src/ui/views/LoanReturnDialog.h
    src/ui/views/LoanHistoryDialog.h
    src/ui/views/MetricsPage.h
    src/ui/views/ReuseNumberFlow.h
    src/ui/workflows/DataEvents.h
    src/ui/workflows/ErrorPresenter.h
    src/ui/workflows/QtErrorPresenter.h
    src/ui/workflows/Forms.h
    src/ui/workflows/DialogFactory.h
    src/ui/workflows/QtDialogFactory.h
    src/ui/presentation/ReadSources.h
    src/ui/workflows/BulkFlow.h
    src/ui/workflows/CategoryEditor.h
    src/ui/workflows/BulkRunner.h
    src/ui/workflows/BookWorkflow.h
    src/ui/workflows/MemberWorkflow.h
    src/ui/workflows/LoanWorkflow.h
    src/ui/workflows/ArchiveWorkflow.h
    src/ui/presentation/QueryState.h
    src/ui/presentation/ColumnSpec.h
    src/ui/presentation/FacetSpec.h
    src/ui/presentation/PageAction.h
    src/ui/presentation/PreviewContent.h
    src/ui/presentation/ListSpec.h
    src/ui/presentation/PagedQueryModel.h
    src/ui/presentation/MemberListSpec.h
    src/ui/presentation/BookListSpec.h
    src/ui/presentation/LoanListSpec.h
    src/ui/presentation/ArchivedMemberSpec.h
    src/ui/presentation/ArchivedBookSpec.h
    src/ui/presentation/ArchivedCopySpec.h
    src/ui/presentation/ArchivedLoanSpec.h
    src/ui/presentation/CatalogLookups.h
    src/ui/presentation/QtCatalogLookups.h
    src/ui/presentation/MemberLookups.h
    src/ui/presentation/QtMemberLookups.h
    src/ui/presentation/CirculationLookups.h
    src/ui/presentation/QtCirculationLookups.h
)

target_include_directories(vlms_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The version from the latest tag (cmake/GitVersion.cmake): the bare X.Y.Z on
# a release, X.Y.Z+N.gSHA on a commit past one, so a dev build never passes
# for a release in the About text or a bug report.
target_compile_definitions(vlms_ui PRIVATE VLMS_VERSION="${VLMS_VERSION_FULL}")

target_link_libraries(vlms_ui
    PUBLIC
        ${CMAKE_ROOT_NAME}::Repositories
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
