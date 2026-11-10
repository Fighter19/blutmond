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
set(BM_PACK_SOURCES src/pack/pack.c)

if(BM_PLATFORM STREQUAL "gba")
  # The software PPU is not needed on the device; only its register definitions are used.
  set(BM_HAL_SOURCES src/hal/gba/hal_gba.c)
  set(BM_HAL_DIR src/hal/gba)
else()
  set(BM_HAL_SOURCES src/hal/host/hal_host.c src/ppu/ppu.c)
  set(BM_HAL_DIR src/hal/host)
endif()

add_library(blutmond STATIC ${BM_CORE_SOURCES} ${BM_GRAPHICS_SOURCES} ${BM_PACK_SOURCES} ${BM_HAL_SOURCES})
target_include_directories(blutmond
  PUBLIC src/core/public src/graphics/public src/hal/public src/ppu src/pack ${BM_HAL_DIR}
  PRIVATE src/core/private)
target_compile_options(blutmond PRIVATE ${BM_WARNINGS})
if(BM_PLATFORM STREQUAL "gba")
  target_compile_definitions(blutmond PUBLIC BM_PLATFORM_GBA)
else()
  target_compile_definitions(blutmond PUBLIC BM_PLATFORM_HOST)
endif()

# --- GBA images ---------------------------------------------------------------------------

# bm_add_gba_image(<name> <sources...>): builds <name>.gba (cartridge) and <name>_mb.gba
# (multiboot) from the same sources. Returns the two target names in BM_GBA_TARGETS.
function(bm_add_gba_image name)
  set(targets)
  foreach(kind cart mb)
    if(kind STREQUAL "cart")
      set(target ${name})
    else()
      set(target ${name}_mb)
    endif()
    list(APPEND targets ${target})
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
  set(BM_GBA_TARGETS ${targets} PARENT_SCOPE)
endfunction()

# --- Applications -------------------------------------------------------------------------

set(BM_SCENE_SOURCES apps/scene_test/main.c apps/scene_test/scene.c)

if(BM_PLATFORM STREQUAL "gba")
  bm_add_gba_image(backdrop apps/backdrop/main.c)
  bm_add_gba_image(scene_test ${BM_SCENE_SOURCES})
  # The same scene without the line table, for comparing PPU snapshots taken in mGBA.
  bm_add_gba_image(scene_static ${BM_SCENE_SOURCES})
  foreach(target ${BM_GBA_TARGETS})
    target_compile_definitions(${target} PRIVATE BM_SCENE_LINE_TABLE=0)
  endforeach()
else()
  foreach(app backdrop scene_test)
    if(app STREQUAL "backdrop")
      add_executable(${app} apps/backdrop/main.c)
    else()
      add_executable(${app} ${BM_SCENE_SOURCES})
    endif()
    target_link_libraries(${app} PRIVATE blutmond)
    target_compile_options(${app} PRIVATE ${BM_WARNINGS})
  endforeach()
endif()

# --- Host tools and tests -----------------------------------------------------------------

if(BM_PLATFORM STREQUAL "host")
  add_executable(bm_ppu_render tools/bm_ppu_render.c src/ppu/ppu.c)
  target_include_directories(bm_ppu_render PRIVATE src/ppu)
  target_compile_options(bm_ppu_render PRIVATE ${BM_WARNINGS})

  enable_testing()
  foreach(test test_pool test_heaps test_backdrop test_ppu test_pack)
    add_executable(${test} tests/${test}.c)
    target_link_libraries(${test} PRIVATE blutmond)
    target_include_directories(${test} PRIVATE src/core/private)
    target_compile_options(${test} PRIVATE ${BM_WARNINGS})
    add_test(NAME ${test} COMMAND ${test})
  endforeach()
  add_test(NAME no_hardware_addresses
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_hw_addresses.py ${PROJECT_SOURCE_DIR})

  # mGBA runner (optional). With libmgba installed (Ubuntu: libmgba-dev) the host build gets
  # bm_mgba_run, which runs GBA images headless. Point BM_GBA_BUILD_DIR at a GBA build
  # directory to test its images:  cmake -S . -B build -DBM_GBA_BUILD_DIR=$PWD/build-gba
  find_path(MGBA_INCLUDE_DIR mgba/core/core.h)
  find_library(MGBA_LIBRARY mgba)
  if(MGBA_INCLUDE_DIR AND MGBA_LIBRARY)
    add_executable(bm_mgba_run tools/bm_mgba_run.c src/ppu/ppu.c)
    target_include_directories(bm_mgba_run PRIVATE ${MGBA_INCLUDE_DIR} src/ppu)
    target_link_libraries(bm_mgba_run PRIVATE ${MGBA_LIBRARY})
    # The libmgba headers need POSIX definitions such as PATH_MAX.
    target_compile_definitions(bm_mgba_run PRIVATE _GNU_SOURCE)

    set(BM_GBA_BUILD_DIR "" CACHE PATH "GBA build directory whose images the tests run in mGBA")
    if(BM_GBA_BUILD_DIR)
      set(out ${CMAKE_CURRENT_BINARY_DIR}/test-output)
      file(MAKE_DIRECTORY ${out})
      set(compare ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/compare_images.py)

      foreach(image backdrop backdrop_mb)
        add_test(NAME mgba_${image}
          COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/${image}.gba --frames 90 --expect-backdrop)
      endforeach()

      # Golden image: the test scene rendered by the software PPU on the host must match
      # the GBA image of the same scene running in mGBA, pixel for pixel.
      add_test(NAME scene_render_host COMMAND scene_test ${out}/scene_host.ppm)
      set_tests_properties(scene_render_host PROPERTIES FIXTURES_SETUP scene_host)
      foreach(image scene_test scene_test_mb)
        add_test(NAME scene_render_${image}
          COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/${image}.gba --frames 60 --ppm ${out}/${image}.ppm)
        set_tests_properties(scene_render_${image} PROPERTIES FIXTURES_SETUP scene_${image})
        add_test(NAME scene_matches_${image}
          COMMAND ${compare} ${out}/scene_host.ppm ${out}/${image}.ppm --diff ${out}/${image}_diff.ppm)
        set_tests_properties(scene_matches_${image} PROPERTIES FIXTURES_REQUIRED "scene_host;scene_${image}")
      endforeach()

      # PPU snapshot taken in mGBA, rendered by the software PPU, must match mGBA's picture.
      add_test(NAME snapshot_from_mgba
        COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/scene_static.gba --frames 60
          --ppm ${out}/static_mgba.ppm --snapshot ${out}/static.bmps)
      set_tests_properties(snapshot_from_mgba PROPERTIES FIXTURES_SETUP snapshot)
      add_test(NAME snapshot_render COMMAND bm_ppu_render ${out}/static.bmps ${out}/static_ppu.ppm)
      set_tests_properties(snapshot_render PROPERTIES FIXTURES_REQUIRED snapshot FIXTURES_SETUP snapshot_ppu)
      add_test(NAME snapshot_matches_mgba
        COMMAND ${compare} ${out}/static_ppu.ppm ${out}/static_mgba.ppm --diff ${out}/static_diff.ppm)
      set_tests_properties(snapshot_matches_mgba PROPERTIES FIXTURES_REQUIRED "snapshot;snapshot_ppu")
    endif()
  else()
    message(STATUS "libmgba not found: bm_mgba_run and the mGBA tests are skipped")
  endif()
endif()
