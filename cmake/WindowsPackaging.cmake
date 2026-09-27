# Windows single-folder installer staging (Inno Setup via scripts/build_windows_installer.ps1).
#
# Installed layout (everything beside vlms.exe):
#   vlms.exe
#   Qt6*.dll, platforms/, styles/, libsqlite3-0.dll, ...
#   tesseract*.dll, leptonica*.dll, tessdata/
#   schema.sql
#   manual/                 (user manual, opened by the ? beside the theme toggle)
#   database/vlms.db   (bundled when present; otherwise created on first run)
#   resources/books/        (bundled when present)
#   resources/members/      (bundled when present)

set(VLMS_INSTALLER_STAGE_DIR "${CMAKE_BINARY_DIR}/installer/stage" CACHE PATH
    "Directory populated by 'cmake --install' before windeployqt / Inno Setup")

if(NOT TARGET vlms)
    message(FATAL_ERROR "WindowsPackaging.cmake requires target vlms")
endif()

# Gitignored runtime data (database + cover images) — checked at install time.
install(SCRIPT "${CMAKE_SOURCE_DIR}/cmake/InstallRuntimeData.cmake")

# Qt deployment script (windeployqt) — run during install to the staging prefix.
if(COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(
        TARGET vlms
        OUTPUT_SCRIPT _vlms_qt_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        NO_TRANSLATIONS
    )
    install(SCRIPT "${_vlms_qt_deploy_script}")
endif()

# Convenience target: stage files for manual inspection (does not build the .exe installer).
add_custom_target(vlms_stage
    COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
            --prefix "${VLMS_INSTALLER_STAGE_DIR}"
            --config "$<CONFIG>"
    COMMENT "Staging VLMS into ${VLMS_INSTALLER_STAGE_DIR}"
    VERBATIM
)
