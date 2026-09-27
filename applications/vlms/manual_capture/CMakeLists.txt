find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Widgets)

qt_add_executable(manual_capture
    main.cpp
    Capture.cpp
    Capture.h
    Shots.h
    shots_general.cpp
    shots_screens.cpp
    shots_tasks.cpp
    ../resources/fonts.qrc
    ../resources/images.qrc
)
set_target_properties(manual_capture PROPERTIES AUTOMOC ON AUTORCC ON)
target_include_directories(manual_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_definitions(manual_capture PRIVATE VLMS_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(manual_capture PRIVATE
    vlms_ui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Test
)
vlms_configure_target(manual_capture)
