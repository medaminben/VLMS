# Dialog tests link vlms_ui. They never construct Application or
# MainWindow: MainWindow reaches for qobject_cast<Application*>(qApp), and
# Application's constructor opens the real source-tree database.

build_gtest_executable(
    NAME test_vlms_ui
    SRC
        src/test_main.cpp
        src/test_loan_dialogs.cpp
        src/test_dialog_translations.cpp
        src/test_birth_date_edit.cpp
        src/test_member_removal.cpp
        src/test_member_filters.cpp
        src/test_member_editor_status.cpp
        src/test_member_image_viewer.cpp
        src/test_circulation_filters.cpp
        src/test_member_sort.cpp
        src/test_catalog_sort.cpp
        src/test_book_editor_ocr.cpp
        src/test_list_page_frame.cpp
        src/test_theme_modes.cpp
        src/test_preview_theme.cpp
        src/test_flag_icons.cpp
        src/test_language_selector.cpp
        src/test_table_header_sort.cpp
        src/test_table_row_checks.cpp
        src/test_bulk_action.cpp
        src/test_bulk_pages.cpp
        src/test_bulk_copies.cpp
        src/test_table_pager.cpp
        src/test_category_sort.cpp
        src/test_metrics_activity_sort.cpp
        src/test_metrics_member_tiles.cpp
        src/test_archive_page.cpp
        src/test_archive_catalog_ui.cpp
        src/test_archive_circulation_ui.cpp
        src/test_local_number_delegate.cpp
        src/test_catalog_local_number.cpp
        src/test_catalog_loans.cpp
        src/test_catalog_removal.cpp
        src/test_free_number_picker.cpp
        src/test_member_loans_actions.cpp
        src/test_clickable_label.cpp
        src/test_licence_dialog.cpp
        src/test_image_viewer_dialog.cpp
        src/test_manual_location.cpp
        src/test_page_facets.cpp
    DEPENDS
        vlms_testsupport
        vlms_ui
        ${CMAKE_ROOT_NAME}::Ocr
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Test
    DISCOVER ON
    NO_GTEST_MAIN
    LABELS ui
)
target_include_directories(test_vlms_ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(test_vlms_ui PROPERTIES
    AUTOMOC ON
    AUTORCC ON
)
# The artwork the pages paint -- the cover placeholder above all. Without it
# every placeholder pixmap is null here, and a test of what a placeholder looks
# like in either theme would pass on an empty label.
target_sources(test_vlms_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../resources/images.qrc)
