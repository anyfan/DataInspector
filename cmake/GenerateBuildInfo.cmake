set(DI_GIT_HASH "不可用")
set(DI_GIT_BRANCH "不可用")
set(DI_GIT_STATE "未知（无 Git 元数据）")
if(DI_GIT_EXECUTABLE AND EXISTS "${DI_SOURCE_DIR}/.git")
    execute_process(COMMAND "${DI_GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${DI_SOURCE_DIR}" OUTPUT_VARIABLE git_hash
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE git_result)
    if(git_result EQUAL 0)
        set(DI_GIT_HASH "${git_hash}")
        execute_process(COMMAND "${DI_GIT_EXECUTABLE}" symbolic-ref --short -q HEAD
            WORKING_DIRECTORY "${DI_SOURCE_DIR}" OUTPUT_VARIABLE DI_GIT_BRANCH
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT DI_GIT_BRANCH)
            set(DI_GIT_BRANCH "detached HEAD")
        endif()
        execute_process(COMMAND "${DI_GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
            WORKING_DIRECTORY "${DI_SOURCE_DIR}" OUTPUT_VARIABLE git_status
            ERROR_QUIET RESULT_VARIABLE status_result)
        if(NOT status_result EQUAL 0)
            set(DI_GIT_STATE "未知（无法读取工作区）")
        elseif(git_status)
            set(DI_GIT_STATE "有未提交修改")
        else()
            set(DI_GIT_STATE "干净")
        endif()
    endif()
endif()
# Refresh time only when inputs change. An always-run lightweight Git check
# must not rewrite the header and relink the application on an unchanged build.
file(GLOB_RECURSE source_inputs LIST_DIRECTORIES false
    "${DI_SOURCE_DIR}/src/*.cpp" "${DI_SOURCE_DIR}/src/*.h"
    "${DI_SOURCE_DIR}/qml/*.qml" "${DI_SOURCE_DIR}/cmake/*.cmake"
    "${DI_SOURCE_DIR}/cmake/*.in" "${DI_SOURCE_DIR}/assets/*")
list(APPEND source_inputs "${DI_SOURCE_DIR}/CMakeLists.txt"
    "${DI_SOURCE_DIR}/CMakePresets.json" "${DI_SOURCE_DIR}/docs/user-guide/changelog.md")
list(SORT source_inputs)
set(signature "${DI_VERSION}|${DI_BUILD_TYPE}|${DI_BUILD_OPTIONS}|${DI_GIT_HASH}|${DI_GIT_BRANCH}|${DI_GIT_STATE}")
foreach(input IN LISTS source_inputs)
    file(SHA256 "${input}" input_hash)
    string(APPEND signature "|${input}|${input_hash}")
endforeach()
string(SHA256 signature "${signature}")
set(signature_file "${DI_OUTPUT_DIR}/buildinfo.signature")
if(EXISTS "${signature_file}" AND EXISTS "${DI_OUTPUT_DIR}/buildinfo.h")
    file(READ "${signature_file}" previous_signature)
    if(signature STREQUAL previous_signature)
        return()
    endif()
endif()
string(TIMESTAMP DI_BUILD_TIME "%Y-%m-%d %H:%M:%S UTC" UTC)
file(READ "${DI_SOURCE_DIR}/docs/user-guide/changelog.md" DI_RELEASE_NOTES)
file(MAKE_DIRECTORY "${DI_OUTPUT_DIR}")
configure_file("${DI_SOURCE_DIR}/cmake/buildinfo.h.in" "${DI_OUTPUT_DIR}/buildinfo.h" @ONLY)
file(WRITE "${signature_file}" "${signature}")
