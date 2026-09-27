# Run as: cmake -DGIT_VERSION_MODULE=<cmake/GitVersion.cmake> -DWORK_DIR=<dir> -P GitVersionTest.cmake
include("${GIT_VERSION_MODULE}")
find_program(GIT git REQUIRED)

function(run_git dir)
    execute_process(COMMAND "${GIT}" -C "${dir}" ${ARGN}
                    RESULT_VARIABLE result OUTPUT_QUIET ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "git ${ARGN}: ${error}")
    endif()
endfunction()

function(expect_version dir numeric_expected full_regex)
    vlms_git_version("${dir}" numeric full)
    if(NOT numeric STREQUAL numeric_expected)
        message(FATAL_ERROR "${dir}: numeric '${numeric}', expected '${numeric_expected}'")
    endif()
    if(NOT full MATCHES "${full_regex}")
        message(FATAL_ERROR "${dir}: full '${full}' does not match '${full_regex}'")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")

# No git history at all. WORK_DIR sits in the build tree, which is itself inside
# this repository, so this also proves a parent repository is not picked up.
file(MAKE_DIRECTORY "${WORK_DIR}/plain")
expect_version("${WORK_DIR}/plain" "0.0.0" "^0\\.0\\.0-dev$")

# A tag on HEAD.
set(repo "${WORK_DIR}/tagged")
file(MAKE_DIRECTORY "${repo}")
run_git("${repo}" init -q)
run_git("${repo}" -c user.name=T -c user.email=t@example.org commit -q --allow-empty -m one)
run_git("${repo}" tag v0.3.1)
expect_version("${repo}" "0.3.1" "^0\\.3\\.1$")

# Commits past the tag.
run_git("${repo}" -c user.name=T -c user.email=t@example.org commit -q --allow-empty -m two)
run_git("${repo}" -c user.name=T -c user.email=t@example.org commit -q --allow-empty -m three)
expect_version("${repo}" "0.3.1" "^0\\.3\\.1\\+2\\.g[0-9a-f]+$")

# A repository without a version tag.
set(repo "${WORK_DIR}/untagged")
file(MAKE_DIRECTORY "${repo}")
run_git("${repo}" init -q)
run_git("${repo}" -c user.name=T -c user.email=t@example.org commit -q --allow-empty -m one)
expect_version("${repo}" "0.0.0" "^0\\.0\\.0-dev$")

message(STATUS "GitVersion: all cases passed")
