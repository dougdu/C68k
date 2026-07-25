# c68k --- CMake host build of the cross-compiler (P0 baseline).
#
# Builds the `c68k` binary from the imported chibicc sources with a host
# GCC/Clang toolchain. The Makefile remains the primary driver for the test
# and self-host targets; this file exists so c68k can be built as part of a
# larger CMake super-build. See the Makefile and docs/implementation-plan.md.
#
# Note: chibicc-derived code is POSIX/GCC-flavored C and does not build with
# MSVC; use GCC or Clang on Linux/macOS.

cmake_minimum_required(VERSION 3.13)
project(c68k LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB C68K_SRCS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)

add_executable(c68k ${C68K_SRCS})

if(MSVC)
  # chibicc calls many POSIX-named CRT functions; silence the deprecation noise.
  target_compile_definitions(c68k PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
else()
  target_compile_options(c68k PRIVATE -fno-common -Wall -Wno-switch)
endif()

# --- Build-unique version stamp ----------------------------------------------
# Embed the git short hash (plus "-dirty" when the working tree has uncommitted
# changes) and the configure-time date, so `c68k --version` and the
# __c68k_gitrev__ predefined macro identify the exact build. Re-running configure
# refreshes them; listing .git/HEAD and .git/index as configure dependencies
# makes a commit or a staged change trigger that reconfigure automatically.
set(C68K_GITREV "unknown")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --always --dirty --abbrev=8
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE C68K_GITREV
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  foreach(gitfile HEAD index)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/${gitfile}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/.git/${gitfile}")
    endif()
  endforeach()
endif()
string(TIMESTAMP C68K_BUILD_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)
target_compile_definitions(c68k PRIVATE
  C68K_GITREV="${C68K_GITREV}"
  C68K_BUILD_DATE="${C68K_BUILD_DATE}")
message(STATUS "c68k build stamp: ${C68K_GITREV} (${C68K_BUILD_DATE})")

# chibicc locates its freestanding builtin headers at <dir-of-argv0>/include,
# so stage an include/ tree next to the built binary (keeps out-of-source
# builds working).
add_custom_command(TARGET c68k POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/include
          $<TARGET_FILE_DIR:c68k>/include
  COMMENT "Staging builtin headers next to c68k")
