cmake_minimum_required(VERSION 3.16)
project(blk_tests CXX)

# Build & run:
#   cmake -S tests -B tests/build [-DLITEMATH_DIR=/path/to/LiteMath]
#   cmake --build tests/build && ctest --test-dir tests/build --output-on-failure

set(LITEMATH_DIR "/home/sammael/labProjects/LiteMath" CACHE PATH "Directory that contains LiteMath.h")
option(BLK_SANITIZE "Build with AddressSanitizer, LeakSanitizer and UBSan" ON)
option(BLK_FUZZ "Build the libFuzzer target blk_fuzz (clang only)" OFF)

if(NOT EXISTS "${LITEMATH_DIR}/LiteMath.h")
  message(FATAL_ERROR "LiteMath.h not found in LITEMATH_DIR='${LITEMATH_DIR}'. Pass -DLITEMATH_DIR=<path>.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BLK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# blk.h includes "LiteMath/LiteMath.h"; the shim makes that work whatever the LiteMath folder is called.
set(SHIM_DIR "${CMAKE_CURRENT_BINARY_DIR}/shim")
file(WRITE "${SHIM_DIR}/LiteMath/LiteMath.h" "#pragma once\n#include \"${LITEMATH_DIR}/LiteMath.h\"\n")

if(BLK_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(blk_under_test STATIC "${BLK_ROOT}/blk.cpp")
target_include_directories(blk_under_test PUBLIC "${BLK_ROOT}" "${SHIM_DIR}")

add_executable(blk_tests run_tests.cpp)
target_link_libraries(blk_tests PRIVATE blk_under_test)
target_compile_definitions(blk_tests PRIVATE BLK_CASES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cases")

# One ctest entry per test, so a crash fails only that test.
enable_testing()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS run_tests.cpp)
file(STRINGS run_tests.cpp test_lines REGEX "^TEST\\([a-z0-9_]+\\)")
foreach(line IN LISTS test_lines)
  string(REGEX REPLACE "^TEST\\(([a-z0-9_]+)\\).*" "\\1" name "${line}")
  add_test(NAME api.${name} COMMAND blk_tests ${name} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endforeach()

file(GLOB case_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cases/*.blk")
list(FILTER case_files EXCLUDE REGEX "\\.out\\.blk$")
foreach(f IN LISTS case_files)
  get_filename_component(name "${f}" NAME_WE)
  add_test(NAME case.${name} COMMAND blk_tests --case "${f}")
endforeach()

if(BLK_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "BLK_FUZZ requires clang: -DCMAKE_CXX_COMPILER=clang++")
  endif()
  add_executable(blk_fuzz fuzz_blk.cpp "${BLK_ROOT}/blk.cpp")
  target_include_directories(blk_fuzz PRIVATE "${BLK_ROOT}" "${SHIM_DIR}")
  target_compile_options(blk_fuzz PRIVATE -fsanitize=fuzzer,address)
  target_link_options(blk_fuzz PRIVATE -fsanitize=fuzzer,address)
endif()
