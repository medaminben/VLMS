# vlms_git_version(<source_dir> <numeric_var> <full_var>)
#
# The version is the latest vX.Y.Z tag; see
# docs/superpowers/specs/2026-09-24-automatic-versioning-design.md.
#   numeric -- X.Y.Z, for project(). 0.0.0 without git or a tag.
#   full    -- X.Y.Z on the tagged commit, X.Y.Z+N.gSHA past it, 0.0.0-dev
#              without git or a tag, so a dev build never passes for a release.
# <source_dir> must be the top of its own repository: a source tree unpacked
# inside some other checkout must not borrow that checkout's tags.
function(vlms_git_version source_dir numeric_var full_var)
    set(numeric "0.0.0")
    set(full "0.0.0-dev")

    find_program(VLMS_GIT_EXECUTABLE git)
    if(VLMS_GIT_EXECUTABLE)
        execute_process(
            COMMAND "${VLMS_GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --show-toplevel
            OUTPUT_VARIABLE toplevel OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE toplevel_result ERROR_QUIET)
        get_filename_component(source_real "${source_dir}" REALPATH)
        get_filename_component(toplevel_real "${toplevel}" REALPATH)
        if(toplevel_result EQUAL 0 AND source_real STREQUAL toplevel_real)
            execute_process(
                COMMAND "${VLMS_GIT_EXECUTABLE}" -C "${source_dir}" describe --tags --abbrev=0
                        --match "v[0-9]*.[0-9]*.[0-9]*"
                OUTPUT_VARIABLE tag OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE describe_result ERROR_QUIET)
            if(describe_result EQUAL 0 AND tag MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
                set(numeric "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
                set(full "${numeric}")
                execute_process(
                    COMMAND "${VLMS_GIT_EXECUTABLE}" -C "${source_dir}" rev-list --count "${tag}..HEAD"
                    OUTPUT_VARIABLE ahead OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
                if(ahead GREATER 0)
                    execute_process(
                        COMMAND "${VLMS_GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --short=7 HEAD
                        OUTPUT_VARIABLE sha OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
                    set(full "${numeric}+${ahead}.g${sha}")
                endif()
            endif()
        endif()
    endif()

    set(${numeric_var} "${numeric}" PARENT_SCOPE)
    set(${full_var} "${full}" PARENT_SCOPE)
endfunction()
