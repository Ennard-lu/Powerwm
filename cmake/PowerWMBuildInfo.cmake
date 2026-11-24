# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

find_package(Git QUIET)

set(POWERWM_GIT_COMMIT "unknown")
set(POWERWM_GIT_COMMIT_SHORT "unknown")
set(POWERWM_GIT_BRANCH "unknown")
set(POWERWM_GIT_DESCRIBE "unknown")
set(POWERWM_GIT_DIRTY FALSE)

function(powerwm_query_git variable)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" ${ARGN}
        OUTPUT_VARIABLE query_result
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE query_status
    )
    if(NOT query_status EQUAL 0 OR query_result STREQUAL "")
        set(query_result "unknown")
    endif()
    set(${variable} "${query_result}" PARENT_SCOPE)
endfunction()

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    powerwm_query_git(POWERWM_GIT_COMMIT rev-parse HEAD)
    powerwm_query_git(POWERWM_GIT_COMMIT_SHORT rev-parse --short=12 HEAD)
    powerwm_query_git(POWERWM_GIT_BRANCH rev-parse --abbrev-ref HEAD)
    powerwm_query_git(POWERWM_GIT_DESCRIBE describe --tags --always --dirty)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_SOURCE_DIR}" status --porcelain
        OUTPUT_VARIABLE POWERWM_GIT_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE POWERWM_GIT_STATUS_CODE
    )
    if(POWERWM_GIT_STATUS_CODE EQUAL 0 AND NOT POWERWM_GIT_STATUS STREQUAL "")
        set(POWERWM_GIT_DIRTY TRUE)
    endif()
endif()

string(TIMESTAMP POWERWM_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)

if(CMAKE_BUILD_TYPE)
    set(POWERWM_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
elseif(CMAKE_CONFIGURATION_TYPES)
    set(POWERWM_BUILD_TYPE "multi-config")
else()
    set(POWERWM_BUILD_TYPE "unknown")
endif()

set(POWERWM_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(TOUPPER "${POWERWM_BUILD_TYPE}" POWERWM_BUILD_TYPE_UPPER)
if(DEFINED CMAKE_CXX_FLAGS_${POWERWM_BUILD_TYPE_UPPER})
    string(APPEND POWERWM_CXX_FLAGS " ${CMAKE_CXX_FLAGS_${POWERWM_BUILD_TYPE_UPPER}}")
endif()
string(STRIP "${POWERWM_CXX_FLAGS}" POWERWM_CXX_FLAGS)
string(REPLACE ";" " " POWERWM_CXX_FLAGS "${POWERWM_CXX_FLAGS}")

set(POWERWM_BUILD_FEATURES "")
if(BUILD_TESTING)
    list(APPEND POWERWM_BUILD_FEATURES "testing")
endif()
if(POWERWM_BUILD_BRINGUP_TOOL)
    list(APPEND POWERWM_BUILD_FEATURES "drm_bringup")
endif()
list(JOIN POWERWM_BUILD_FEATURES ", " POWERWM_BUILD_FEATURES)

set(POWERWM_GENERATOR "${CMAKE_GENERATOR}")
if(CMAKE_GENERATOR_PLATFORM)
    string(APPEND POWERWM_GENERATOR " (${CMAKE_GENERATOR_PLATFORM})")
endif()

if(NOT CMAKE_CXX_STANDARD)
    set(POWERWM_CXX_STANDARD "unknown")
else()
    set(POWERWM_CXX_STANDARD "${CMAKE_CXX_STANDARD}")
endif()

set(POWERWM_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}")
set(POWERWM_SYSTEM_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
set(POWERWM_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}")
set(POWERWM_COMPILER_VERSION "${CMAKE_CXX_COMPILER_VERSION}")
set(POWERWM_COMPILER_PATH "${CMAKE_CXX_COMPILER}")
set(POWERWM_BUILD_DIRECTORY "${CMAKE_BINARY_DIR}")
if(POWERWM_GIT_DIRTY)
    set(POWERWM_GIT_DIRTY_BOOL "true")
else()
    set(POWERWM_GIT_DIRTY_BOOL "false")
endif()

function(powerwm_escape_cpp_string variable value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\r" "\\r" value "${value}")
    set(${variable} "${value}" PARENT_SCOPE)
endfunction()

powerwm_escape_cpp_string(POWERWM_GIT_COMMIT "${POWERWM_GIT_COMMIT}")
powerwm_escape_cpp_string(POWERWM_GIT_COMMIT_SHORT "${POWERWM_GIT_COMMIT_SHORT}")
powerwm_escape_cpp_string(POWERWM_GIT_BRANCH "${POWERWM_GIT_BRANCH}")
powerwm_escape_cpp_string(POWERWM_GIT_DESCRIBE "${POWERWM_GIT_DESCRIBE}")
powerwm_escape_cpp_string(POWERWM_BUILD_TYPE "${POWERWM_BUILD_TYPE}")
powerwm_escape_cpp_string(POWERWM_COMPILER_ID "${POWERWM_COMPILER_ID}")
powerwm_escape_cpp_string(POWERWM_COMPILER_VERSION "${POWERWM_COMPILER_VERSION}")
powerwm_escape_cpp_string(POWERWM_COMPILER_PATH "${POWERWM_COMPILER_PATH}")
powerwm_escape_cpp_string(POWERWM_GENERATOR "${POWERWM_GENERATOR}")
powerwm_escape_cpp_string(POWERWM_SYSTEM_NAME "${POWERWM_SYSTEM_NAME}")
powerwm_escape_cpp_string(POWERWM_SYSTEM_PROCESSOR "${POWERWM_SYSTEM_PROCESSOR}")
powerwm_escape_cpp_string(POWERWM_CXX_STANDARD "${POWERWM_CXX_STANDARD}")
powerwm_escape_cpp_string(POWERWM_CXX_FLAGS "${POWERWM_CXX_FLAGS}")
powerwm_escape_cpp_string(POWERWM_BUILD_DIRECTORY "${POWERWM_BUILD_DIRECTORY}")
powerwm_escape_cpp_string(POWERWM_BUILD_FEATURES "${POWERWM_BUILD_FEATURES}")

set(POWERWM_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${POWERWM_GENERATED_DIR}/powerwm")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/BuildInfo.hpp.in"
    "${POWERWM_GENERATED_DIR}/powerwm/build_info.hpp"
    @ONLY
)

if(EXISTS "${CMAKE_SOURCE_DIR}/.git/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/.git/HEAD"
    )
endif()
if(POWERWM_GIT_BRANCH STREQUAL "HEAD")
    set(POWERWM_GIT_REF "detached")
else()
    set(POWERWM_GIT_REF "refs/heads/${POWERWM_GIT_BRANCH}")
endif()
if(EXISTS "${CMAKE_SOURCE_DIR}/.git/${POWERWM_GIT_REF}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/.git/${POWERWM_GIT_REF}"
    )
endif()