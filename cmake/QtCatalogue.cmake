# Qt's own message catalogue, embedded in the executable.
#
# Some of the text on screen is not this application's to write: the file
# picker's toolbar and column headers, and anything else a stock Qt widget
# says for itself, come out of Qt's qtbase_<lang>.qm. The Windows package is
# deployed with NO_TRANSLATIONS -- deliberately, it is one folder beside the
# .exe and nobody wants a translations/ directory in it -- so there is no
# catalogue on disk to find at run time.
#
# Embedding the two languages that are not English costs ~320 KB and makes the
# question of what is installed on the machine moot. English needs no
# catalogue: it is what the sources are written in.
#
# Defines VLMS_QT_CATALOGUE_QRC (a generated .qrc to add to a target's
# sources) when at least one catalogue was found, and leaves it unset
# otherwise -- a Qt built without translations must still build the app, with
# Qt's own strings falling back to English.

function(vlms_generate_qt_catalogue out_variable)
    set(_languages ar fr)

    set(_search_dirs "")
    if(DEFINED QT6_INSTALL_PREFIX AND DEFINED QT6_INSTALL_TRANSLATIONS)
        list(APPEND _search_dirs "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
    endif()
    # A Qt whose CMake package does not export the paths still has qmake, and
    # qmake always knows where its own translations went.
    foreach(_tool qmake qtpaths)
        if(TARGET Qt${QT_VERSION_MAJOR}::${_tool})
            get_target_property(_tool_path Qt${QT_VERSION_MAJOR}::${_tool} IMPORTED_LOCATION)
            if(_tool_path)
                execute_process(
                    COMMAND "${_tool_path}" -query QT_INSTALL_TRANSLATIONS
                    OUTPUT_VARIABLE _queried
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
                if(_queried)
                    list(APPEND _search_dirs "${_queried}")
                endif()
            endif()
        endif()
    endforeach()

    set(_stage_dir "${CMAKE_CURRENT_BINARY_DIR}/qt_catalogue")
    file(MAKE_DIRECTORY "${_stage_dir}")

    set(_entries "")
    foreach(_lang IN LISTS _languages)
        foreach(_dir IN LISTS _search_dirs)
            set(_candidate "${_dir}/qtbase_${_lang}.qm")
            if(EXISTS "${_candidate}")
                # configure_file, not file(COPY): it registers a dependency, so
                # a Qt update re-runs CMake instead of leaving a stale catalogue
                # baked into the resource.
                configure_file("${_candidate}" "${_stage_dir}/qtbase_${_lang}.qm" COPYONLY)
                string(APPEND _entries
                       "    <file>qtbase_${_lang}.qm</file>\n")
                break()
            endif()
        endforeach()
    endforeach()

    if(_entries STREQUAL "")
        message(STATUS "Qt catalogue: no qtbase_*.qm found; Qt's own strings stay English")
        return()
    endif()

    set(_qrc "${_stage_dir}/qt_catalogue.qrc")
    file(WRITE "${_qrc}"
         "<!DOCTYPE RCC><RCC version=\"1.0\">\n"
         "  <qresource prefix=\"/translations\">\n"
         "${_entries}"
         "  </qresource>\n"
         "</RCC>\n")

    message(STATUS "Qt catalogue: embedding ${_qrc}")
    set(${out_variable} "${_qrc}" PARENT_SCOPE)
endfunction()
