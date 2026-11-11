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
file(GLOB BM_WORLD_SOURCES CONFIGURE_DEPENDS src/world/*.c)
set(BM_LINK_SOURCES src/link/link.c src/link/live.c src/ppu/ppu_snapshot_part.c)

if(BM_PLATFORM STREQUAL "gba")
  # The software PPU is not needed on the device; only its register definitions are used.
  set(BM_HAL_SOURCES src/hal/gba/hal_gba.c src/hal/gba/irq.S)
  set(BM_HAL_DIR src/hal/gba)
else()
  set(BM_HAL_SOURCES src/hal/host/hal_host.c src/ppu/ppu.c)
  set(BM_HAL_DIR src/hal/host)
endif()

add_library(blutmond STATIC ${BM_CORE_SOURCES} ${BM_GRAPHICS_SOURCES} ${BM_PACK_SOURCES} ${BM_WORLD_SOURCES} ${BM_LINK_SOURCES} ${BM_HAL_SOURCES})
target_include_directories(blutmond
  PUBLIC src/core/public src/graphics/public src/hal/public src/ppu src/pack src/world src/link ${BM_HAL_DIR}
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

  # Host tools (the cooker) for building packs during the GBA build. Either point
  # BM_HOST_TOOLS_DIR at a host build directory, or they are built here automatically.
  set(BM_HOST_TOOLS_DIR "" CACHE PATH "Host build directory that contains bmcook")
  if(BM_HOST_TOOLS_DIR)
    set(BM_BMCOOK ${BM_HOST_TOOLS_DIR}/bmcook)
    add_custom_target(host_tools)
  else()
    include(ExternalProject)
    set(BM_BMCOOK ${CMAKE_BINARY_DIR}/host-tools/bmcook)
    ExternalProject_Add(host_tools
      SOURCE_DIR ${PROJECT_SOURCE_DIR}
      BINARY_DIR ${CMAKE_BINARY_DIR}/host-tools
      CMAKE_ARGS -DBM_PLATFORM=host -DCMAKE_BUILD_TYPE=Release -DBM_BUILD_EDITOR=OFF
      BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target bmcook
      INSTALL_COMMAND ""
      BUILD_ALWAYS ON
      BUILD_BYPRODUCTS ${BM_BMCOOK})
  endif()

  set(BM_TEST_ROOM_PACK ${CMAKE_BINARY_DIR}/test_room_gba.bmpk)
  add_custom_command(OUTPUT ${BM_TEST_ROOM_PACK}
    COMMAND ${BM_BMCOOK} --profile gba -o ${BM_TEST_ROOM_PACK} ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx
    DEPENDS host_tools ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/tileset.png
    COMMENT "Cooking the test room")
  add_custom_target(test_room_pack DEPENDS ${BM_TEST_ROOM_PACK})

  set(BM_TEST_LEVEL_PACK ${CMAKE_BINARY_DIR}/test_level_gba.bmpk)
  add_custom_command(OUTPUT ${BM_TEST_LEVEL_PACK}
    COMMAND ${BM_BMCOOK} --profile gba -o ${BM_TEST_LEVEL_PACK}
      ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/room2.tmx
    DEPENDS host_tools ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/room2.tmx
      ${PROJECT_SOURCE_DIR}/assets/test_room/tileset.png
    COMMENT "Cooking the test level")
  add_custom_target(test_level_pack DEPENDS ${BM_TEST_LEVEL_PACK})

  # The game, played with the buttons, and the same with the autopilot (for tests and demos).
  # embed_pack.S is compiled per target with that target's BM_PACK_PATH.
  foreach(variant game game_autoplay)
    bm_add_gba_image(${variant} apps/game/main.c apps/game/autopilot.c apps/game/game_live.c ${PROJECT_SOURCE_DIR}/src/hal/gba/embed_pack.S)
    foreach(target ${BM_GBA_TARGETS})
      add_dependencies(${target} test_level_pack)
      target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/apps/game)
      target_compile_definitions(${target} PRIVATE "BM_PACK_PATH=\"${BM_TEST_LEVEL_PACK}\"")
      if(variant STREQUAL "game_autoplay")
        # The autoplay image also writes a snapshot at game frame 300 (for the state tests).
        target_compile_definitions(${target} PRIVATE BM_AUTOPLAY=1 BM_SNAPSHOT_FRAME=300)
      endif()
    endforeach()
  endforeach()
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/src/hal/gba/embed_pack.S APPEND PROPERTY OBJECT_DEPENDS ${BM_TEST_LEVEL_PACK})

  bm_add_gba_image(room_viewer apps/room_viewer/main.c ${PROJECT_SOURCE_DIR}/src/hal/gba/embed_pack.S)
  set_source_files_properties(${PROJECT_SOURCE_DIR}/src/hal/gba/embed_pack.S PROPERTIES OBJECT_DEPENDS ${BM_TEST_ROOM_PACK})
  foreach(target ${BM_GBA_TARGETS})
    add_dependencies(${target} test_room_pack)
    target_compile_definitions(${target} PRIVATE "BM_PACK_PATH=\"${BM_TEST_ROOM_PACK}\"")
  endforeach()
else()
  foreach(app backdrop scene_test room_viewer game)
    if(app STREQUAL "backdrop")
      add_executable(${app} apps/backdrop/main.c)
    elseif(app STREQUAL "room_viewer")
      add_executable(${app} apps/room_viewer/main.c)
    elseif(app STREQUAL "game")
      add_executable(${app} apps/game/main.c apps/game/autopilot.c apps/game/game_live.c apps/game/host_link.c)
      find_package(Threads REQUIRED)
      target_link_libraries(${app} PRIVATE Threads::Threads)
    else()
      add_executable(${app} ${BM_SCENE_SOURCES})
    endif()
    target_link_libraries(${app} PRIVATE blutmond)
    target_compile_options(${app} PRIVATE ${BM_WARNINGS})
  endforeach()
endif()

# --- Host tools and tests -----------------------------------------------------------------

if(BM_PLATFORM STREQUAL "host")
  enable_language(CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)

  # Cooker: Tiled rooms -> pack (tools/cooker), as a library for bmcook and the editor.
  # Uses stb_image from third_party.
  add_library(bm_cooker STATIC
    tools/cooker/cook.cpp tools/cooker/tmx.cpp tools/cooker/xml.cpp tools/cooker/image.cpp tools/cooker/overrides.cpp src/pack/pack.c)
  target_include_directories(bm_cooker PUBLIC tools/cooker src/pack PRIVATE third_party/stb)
  target_compile_definitions(bm_cooker PUBLIC BM_PLATFORM_HOST)
  target_compile_options(bm_cooker PRIVATE ${BM_WARNINGS})
  # The editor's project model (project file, asset catalogue, new rooms), without UI.
  add_library(bm_editor_model STATIC editor/project_file.cpp editor/vram.cpp)
  target_include_directories(bm_editor_model PUBLIC editor)
  target_link_libraries(bm_editor_model PUBLIC bm_cooker blutmond)
  target_compile_options(bm_editor_model PRIVATE ${BM_WARNINGS})
  add_executable(bmcook tools/cooker/main.cpp)
  target_link_libraries(bmcook PRIVATE bm_cooker)
  target_compile_options(bmcook PRIVATE ${BM_WARNINGS})

  # Editor (on ose-ui with SDL3 and GLES 2). ose-ui is fetched at a pinned commit; point
  # FETCHCONTENT_SOURCE_DIR_OSE_UI at a local checkout to work on both.
  option(BM_BUILD_EDITOR "Build the editor (downloads ose-ui and, if not installed, SDL3)" ON)
  if(BM_BUILD_EDITOR)
    include(FetchContent)
    FetchContent_Declare(ose_ui
      GIT_REPOSITORY https://github.com/Fighter19/ose-ui.git
      GIT_TAG 81846abb402362fd542952a58e648844a6551520)
    FetchContent_MakeAvailable(ose_ui)
    add_executable(bmeditor editor/main.cpp editor/project.cpp editor/views.cpp editor/runner.cpp editor/graphics_view.cpp
      editor/dialogs.cpp editor/project_panel.cpp editor/palette_panel.cpp)
    set_target_properties(bmeditor PROPERTIES CXX_STANDARD 20)
    target_link_libraries(bmeditor PRIVATE bm_editor_model bm_cooker blutmond ose_ui_sdl3)
    target_compile_definitions(bmeditor PRIVATE
      "BM_EDITOR_DEFAULT_PROJECT=\"${PROJECT_SOURCE_DIR}/assets/test.bmproj\""
      "BM_EDITOR_ENGINE_DIR=\"${PROJECT_SOURCE_DIR}/assets/engine\""
      "BM_EDITOR_GAME=\"$<TARGET_FILE:game>\""
      "BM_EDITOR_BMLINK=\"$<TARGET_FILE:bmlink>\"")
    # Run on the GBA multiboots the runtime from a GBA build (or set gba-image in editor.conf).
    set(BM_GBA_BUILD_DIR "" CACHE PATH "GBA build directory (runtime images for the editor and the mGBA tests)")
    if(BM_GBA_BUILD_DIR)
      target_compile_definitions(bmeditor PRIVATE "BM_EDITOR_GBA_IMAGE=\"${BM_GBA_BUILD_DIR}/game_mb.gba\"")
    endif()
    # Run needs the game and bmlink next to the editor.
    add_dependencies(bmeditor game bmlink)
    if(NOT MSVC)
      target_compile_options(bmeditor PRIVATE -Wall -Wextra)
    endif()
  endif()
  # The PC game plays in a window when SDL3 is there (from ose-ui, or installed), and plays the
  # test level when started without a pack.
  if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 CONFIG QUIET)
  endif()
  if(TARGET SDL3::SDL3)
    target_sources(game PRIVATE apps/game/host_window.c)
    target_link_libraries(game PRIVATE SDL3::SDL3)
    if(NOT EMSCRIPTEN)
      find_library(BM_GLESV2 GLESv2 REQUIRED)
      target_link_libraries(game PRIVATE ${BM_GLESV2})
    endif()
    target_compile_definitions(game PRIVATE BM_GAME_WINDOW=1)
  endif()
  set(BM_GAME_LEVEL_PACK ${CMAKE_BINARY_DIR}/test_level_gba.bmpk)
  add_custom_command(OUTPUT ${BM_GAME_LEVEL_PACK}
    COMMAND bmcook --profile gba -o ${BM_GAME_LEVEL_PACK}
      ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/room2.tmx
    DEPENDS bmcook ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/room2.tmx
      ${PROJECT_SOURCE_DIR}/assets/test_room/tileset.png
    COMMENT "Cooking the test level for the game")
  add_custom_target(game_level_pack ALL DEPENDS ${BM_GAME_LEVEL_PACK})
  add_dependencies(game game_level_pack)
  target_compile_definitions(game PRIVATE "BM_GAME_DEFAULT_PACK=\"${BM_GAME_LEVEL_PACK}\"")

  add_executable(bm_ppu_render tools/bm_ppu_render.c src/ppu/ppu.c)
  target_include_directories(bm_ppu_render PRIVATE src/ppu)
  target_compile_options(bm_ppu_render PRIVATE ${BM_WARNINGS})

  # Master side of the live link (PC tools and tests).
  add_library(bm_link_host STATIC src/link/link_master.c src/link/live_master.c)
  target_link_libraries(bm_link_host PUBLIC blutmond)
  target_compile_options(bm_link_host PRIVATE ${BM_WARNINGS})
  # bmlink without mGBA (Xboo only); with libmgba it is built further down with both.
  find_path(MGBA_INCLUDE_DIR mgba/core/core.h)
  find_library(MGBA_LIBRARY mgba)
  if(NOT (MGBA_INCLUDE_DIR AND MGBA_LIBRARY))
    add_executable(bmlink tools/bmlink/bmlink.c tools/bmlink/xboo.c tools/bmlink/tcp_link.c)
    target_include_directories(bmlink PRIVATE tools tools/bmlink)
    target_link_libraries(bmlink PRIVATE bm_link_host)
    target_compile_definitions(bmlink PRIVATE _GNU_SOURCE)
    target_compile_options(bmlink PRIVATE ${BM_WARNINGS})
  endif()

  enable_testing()
  foreach(test test_pool test_heaps test_backdrop test_ppu test_pack test_physics test_state test_link)
    add_executable(${test} tests/${test}.c)
    target_link_libraries(${test} PRIVATE blutmond bm_link_host)
    target_include_directories(${test} PRIVATE src/core/private)
    target_compile_options(${test} PRIVATE ${BM_WARNINGS})
    add_test(NAME ${test} COMMAND ${test})
  endforeach()
  add_executable(test_project tests/test_project.cpp)
  target_link_libraries(test_project PRIVATE bm_editor_model)
  target_include_directories(test_project PRIVATE tests)
  target_compile_options(test_project PRIVATE ${BM_WARNINGS})
  add_test(NAME test_project COMMAND test_project ${PROJECT_SOURCE_DIR}/assets ${CMAKE_CURRENT_BINARY_DIR}/test-output/project)
  add_executable(test_vram tests/test_vram.cpp)
  target_link_libraries(test_vram PRIVATE bm_editor_model)
  target_include_directories(test_vram PRIVATE tests)
  target_compile_options(test_vram PRIVATE ${BM_WARNINGS})
  add_test(NAME test_vram COMMAND test_vram)
  # Cooking the test room: GBA pack, SNES report (must cook without errors).
  set(cooked ${CMAKE_CURRENT_BINARY_DIR}/test-output)
  file(MAKE_DIRECTORY ${cooked})
  add_test(NAME cook_test_room_gba
    COMMAND bmcook --profile gba --report ${cooked}/test_room_gba.txt -o ${cooked}/test_room_gba.bmpk
      ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx)
  set_tests_properties(cook_test_room_gba PROPERTIES FIXTURES_SETUP test_room_pack)
  add_test(NAME cook_test_room_snes
    COMMAND bmcook --profile snes --report ${cooked}/test_room_snes.txt -o ${cooked}/test_room_snes.bmpk
      ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx)
  set_tests_properties(cook_test_room_snes PROPERTIES PASS_REGULAR_EXPRESSION "layer-dropped.*foreground")

  # The test level: both rooms in one pack.
  add_test(NAME cook_test_level
    COMMAND bmcook --profile gba -o ${cooked}/test_level_gba.bmpk
      ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/room2.tmx)
  set_tests_properties(cook_test_level PROPERTIES FIXTURES_SETUP test_level_pack)
  add_executable(test_game tests/test_game.c apps/game/autopilot.c apps/game/game_live.c)
  target_link_libraries(test_game PRIVATE blutmond)
  target_include_directories(test_game PRIVATE apps/game)
  target_compile_options(test_game PRIVATE ${BM_WARNINGS})
  add_test(NAME test_game COMMAND test_game ${cooked}/test_level_gba.bmpk)
  set_tests_properties(test_game PROPERTIES FIXTURES_REQUIRED test_level_pack)
  # Live reload: a variant of the level with one background tile changed.
  add_test(NAME make_room_variant
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/make_room_variant.py
      ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx ${cooked}/room_variant.tmx)
  set_tests_properties(make_room_variant PROPERTIES FIXTURES_SETUP room_variant_tmx)
  add_test(NAME cook_test_level_variant
    COMMAND bmcook --profile gba -o ${cooked}/test_level_variant_gba.bmpk
      ${cooked}/room_variant.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/room2.tmx)
  set_tests_properties(cook_test_level_variant PROPERTIES FIXTURES_REQUIRED room_variant_tmx FIXTURES_SETUP test_level_variant)
  add_executable(test_live tests/test_live.c apps/game/autopilot.c apps/game/game_live.c)
  target_link_libraries(test_live PRIVATE blutmond bm_link_host)
  target_include_directories(test_live PRIVATE apps/game)
  target_compile_options(test_live PRIVATE ${BM_WARNINGS})
  add_test(NAME test_live COMMAND test_live ${cooked}/test_level_gba.bmpk ${cooked}/test_level_variant_gba.bmpk)
  set_tests_properties(test_live PROPERTIES FIXTURES_REQUIRED "test_level_pack;test_level_variant")
  add_test(NAME game_autoplay_host COMMAND game ${cooked}/test_level_gba.bmpk --autoplay --frames 1000 ${cooked}/game_host.ppm)
  set_tests_properties(game_autoplay_host PROPERTIES FIXTURES_REQUIRED test_level_pack FIXTURES_SETUP game_host
    PASS_REGULAR_EXPRESSION "room test_room_2.*deaths 0")

  # Map streaming: scrolling to a position must give the same picture as showing the room
  # there directly.
  add_test(NAME room_scrolled COMMAND room_viewer ${cooked}/test_room_gba.bmpk --frames 400 ${cooked}/room_scrolled.ppm)
  add_test(NAME room_fresh COMMAND room_viewer ${cooked}/test_room_gba.bmpk --frames 400 --fresh ${cooked}/room_fresh.ppm)
  add_test(NAME room_midway_scrolled COMMAND room_viewer ${cooked}/test_room_gba.bmpk --frames 137 ${cooked}/room_midway_scrolled.ppm)
  add_test(NAME room_midway_fresh COMMAND room_viewer ${cooked}/test_room_gba.bmpk --frames 137 --fresh ${cooked}/room_midway_fresh.ppm)
  set_tests_properties(room_scrolled room_fresh room_midway_scrolled room_midway_fresh PROPERTIES
    FIXTURES_REQUIRED test_room_pack FIXTURES_SETUP room_renders)
  add_test(NAME room_streaming_matches
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/compare_images.py ${cooked}/room_scrolled.ppm ${cooked}/room_fresh.ppm)
  add_test(NAME room_streaming_matches_midway
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/compare_images.py ${cooked}/room_midway_scrolled.ppm ${cooked}/room_midway_fresh.ppm)
  set_tests_properties(room_streaming_matches room_streaming_matches_midway PROPERTIES FIXTURES_REQUIRED room_renders)

  if(BM_BUILD_EDITOR)
    add_test(NAME editor_offscreen
      COMMAND bmeditor --frames 3 --screenshot ${cooked}/editor.ppm ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx)
    # Mesa's EGL driver, loaded by SDL's offscreen driver, leaks a few allocations at exit;
    # they are not ours, so LeakSanitizer is off for this test.
    set_tests_properties(editor_offscreen PROPERTIES
      ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;EGL_PLATFORM=surfaceless;GALLIUM_DRIVER=llvmpipe;ASAN_OPTIONS=detect_leaks=0")
  endif()

  add_test(NAME no_hardware_addresses
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_hw_addresses.py ${PROJECT_SOURCE_DIR})

  # mGBA runner (optional). With libmgba installed (Ubuntu: libmgba-dev) the host build gets
  # bm_mgba_run, which runs GBA images headless. Point BM_GBA_BUILD_DIR at a GBA build
  # directory to test its images:  cmake -S . -B build -DBM_GBA_BUILD_DIR=$PWD/build-gba
  find_path(MGBA_INCLUDE_DIR mgba/core/core.h)
  find_library(MGBA_LIBRARY mgba)
  if(MGBA_INCLUDE_DIR AND MGBA_LIBRARY)
    add_executable(bm_mgba_run tools/bm_mgba_run.c src/ppu/ppu.c)
    target_include_directories(bm_mgba_run PRIVATE ${MGBA_INCLUDE_DIR} src/ppu tools)
    target_link_libraries(bm_mgba_run PRIVATE ${MGBA_LIBRARY})
    # The libmgba headers need POSIX definitions such as PATH_MAX.
    target_compile_definitions(bm_mgba_run PRIVATE _GNU_SOURCE)

    add_executable(bmlink tools/bmlink/bmlink.c tools/bmlink/xboo.c tools/bmlink/tcp_link.c tools/bmlink/mgba_link.c)
    target_include_directories(bmlink PRIVATE ${MGBA_INCLUDE_DIR} tools tools/bmlink)
    target_link_libraries(bmlink PRIVATE bm_link_host ${MGBA_LIBRARY})
    target_compile_definitions(bmlink PRIVATE _GNU_SOURCE BM_HAVE_MGBA)
    target_compile_options(bmlink PRIVATE -Wall -Wextra -Werror)

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

      # The room viewer on the GBA (pack embedded) must show the same picture as on the host.
      foreach(image room_viewer room_viewer_mb)
        add_test(NAME room_render_${image}
          COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/${image}.gba --frames 420 --ppm ${out}/${image}.ppm)
        set_tests_properties(room_render_${image} PROPERTIES FIXTURES_SETUP room_${image})
        add_test(NAME room_matches_${image}
          COMMAND ${compare} ${out}/room_scrolled.ppm ${out}/${image}.ppm --diff ${out}/${image}_diff.ppm)
        set_tests_properties(room_matches_${image} PROPERTIES FIXTURES_REQUIRED "room_renders;room_${image}")
      endforeach()

      # The autopilot plays the level on the GBA: after it has settled in the second room the
      # picture must match the host's.
      foreach(image game_autoplay game_autoplay_mb)
        # On the GBA too: no deaths, one room change, and at most 4 frames missed (while the
        # new room is loaded: 2 from ROM, 4 from EWRAM in the multiboot image).
        set(elf ${BM_GBA_BUILD_DIR}/${image}.elf)
        add_test(NAME game_render_${image}
          COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/${image}.gba --frames 1100 --ppm ${out}/${image}.ppm
            --check ${elf}:bm_game_stats+8=0 --check ${elf}:bm_game_stats+12=1 --check "${elf}:bm_game_stats+4<=4")
        set_tests_properties(game_render_${image} PROPERTIES FIXTURES_SETUP game_${image})
        add_test(NAME game_matches_${image}
          COMMAND ${compare} ${out}/game_host.ppm ${out}/${image}.ppm --diff ${out}/${image}_diff.ppm)
        set_tests_properties(game_matches_${image} PROPERTIES FIXTURES_REQUIRED "game_host;game_${image}")
      endforeach()

      # Save states move between the platforms (docs/spec/state.md):
      #  - the GBA's snapshot at game frame 300 must be byte for byte the host's;
      #  - the host continues from the GBA's snapshot to the same picture;
      #  - the GBA, given the host's snapshot after 20 frames, continues to the same picture.
      set(elf ${BM_GBA_BUILD_DIR}/game_autoplay.elf)
      add_test(NAME state_snapshot_host
        COMMAND game ${cooked}/test_level_gba.bmpk --autoplay --frames 400 --snapshot-at 300 ${out}/state_host_300.bmst)
      set_tests_properties(state_snapshot_host PROPERTIES FIXTURES_REQUIRED test_level_pack FIXTURES_SETUP state_host)
      add_test(NAME state_snapshot_gba
        COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/game_autoplay.gba --frames 400 --dump ${elf}:bm_state_outbox ${out}/state_gba_300.bmst)
      set_tests_properties(state_snapshot_gba PROPERTIES FIXTURES_SETUP state_gba)
      add_test(NAME state_snapshots_identical
        COMMAND ${CMAKE_COMMAND} -E compare_files ${out}/state_host_300.bmst ${out}/state_gba_300.bmst)
      set_tests_properties(state_snapshots_identical PROPERTIES FIXTURES_REQUIRED "state_host;state_gba")
      add_test(NAME state_gba_to_host
        COMMAND game ${cooked}/test_level_gba.bmpk --autoplay --load ${out}/state_gba_300.bmst --frames 700 ${out}/state_gba_to_host.ppm)
      set_tests_properties(state_gba_to_host PROPERTIES FIXTURES_REQUIRED "test_level_pack;state_gba" FIXTURES_SETUP state_gba_to_host)
      add_test(NAME state_gba_to_host_matches
        COMMAND ${compare} ${out}/game_host.ppm ${out}/state_gba_to_host.ppm)
      set_tests_properties(state_gba_to_host_matches PROPERTIES FIXTURES_REQUIRED "game_host;state_gba_to_host")
      add_test(NAME state_host_to_gba
        COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/game_autoplay.gba --frames 1100 --poke 20 ${elf}:bm_state_inbox ${out}/state_host_300.bmst
          --ppm ${out}/state_host_to_gba.ppm --check ${elf}:bm_game_stats+8=0 --check ${elf}:bm_game_stats+12=1
          --check ${elf}:bm_game_stats+20=1 --print ${elf}:bm_game_stats+24)
      set_tests_properties(state_host_to_gba PROPERTIES FIXTURES_REQUIRED state_host FIXTURES_SETUP state_host_to_gba)
      add_test(NAME state_host_to_gba_matches
        COMMAND ${compare} ${out}/game_host.ppm ${out}/state_host_to_gba.ppm)
      set_tests_properties(state_host_to_gba_matches PROPERTIES FIXTURES_REQUIRED "game_host;state_host_to_gba")

      # Live link (docs/spec/link-protocol.md) with the game running in mGBA, driven word by word
      # through its serial port by bmlink:
      #  - a changed room pushed hot changes exactly the one tile on screen, without a reset;
      #  - with bit errors and lost words on the wire, the autoplaying game takes a changed pack,
      #    a state and a restart and still plays through (cartridge and multiboot image).
      set(elf ${BM_GBA_BUILD_DIR}/game.elf)
      add_test(NAME live_mgba_hot
        COMMAND bmlink --mgba ${BM_GBA_BUILD_DIR}/game.gba run 60 screenshot ${out}/live_before.ppm
          push ${cooked}/test_level_variant_gba.bmpk hot run 2 screenshot ${out}/live_after.ppm
          check ${elf}:bm_game_stats+28=1 check ${elf}:bm_game_stats+12=0 stats)
      set_tests_properties(live_mgba_hot PROPERTIES FIXTURES_REQUIRED test_level_variant FIXTURES_SETUP live_mgba_hot)
      add_test(NAME live_mgba_hot_changes_one_tile
        COMMAND ${compare} ${out}/live_before.ppm ${out}/live_after.ppm --changed-in 0 0 15 15)
      set_tests_properties(live_mgba_hot_changes_one_tile PROPERTIES FIXTURES_REQUIRED live_mgba_hot)
      # The video state read over the link (PPU_SNAPSHOT), rendered by the software PPU, is
      # what mGBA shows; with bit errors none of its 100 packets may be lost.
      add_test(NAME live_mgba_ppu_snapshot
        COMMAND bmlink --mgba ${BM_GBA_BUILD_DIR}/game.gba --errors 1000 1000 run 120 ppu-snapshot ${out}/live_snapshot.bmps
          screenshot ${out}/live_snapshot_mgba.ppm stats)
      set_tests_properties(live_mgba_ppu_snapshot PROPERTIES FIXTURES_SETUP live_mgba_ppu_snapshot)
      add_test(NAME live_mgba_ppu_snapshot_render COMMAND bm_ppu_render ${out}/live_snapshot.bmps ${out}/live_snapshot_ppu.ppm)
      set_tests_properties(live_mgba_ppu_snapshot_render PROPERTIES FIXTURES_REQUIRED live_mgba_ppu_snapshot FIXTURES_SETUP live_mgba_ppu_render)
      add_test(NAME live_mgba_ppu_snapshot_matches COMMAND ${compare} ${out}/live_snapshot_mgba.ppm ${out}/live_snapshot_ppu.ppm)
      set_tests_properties(live_mgba_ppu_snapshot_matches PROPERTIES FIXTURES_REQUIRED live_mgba_ppu_render)
      foreach(image game_autoplay game_autoplay_mb)
        set(elf ${BM_GBA_BUILD_DIR}/${image}.elf)
        add_test(NAME live_mgba_bit_errors_${image}
          COMMAND bmlink --mgba ${BM_GBA_BUILD_DIR}/${image}.gba --errors 2000 2000 run 30 hello
            push ${cooked}/test_level_variant_gba.bmpk hot
            state-get ${out}/live_${image}.bmst run 30 state-put ${out}/live_${image}.bmst
            push ${cooked}/test_level_gba.bmpk restart:test_room
            push ${cooked}/test_level_variant_gba.bmpk hot run 1200
            check ${elf}:bm_game_stats+28=4 check ${elf}:bm_game_stats+8=0 check ${elf}:bm_game_stats+12=1 stats)
        set_tests_properties(live_mgba_bit_errors_${image} PROPERTIES FIXTURES_REQUIRED "test_level_pack;test_level_variant")
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
