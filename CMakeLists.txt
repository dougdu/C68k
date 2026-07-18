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

# chibicc locates its freestanding builtin headers at <dir-of-argv0>/include,
# so stage an include/ tree next to the built binary (keeps out-of-source
# builds working).
add_custom_command(TARGET c68k POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/include
          $<TARGET_FILE_DIR:c68k>/include
  COMMENT "Staging builtin headers next to c68k")
