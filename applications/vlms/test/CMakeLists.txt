# Dialog tests link vlms_ui. They build pages against temporary databases and
# FakeNavigator/DataEvents; MainWindow needs a real Application and is only
# covered lightly (or not at all) here.

build_gtest_executable(
    NAME test_vlms_ui
    SRC
        src/test_main.cpp
        src/test_loan_dialogs.cpp
        src/test_plain_text.cpp
        src/test_dialog_translations.cpp
        src/test_birth_date_edit.cpp
        src/test_member_removal.cpp
        src/test_member_filters.cpp
        src/test_member_editor_status.cpp
        src/test_member_image_viewer.cpp
        src/test_circulation_filters.cpp
        src/test_book_editor_ocr.cpp
        src/test_book_editor_lookups.cpp
        src/test_list_page_frame.cpp
        src/test_theme_modes.cpp
        src/test_preview_theme.cpp
        src/test_flag_icons.cpp
        src/test_language_selector.cpp
        src/test_bulk_action.cpp
        src/test_bulk_pages.cpp
        src/test_bulk_copies.cpp
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
        src/test_loan_history_dialog.cpp
        src/test_clickable_label.cpp
        src/test_licence_dialog.cpp
        src/test_image_viewer_dialog.cpp
        src/test_manual_location.cpp
        src/test_page_facets.cpp
        src/test_data_events.cpp
        src/test_paged_query_model.cpp
        src/test_list_page.cpp
        src/test_list_page_shortcuts.cpp
        src/test_error_presenter.cpp
        src/test_dialog_factory.cpp
        src/test_string_keys_used.cpp
        src/test_navigator.cpp
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
target_compile_definitions(test_vlms_ui PRIVATE VLMS_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
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
# The LICENSE file itself, which the licence dialog shows word for word.
target_sources(test_vlms_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../resources/licence.qrc)

# LanguageChange path needs Application as the QApplication (setUiLocale
# broadcast). Keeping it off the shared QApplication test_main avoids a second
# QApplication in the main UI suite.
build_gtest_executable(
    NAME test_vlms_language_change
    SRC
        src/test_language_change_main.cpp
        src/test_language_change.cpp
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
target_include_directories(test_vlms_language_change PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(test_vlms_language_change PROPERTIES
    AUTOMOC ON
    AUTORCC ON
)
target_sources(test_vlms_language_change PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../resources/images.qrc)

build_gtest_executable(
    NAME test_vlms_workflows
    SRC
        src/test_main.cpp
        src/test_book_workflow.cpp
        src/test_member_workflow.cpp
        src/test_loan_workflow.cpp
        src/test_archive_workflow.cpp
        src/test_bulk_runner.cpp
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
target_include_directories(test_vlms_workflows PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(test_vlms_workflows PROPERTIES
    AUTOMOC ON
    AUTORCC ON
)
target_sources(test_vlms_workflows PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../resources/images.qrc)

# MainWindow on the real Application, with the project root moved to a
# temporary folder so the suite never opens database/vlms.db.
build_gtest_executable(
    NAME test_vlms_shell
    SRC
        src/test_shell_main.cpp
        src/test_shell.cpp
        src/test_instance_lock.cpp
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
target_include_directories(test_vlms_shell PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(test_vlms_shell PROPERTIES
    AUTOMOC ON
    AUTORCC ON
)
target_sources(test_vlms_shell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../resources/images.qrc)

# End-to-end passes through the real dialogs (spec: test_vlms_smoke).
build_gtest_executable(
    NAME test_vlms_smoke
    SRC
        src/test_main.cpp
        src/test_vlms_smoke.cpp
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
target_include_directories(test_vlms_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(test_vlms_smoke PROPERTIES
    AUTOMOC ON
    AUTORCC ON
)
target_sources(test_vlms_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../resources/images.qrc)

# Presentation ListSpec adapters (Member/Book/Loan/Archive). Kept separate from
# the large widget suite so discovery and -L ui stay fast as specs grow.
build_gtest_executable(
    NAME test_vlms_presentation
    SRC
        src/test_main.cpp
        src/test_member_list_spec.cpp
        src/test_book_list_spec.cpp
        src/test_loan_list_spec.cpp
        src/test_archived_member_spec.cpp
        src/test_archived_book_spec.cpp
        src/test_archived_copy_spec.cpp
        src/test_archived_loan_spec.cpp
        src/test_action_labels.cpp
        src/test_catalog_lookups.cpp
        src/test_member_lookups.cpp
        src/test_circulation_lookups.cpp
    DEPENDS
        vlms_testsupport
        vlms_ui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Test
    DISCOVER ON
    NO_GTEST_MAIN
    LABELS ui
)
target_include_directories(test_vlms_presentation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(test_vlms_presentation PROPERTIES
    AUTOMOC ON
    AUTORCC ON
)
target_sources(test_vlms_presentation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../resources/images.qrc)

# Catalog Show-All timing against the bundled production DB. Labelled `bench`
# (not `ui`) so CI can exclude or skip it under sanitizers without slowing the
# main widget suite. Runtime also honours VLMS_SKIP_BENCH.
build_gtest_executable(
    NAME test_vlms_bench
    SRC src/test_main.cpp src/test_vlms_bench.cpp
    DEPENDS
        vlms_testsupport
        vlms_ui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Test
    DISCOVER ON
    NO_GTEST_MAIN
    LABELS bench
    TIMEOUT 120
)
target_include_directories(test_vlms_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
# Absolute path: discovered-test ENVIRONMENT is truncated (semicolon list bug),
# so the bench cannot rely on VLMS_SCHEMA_PATH at runtime.
target_compile_definitions(test_vlms_bench PRIVATE
    VLMS_BENCH_DB_PATH="${CMAKE_SOURCE_DIR}/database/vlms.db"
)
set_target_properties(test_vlms_bench PROPERTIES
    AUTOMOC ON
    AUTORCC ON
)
target_sources(test_vlms_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../resources/images.qrc)

# Layer rules from the frontend redesign spec, checked on the sources.
find_program(VLMS_BASH bash)
if(VLMS_BASH)
    add_test(NAME ui_layer_deps
             COMMAND ${VLMS_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/check_ui_layer_deps.sh)
    set_tests_properties(ui_layer_deps PROPERTIES LABELS ui)
endif()
