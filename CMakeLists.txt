cmake_minimum_required(VERSION 3.20)
project(blutmond LANGUAGES C ASM)

# Platform: "host" (PC, tests, editor) or "gba" (set by cmake/toolchain-gba.cmake).
set(BM_PLATFORM host CACHE STRING "Target platform: host or gba")
set_property(CACHE BM_PLATFORM PROPERTY STRINGS host gba)

option(USE_SANITIZER "Enable AddressSanitizer and UBSan (host only)" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

if(BM_PLATFORM STREQUAL "host")
  if(USE_SANITIZER)
    add_compile_options(-fsanitize=address -fsanitize=undefined)
    add_link_options(-fsanitize=address -fsanitize=undefined)
  endif()
  if(UNIX)
    add_compile_definitions(UNIX)
  endif()
endif()

set(BM_WARNINGS -Wall -Wextra -Werror -pedantic)

# --- Engine library -----------------------------------------------------------------------

file(GLOB BM_CORE_SOURCES CONFIGURE_DEPENDS src/core/public/*.c)
file(GLOB BM_GRAPHICS_SOURCES CONFIGURE_DEPENDS src/graphics/public/*.c)

if(BM_PLATFORM STREQUAL "gba")
  set(BM_HAL_SOURCES src/hal/gba/hal_gba.c)
  set(BM_HAL_DIR src/hal/gba)
else()
  set(BM_HAL_SOURCES src/hal/host/hal_host.c)
  set(BM_HAL_DIR src/hal/host)
endif()

add_library(blutmond STATIC ${BM_CORE_SOURCES} ${BM_GRAPHICS_SOURCES} ${BM_HAL_SOURCES})
target_include_directories(blutmond
  PUBLIC src/core/public src/graphics/public src/hal/public ${BM_HAL_DIR}
  PRIVATE src/core/private)
target_compile_options(blutmond PRIVATE ${BM_WARNINGS})
if(BM_PLATFORM STREQUAL "gba")
  target_compile_definitions(blutmond PUBLIC BM_PLATFORM_GBA)
else()
  target_compile_definitions(blutmond PUBLIC BM_PLATFORM_HOST)
endif()

# --- GBA images ---------------------------------------------------------------------------

# bm_add_gba_image(<name> <sources...>): builds <name>.gba (cartridge) and <name>_mb.gba
# (multiboot) from the same sources.
function(bm_add_gba_image name)
  foreach(kind cart mb)
    if(kind STREQUAL "cart")
      set(target ${name})
    else()
      set(target ${name}_mb)
    endif()
    add_executable(${target} ${ARGN} ${PROJECT_SOURCE_DIR}/src/hal/gba/crt0.S)
    set_target_properties(${target} PROPERTIES SUFFIX ".elf")
    target_link_libraries(${target} PRIVATE blutmond)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:${BM_WARNINGS}>)
    if(kind STREQUAL "mb")
      target_compile_definitions(${target} PRIVATE BM_GBA_MULTIBOOT)
    endif()
    target_link_options(${target} PRIVATE
      -T ${PROJECT_SOURCE_DIR}/src/hal/gba/gba_${kind}.ld
      -Wl,-Map=$<TARGET_FILE_DIR:${target}>/${target}.map)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
      ${PROJECT_SOURCE_DIR}/src/hal/gba/gba_${kind}.ld)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> $<TARGET_FILE_DIR:${target}>/${target}.gba
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gbafix.py $<TARGET_FILE_DIR:${target}>/${target}.gba --pad
      COMMENT "Writing ${target}.gba")
  endforeach()
endfunction()

# --- Applications -------------------------------------------------------------------------

if(BM_PLATFORM STREQUAL "gba")
  bm_add_gba_image(backdrop apps/backdrop/main.c)
else()
  add_executable(backdrop apps/backdrop/main.c)
  target_link_libraries(backdrop PRIVATE blutmond)
  target_compile_options(backdrop PRIVATE ${BM_WARNINGS})
endif()

# --- Tests (host) -------------------------------------------------------------------------

if(BM_PLATFORM STREQUAL "host")
  enable_testing()
  foreach(test test_pool test_heaps test_backdrop)
    add_executable(${test} tests/${test}.c)
    target_link_libraries(${test} PRIVATE blutmond)
    target_include_directories(${test} PRIVATE src/core/private)
    target_compile_options(${test} PRIVATE ${BM_WARNINGS})
    add_test(NAME ${test} COMMAND ${test})
  endforeach()
  add_test(NAME no_hardware_addresses
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_hw_addresses.py ${PROJECT_SOURCE_DIR})
endif()

# --- mGBA runner (host, optional) ---------------------------------------------------------
#
# With libmgba installed (Ubuntu: libmgba-dev) the host build gets tools/bm_mgba_run, which runs
# GBA images headless. Point BM_GBA_BUILD_DIR at a GBA build directory to test its images:
#   cmake -S . -B build -DBM_GBA_BUILD_DIR=$PWD/build-gba

if(BM_PLATFORM STREQUAL "host")
  find_path(MGBA_INCLUDE_DIR mgba/core/core.h)
  find_library(MGBA_LIBRARY mgba)
  if(MGBA_INCLUDE_DIR AND MGBA_LIBRARY)
    add_executable(bm_mgba_run tools/bm_mgba_run.c)
    target_include_directories(bm_mgba_run PRIVATE ${MGBA_INCLUDE_DIR})
    target_link_libraries(bm_mgba_run PRIVATE ${MGBA_LIBRARY})
    # The libmgba headers need POSIX definitions such as PATH_MAX.
    target_compile_definitions(bm_mgba_run PRIVATE _GNU_SOURCE)
    set(BM_GBA_BUILD_DIR "" CACHE PATH "GBA build directory whose images the tests run in mGBA")
    if(BM_GBA_BUILD_DIR)
      foreach(image backdrop backdrop_mb)
        add_test(NAME mgba_${image}
          COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/${image}.gba --frames 90 --expect-backdrop)
      endforeach()
    endif()
  else()
    message(STATUS "libmgba not found: bm_mgba_run and the mGBA tests are skipped")
  endif()
endif()
