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

if(BM_PLATFORM STREQUAL "gba")
  # The multiboot images must fit into 256 KB, which they only do built for size. Without a
  # build type, or with one whose flags have no optimisation level, build for size anyway.
  string(TOUPPER "${CMAKE_BUILD_TYPE}" bm_build_type)
  if(NOT "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${bm_build_type}}" MATCHES "-O")
    message(STATUS "No optimisation level in the C flags: building the GBA images with -Os")
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-Os>)
  endif()
endif()

set(BM_WARNINGS -Wall -Wextra -Werror -pedantic)

# --- Engine library -----------------------------------------------------------------------

# What the test level is cooked from besides its rooms. The base: the first two rooms, whose
# scripts compiled to native code fit the 12 KB native slot in IWRAM; the native-script images
# play it. The full level adds languages, menus and the boss; every other image plays that.
set(BM_TEST_LEVEL_BASE_ROOMS ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/room2.tmx)
set(BM_TEST_LEVEL_BASE_ASSETS
  ${PROJECT_SOURCE_DIR}/assets/test_room/player_legs.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/player_torso.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/player.bmfigure
  ${PROJECT_SOURCE_DIR}/assets/test_room/bat.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/bat.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/door.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/pickup.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/high_jump.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/dummy.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/weapon_swing.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/weapon_stand.ms
  ${PROJECT_SOURCE_DIR}/assets/engine/font.bmfont
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/enemies.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/enemies.bmdata
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/items.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/weapons.bmdata
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/consumables.bmdata
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/castle.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/jump.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/hurt.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/world.bmworld
  ${PROJECT_SOURCE_DIR}/assets/test_room/save.bmschema)
# Samples the songs play (not given to the cooker: the songs name them), so a changed one cooks again.
file(GLOB BM_TEST_LEVEL_SAMPLES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/assets/test_room/music/*.wav)
set(BM_TEST_LEVEL_ROOMS ${BM_TEST_LEVEL_BASE_ROOMS} ${PROJECT_SOURCE_DIR}/assets/test_room/boss_hall.tmx
  ${PROJECT_SOURCE_DIR}/assets/test_room/ability_hall.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/guard_room.tmx
  ${PROJECT_SOURCE_DIR}/assets/test_room/puzzle_room.tmx
  ${PROJECT_SOURCE_DIR}/assets/test_room/chasm_room.tmx
  ${PROJECT_SOURCE_DIR}/assets/test_room/debug_lobby.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/debug_wave.tmx
  ${PROJECT_SOURCE_DIR}/assets/test_room/debug_spotlight.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/debug_pulse.tmx
  ${PROJECT_SOURCE_DIR}/assets/test_room/debug_gradient.tmx
  ${PROJECT_SOURCE_DIR}/assets/test_room/torch_hall.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/torch_hall_layer.tmx
  ${PROJECT_SOURCE_DIR}/assets/test_room/torch_hall_fitted.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/torch_hall_normals.tmx)
# The PC and browser game's pack adds the streamed torch hall (its rows of light are 43 KB; the
# packs the tests compare and push between devices, the cartridge's and the multiboot pack leave
# it out). On the GBA it comes pushed over the link into the cartridge game (light_mgba_*).
set(BM_TEST_LEVEL_CART_ROOMS ${BM_TEST_LEVEL_ROOMS} ${PROJECT_SOURCE_DIR}/assets/test_room/torch_hall_stream.tmx)
set(BM_TEST_LEVEL_ASSETS ${BM_TEST_LEVEL_BASE_ASSETS}
  ${PROJECT_SOURCE_DIR}/assets/test_room/torch.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/torch.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/spells.bmdata
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/abilities.bmdata
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/armour.bmdata
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/key_items.bmdata
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/armour_stats.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/armour_heavy.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/armour_thorns.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/use_restore.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/use_return.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/dialogue.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/speakers.bmdata
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/lines.bmdata
  ${PROJECT_SOURCE_DIR}/assets/test_room/hero_face.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/vesper_face.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/vesper.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/voice_hero.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/voice_vesper.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/teleport.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/hit_bat.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/hit_moon.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/hit_dummy.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/pickup.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/fanfare.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/use_debug_mirror.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/teleporter.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/drop.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/moon_bat.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/fireball.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/moon_bat.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/knight.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/knight.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/clang.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/spark.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/debris.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/crate.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/bat_armour.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/puzzle.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/drag.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/thud.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/grapple.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/grapple.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/hook.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/crate.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/music/crumble.bmsong
  ${PROJECT_SOURCE_DIR}/assets/test_room/fireball.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/drop.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/door_panel.bmsprite
  ${PROJECT_SOURCE_DIR}/assets/test_room/screens/start_menu.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/screens/equipment_menu.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/screens/items_menu.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/screens/system_menu.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/screens/commands_menu.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/screens/map_menu.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/ability_dash.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/ability_pound.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/ability_second_wind.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/pedestal.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/data/commands.bmdata
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/command_strike.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/command_absorb.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/command_gravity.ms
  ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/gift.ms
  ${PROJECT_SOURCE_DIR}/assets/engine/font_latin.bmfont
  ${PROJECT_SOURCE_DIR}/assets/test_room/lang/de.bmlang
  ${PROJECT_SOURCE_DIR}/assets/test_room/lang/fr.bmlang)

file(GLOB BM_CORE_SOURCES CONFIGURE_DEPENDS src/core/public/*.c)
file(GLOB BM_GRAPHICS_SOURCES CONFIGURE_DEPENDS src/graphics/public/*.c)
set(BM_PACK_SOURCES src/pack/pack.c)
file(GLOB BM_WORLD_SOURCES CONFIGURE_DEPENDS src/world/*.c src/ui/*.c)
set(BM_LINK_SOURCES src/link/link.c src/link/live.c src/link/multiboot.c src/link/transfer.c src/link/link_master.c src/link/live_master.c src/ppu/ppu_snapshot_part.c)
set(BM_SCRIPT_SOURCES src/script/vm.c src/script/native.c)
set(BM_SOUND_SOURCES src/sound/sound.c)
set(BM_DATA_SOURCES src/data/data.c)

if(BM_PLATFORM STREQUAL "gba")
  # The software PPU is not needed on the device; only its register definitions are used.
  set(BM_HAL_SOURCES src/hal/gba/hal_gba.c src/hal/gba/irq.S src/hal/gba/debug.c src/hal/gba/debug_trap.S)
  set(BM_HAL_DIR src/hal/gba)
else()
  set(BM_HAL_SOURCES src/hal/host/hal_host.c src/ppu/ppu.c)
  set(BM_HAL_DIR src/hal/host)
endif()

add_library(blutmond STATIC ${BM_CORE_SOURCES} ${BM_GRAPHICS_SOURCES} ${BM_PACK_SOURCES} ${BM_WORLD_SOURCES} ${BM_LINK_SOURCES}
  ${BM_SCRIPT_SOURCES} ${BM_SOUND_SOURCES} ${BM_DATA_SOURCES} ${BM_HAL_SOURCES})
target_include_directories(blutmond
  PUBLIC src/core/public src/graphics/public src/hal/public src/ppu src/pack src/world src/ui src/link src/script src/sound src/data ${BM_HAL_DIR}
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
      # Everything of a multiboot image shares 256 KB; the content pool gets what is left
      # (src/hal/gba/gba_mb.ld).
      target_compile_definitions(${target} PRIVATE BM_GBA_MULTIBOOT)
      # Without the light (src/hal/gba/light_off.c says why).
      target_sources(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src/hal/gba/light_off.c)
    endif()
    target_link_options(${target} PRIVATE
      -T ${PROJECT_SOURCE_DIR}/src/hal/gba/gba_${kind}.ld
      -Wl,-Map=$<TARGET_FILE_DIR:${target}>/${target}.map)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
      ${PROJECT_SOURCE_DIR}/src/hal/gba/gba_${kind}.ld)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> $<TARGET_FILE_DIR:${target}>/${target}.gba
      COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gbafix.py $<TARGET_FILE_DIR:${target}>/${target}.gba --pad
      BYPRODUCTS ${CMAKE_BINARY_DIR}/${target}.gba
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
  # The scene with windows, colour effects and all three line tables.
  bm_add_gba_image(scene_effects ${BM_SCENE_SOURCES})
  foreach(target ${BM_GBA_TARGETS})
    target_compile_definitions(${target} PRIVATE BM_SCENE_EFFECTS=1)
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
    DEPENDS host_tools ${BM_BMCOOK} ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx ${PROJECT_SOURCE_DIR}/assets/test_room/tileset.png ${PROJECT_SOURCE_DIR}/assets/test_room/torch_tiles.png ${PROJECT_SOURCE_DIR}/assets/test_room/torch_tiles_n.png
    COMMENT "Cooking the test room")
  add_custom_target(test_room_pack DEPENDS ${BM_TEST_ROOM_PACK})

  set(BM_TEST_LEVEL_PACK ${CMAKE_BINARY_DIR}/test_level_gba.bmpk)
  add_custom_command(OUTPUT ${BM_TEST_LEVEL_PACK}
    COMMAND ${BM_BMCOOK} --profile gba -o ${BM_TEST_LEVEL_PACK} ${BM_TEST_LEVEL_ROOMS} ${BM_TEST_LEVEL_ASSETS}
    DEPENDS host_tools ${BM_BMCOOK} ${BM_TEST_LEVEL_ROOMS} ${BM_TEST_LEVEL_ASSETS} ${BM_TEST_LEVEL_SAMPLES} ${PROJECT_SOURCE_DIR}/assets/test_room/tileset.png ${PROJECT_SOURCE_DIR}/assets/test_room/torch_tiles.png ${PROJECT_SOURCE_DIR}/assets/test_room/torch_tiles_n.png
    COMMENT "Cooking the test level")
  # For the multiboot images, which share 256 KB with the engine: the same level with the debug
  # rooms, the boss hall (with the boss's sprites, music and voice), the Hall of Abilities behind it,
  # the Guard Room (with its knight and the Iron Bat's armour)
  # and the translations left out (pack-format.md §0: their entries stay, flagged absent); a
  # fitted push brings them later (runtime-abi.md §0).
  set(BM_TEST_LEVEL_ABSENT --absent-room debug_lobby --absent-room debug_wave --absent-room debug_spotlight
    --absent-room debug_pulse --absent-room debug_gradient --absent-room boss_hall --absent-room ability_hall
    --absent-asset vesper --absent-asset moon_bat --absent-asset vesper_face --absent-asset voice_vesper
    --absent-asset fireball --absent-room guard_room --absent-asset knight --absent-asset bat_armour --absent-asset clang
    --absent-asset torch
    --absent-kind LANG)
  # The torch halls (docs/plans/lighting.md) are not in it at all, not even as absent entries:
  # the multiboot images have no light, and the halls come last, so the other rooms keep their
  # numbers.
  set(BM_TEST_LEVEL_MB_ROOMS ${BM_TEST_LEVEL_ROOMS})
  list(FILTER BM_TEST_LEVEL_MB_ROOMS EXCLUDE REGEX "torch_hall")
  set(BM_TEST_LEVEL_MB_PACK ${CMAKE_BINARY_DIR}/test_level_mb_gba.bmpk)
  add_custom_command(OUTPUT ${BM_TEST_LEVEL_MB_PACK}
    COMMAND ${BM_BMCOOK} --profile gba ${BM_TEST_LEVEL_ABSENT} -o ${BM_TEST_LEVEL_MB_PACK} ${BM_TEST_LEVEL_MB_ROOMS} ${BM_TEST_LEVEL_ASSETS}
    DEPENDS host_tools ${BM_BMCOOK} ${BM_TEST_LEVEL_ROOMS} ${BM_TEST_LEVEL_ASSETS} ${BM_TEST_LEVEL_SAMPLES} ${PROJECT_SOURCE_DIR}/assets/test_room/tileset.png ${PROJECT_SOURCE_DIR}/assets/test_room/torch_tiles.png ${PROJECT_SOURCE_DIR}/assets/test_room/torch_tiles_n.png
    COMMENT "Cooking the test level for multiboot")
  # The same level with the scripts also compiled to native code (mondskript.md section 12).
  set(BM_TEST_LEVEL_NATIVE_PACK ${CMAKE_BINARY_DIR}/test_level_native_gba.bmpk)
  add_custom_command(OUTPUT ${BM_TEST_LEVEL_NATIVE_PACK}
    COMMAND ${BM_BMCOOK} --profile gba --native ${CMAKE_C_COMPILER} --native-elf ${CMAKE_BINARY_DIR}/test_level_native.elf
      --script-c ${CMAKE_BINARY_DIR}/test_level_native.c -o ${BM_TEST_LEVEL_NATIVE_PACK}
      ${BM_TEST_LEVEL_BASE_ROOMS} ${BM_TEST_LEVEL_BASE_ASSETS}
    DEPENDS host_tools ${BM_BMCOOK} ${BM_TEST_LEVEL_BASE_ROOMS} ${BM_TEST_LEVEL_BASE_ASSETS}
      ${PROJECT_SOURCE_DIR}/assets/test_room/tileset.png ${PROJECT_SOURCE_DIR}/assets/test_room/torch_tiles.png ${PROJECT_SOURCE_DIR}/assets/test_room/torch_tiles_n.png ${PROJECT_SOURCE_DIR}/src/script/native.h ${PROJECT_SOURCE_DIR}/src/script/vm.h
      ${PROJECT_SOURCE_DIR}/tools/mondskript/aot.cpp ${PROJECT_SOURCE_DIR}/tools/cooker/native_module.cpp
    COMMENT "Cooking the test level with native scripts")
  add_custom_target(test_level_pack DEPENDS ${BM_TEST_LEVEL_PACK} ${BM_TEST_LEVEL_MB_PACK} ${BM_TEST_LEVEL_NATIVE_PACK})

  # The game, played with the buttons, and the same with the autopilot (for tests and demos).
  # embed_pack.S is compiled per target with that target's BM_PACK_PATH.
  # The bootstrap (link-protocol.md section 9): a multiboot image on its own, without the engine.
  add_executable(bootstrap_mb src/hal/gba/bootstrap/bootstrap.c src/hal/gba/crt0.S)
  set_target_properties(bootstrap_mb PROPERTIES SUFFIX ".elf")
  target_include_directories(bootstrap_mb PRIVATE src/link)
  target_compile_definitions(bootstrap_mb PRIVATE BM_GBA_MULTIBOOT BM_PLATFORM_GBA)
  target_compile_options(bootstrap_mb PRIVATE $<$<COMPILE_LANGUAGE:C>:${BM_WARNINGS}>)
  target_link_options(bootstrap_mb PRIVATE -T ${PROJECT_SOURCE_DIR}/src/hal/gba/gba_mb.ld -nostdlib
    -Wl,-Map=${CMAKE_BINARY_DIR}/bootstrap_mb.map)
  add_custom_command(TARGET bootstrap_mb POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:bootstrap_mb> ${CMAKE_BINARY_DIR}/bootstrap_mb.gba
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gbafix.py ${CMAKE_BINARY_DIR}/bootstrap_mb.gba --pad
    BYPRODUCTS ${CMAKE_BINARY_DIR}/bootstrap_mb.gba
    COMMENT "Writing bootstrap_mb.gba")

  # game_send: the autoplaying game that, at frame 800 (in the second room), brings the GBA on the other end of the
  # cable to where it is (tests). The cartridge game does the same on Select + Start.
  # game_overlay: the autoplaying game with the debug overlay on (tests).
  # game_runtime: the game with no pack inside: it waits for one over the link, showing a
  # loading box (the editor boots it and pushes the project's pack once).
  file(WRITE ${CMAKE_BINARY_DIR}/no_pack.bmpk "")
  foreach(variant game game_autoplay game_autoplay_native game_send game_overlay game_runtime)
    bm_add_gba_image(${variant} apps/game/main.c apps/game/autopilot.c apps/game/game_live.c apps/game/overlay.c ${PROJECT_SOURCE_DIR}/src/hal/gba/embed_pack.S)
    if(variant STREQUAL "game_overlay")
      foreach(target ${BM_GBA_TARGETS})
        target_compile_definitions(${target} PRIVATE BM_OVERLAY=1)
      endforeach()
    endif()
    if(variant STREQUAL "game" OR variant STREQUAL "game_send")
      # Only cartridge images can boot another GBA: the images they send are in their ROM.
      target_sources(${variant} PRIVATE ${PROJECT_SOURCE_DIR}/src/hal/gba/embed_transfer.S)
      # What is sent as the runtime is the game's own multiboot image.
      add_dependencies(${variant} bootstrap_mb game_mb)
      target_compile_definitions(${variant} PRIVATE BM_GAME_SEND "BM_BOOTSTRAP_PATH=\"${CMAKE_BINARY_DIR}/bootstrap_mb.gba\""
        "BM_RUNTIME_PATH=\"${CMAKE_BINARY_DIR}/game_mb.gba\"")
      if(variant STREQUAL "game_send")
        target_compile_definitions(${variant} PRIVATE BM_SEND_FRAME=800)
      endif()
    endif()
    foreach(target ${BM_GBA_TARGETS})
      add_dependencies(${target} test_level_pack)
      target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/apps/game)
      if(variant STREQUAL "game_autoplay_native" OR (variant STREQUAL "game" AND NOT target MATCHES "_mb$"))
        # These run native code (mondskript.md §12): 12 KB of IWRAM for it.
        target_compile_definitions(${target} PRIVATE BM_NATIVE_SLOT_BYTES=14336)
      endif()
      if(variant STREQUAL "game_autoplay_native")
        target_compile_definitions(${target} PRIVATE "BM_PACK_PATH=\"${BM_TEST_LEVEL_NATIVE_PACK}\"")
      elseif(variant STREQUAL "game_runtime")
        target_compile_definitions(${target} PRIVATE "BM_PACK_PATH=\"${CMAKE_BINARY_DIR}/no_pack.bmpk\"")
      elseif(target MATCHES "_mb$")
        target_compile_definitions(${target} PRIVATE "BM_PACK_PATH=\"${BM_TEST_LEVEL_MB_PACK}\"")
      else()
        target_compile_definitions(${target} PRIVATE "BM_PACK_PATH=\"${BM_TEST_LEVEL_PACK}\"")
      endif()
      if(NOT variant STREQUAL "game" AND NOT variant STREQUAL "game_runtime")
        # The autoplay image also writes a snapshot at game frame 300 (for the state tests).
        target_compile_definitions(${target} PRIVATE BM_AUTOPLAY=1 BM_SNAPSHOT_FRAME=300)
      endif()
    endforeach()
  endforeach()


  # Scripts interpreted against scripts as native code: a speed comparison (tests read it).
  set(BM_SCRIPT_BENCH_PACK ${CMAKE_BINARY_DIR}/script_bench_gba.bmpk)
  add_custom_command(OUTPUT ${BM_SCRIPT_BENCH_PACK}
    COMMAND ${BM_BMCOOK} --profile gba --native ${CMAKE_C_COMPILER} -o ${BM_SCRIPT_BENCH_PACK} ${PROJECT_SOURCE_DIR}/tests/scripts/bench.ms
    DEPENDS host_tools ${BM_BMCOOK} ${PROJECT_SOURCE_DIR}/tests/scripts/bench.ms ${PROJECT_SOURCE_DIR}/src/script/native.h ${PROJECT_SOURCE_DIR}/src/script/vm.h
    COMMENT "Cooking the script benchmark")
  add_custom_target(script_bench_pack DEPENDS ${BM_SCRIPT_BENCH_PACK})
  bm_add_gba_image(script_bench apps/script_bench/main.c ${PROJECT_SOURCE_DIR}/src/hal/gba/embed_pack.S)
  foreach(target ${BM_GBA_TARGETS})
    add_dependencies(${target} script_bench_pack)
    target_compile_definitions(${target} PRIVATE "BM_PACK_PATH=\"${BM_SCRIPT_BENCH_PACK}\"")
  endforeach()

  set_source_files_properties(${PROJECT_SOURCE_DIR}/src/hal/gba/embed_transfer.S PROPERTIES
    OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/bootstrap_mb.gba;${CMAKE_BINARY_DIR}/game_mb.gba")

  bm_add_gba_image(room_viewer apps/room_viewer/main.c ${PROJECT_SOURCE_DIR}/src/hal/gba/embed_pack.S)
  # The embedded pack is pulled in by the assembler, which the build system cannot see: every
  # image that embeds one is assembled again when either pack changes.
  set_source_files_properties(${PROJECT_SOURCE_DIR}/src/hal/gba/embed_pack.S PROPERTIES
    OBJECT_DEPENDS "${BM_TEST_ROOM_PACK};${BM_TEST_LEVEL_PACK};${BM_TEST_LEVEL_MB_PACK};${BM_TEST_LEVEL_NATIVE_PACK};${BM_SCRIPT_BENCH_PACK}")
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
      add_executable(${app} apps/game/main.c apps/game/autopilot.c apps/game/game_live.c apps/game/host_link.c apps/game/overlay.c)
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
  # The Mondskript compiler (tools/mondskript), with the interpreter's verifier.
  add_library(bm_script STATIC tools/mondskript/mondskript.cpp tools/mondskript/aot.cpp src/script/vm.c src/script/native.c)
  target_include_directories(bm_script PUBLIC tools/mondskript src/script src/data src/core/public)
  target_compile_definitions(bm_script PUBLIC BM_PLATFORM_HOST)
  target_compile_options(bm_script PRIVATE ${BM_WARNINGS})

  add_library(bm_cooker STATIC
    tools/cooker/cook.cpp tools/cooker/tmx.cpp tools/cooker/xml.cpp tools/cooker/image.cpp tools/cooker/overrides.cpp tools/cooker/sprite_asset.cpp tools/cooker/song_asset.cpp tools/cooker/native_module.cpp tools/cooker/data_asset.cpp tools/cooker/font_asset.cpp tools/cooker/lang_asset.cpp tools/cooker/world_asset.cpp tools/cooker/figure_asset.cpp tools/cooker/light_bake.cpp src/sound/sound.c src/pack/pack.c src/data/data.c)
  target_include_directories(bm_cooker PUBLIC tools/cooker src/pack src/sound src/data src/core/public PRIVATE third_party/stb)
  target_link_libraries(bm_cooker PUBLIC bm_script)
  target_compile_definitions(bm_cooker PUBLIC BM_PLATFORM_HOST PRIVATE "BM_NATIVE_INCLUDE_DIR=\"${PROJECT_SOURCE_DIR}/src/script\"")
  target_compile_options(bm_cooker PRIVATE ${BM_WARNINGS})
  # The editor's project model (project file, asset catalogue, new rooms), without UI.
  add_library(bm_editor_model STATIC editor/project_file.cpp editor/vram.cpp editor/rom_export.cpp editor/data_file.cpp)
  target_include_directories(bm_editor_model PUBLIC editor src/pack)
  target_link_libraries(bm_editor_model PUBLIC bm_cooker blutmond)
  target_compile_options(bm_editor_model PRIVATE ${BM_WARNINGS})
  # Export on the command line: the game's cartridge image with a pack appended (editor/rom_export.h).
  add_executable(bmexport tools/bmexport.cpp)
  target_link_libraries(bmexport PRIVATE bm_editor_model)
  target_compile_options(bmexport PRIVATE ${BM_WARNINGS})
  # The save game's layout for the project's schema history (docs/spec/state.md §7).
  add_executable(bmschema tools/bmschema.c)
  target_link_libraries(bmschema PRIVATE blutmond)
  target_compile_options(bmschema PRIVATE ${BM_WARNINGS})
  add_executable(bmcook tools/cooker/main.cpp)
  target_link_libraries(bmcook PRIVATE bm_cooker)
  target_compile_options(bmcook PRIVATE ${BM_WARNINGS})
  if(EMSCRIPTEN)
    # Built with Emscripten, the cooker runs under Node during the build and reads real files.
    target_link_options(bmcook PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1)
  endif()

  # Editor (on ose-ui with SDL3 and GLES 2). ose-ui is fetched at a pinned commit; point
  # FETCHCONTENT_SOURCE_DIR_OSE_UI at a local checkout to work on both.
  option(BM_BUILD_EDITOR "Build the editor (downloads ose-ui and, if not installed, SDL3)" ON)
  if(BM_BUILD_EDITOR)
    include(FetchContent)
    FetchContent_Declare(ose_ui
      GIT_REPOSITORY https://github.com/Fighter19/ose-ui.git
      GIT_TAG 0bf77bb4d40d8e1e001605383bdf9d30e6b83c40)
    FetchContent_MakeAvailable(ose_ui)
    if(EMSCRIPTEN)
      # Emscripten's libc++ does not bring <cstdlib> in through other headers as libstdc++ does.
      foreach(t ose_ui ose_ui_gles2 ose_ui_sdl3)
        if(TARGET ${t})
          target_compile_options(${t} PRIVATE "SHELL:-include cstdlib")
        endif()
      endforeach()
    endif()
    add_executable(bmeditor editor/main.cpp editor/project.cpp editor/views.cpp editor/runner.cpp editor/graphics_view.cpp
      editor/dialogs.cpp editor/credits.cpp editor/data_panel.cpp editor/project_panel.cpp editor/palette_panel.cpp editor/animation_panel.cpp
      editor/code_edit.cpp editor/script_panel.cpp editor/sound_panel.cpp editor/world_panel.cpp ${CMAKE_BINARY_DIR}/generated/build_version.c)
    set_target_properties(bmeditor PROPERTIES CXX_STANDARD 20)
    # The commit and build date for Help > Version: looked at in every build, rewritten when the
    # commit changes (cmake/version.cmake).
    add_custom_target(bm_build_version
      COMMAND ${CMAKE_COMMAND} -DSRC=${PROJECT_SOURCE_DIR} -DOUT=${CMAKE_BINARY_DIR}/generated/build_version.c
        -P ${PROJECT_SOURCE_DIR}/cmake/version.cmake
      BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/build_version.c)
    add_dependencies(bmeditor bm_build_version)
    target_link_libraries(bmeditor PRIVATE bm_editor_model bm_cooker blutmond ose_ui_sdl3)
    set(BM_GBA_BUILD_DIR "" CACHE PATH "GBA build directory (runtime images for the editor and the mGBA tests)")
    if(EMSCRIPTEN)
      # In the browser (docs/USAGE.md, "The editor in the browser"): the test project and the
      # game's cartridge image are packed into the page's file system under /blutmond, so
      # Export works; Run has no game or bmlink to start there.
      target_compile_definitions(bmeditor PRIVATE
        "BM_EDITOR_DEFAULT_PROJECT=\"/blutmond/assets/test.bmproj\""
        "BM_EDITOR_ENGINE_DIR=\"/blutmond/assets/engine\"")
      target_sources(bmeditor PRIVATE editor/web_stubs.c)
      set_property(TARGET bmeditor APPEND PROPERTY LINK_DEPENDS ${PROJECT_SOURCE_DIR}/web/shell.html)
      set_target_properties(bmeditor PROPERTIES SUFFIX ".html")
      target_link_options(bmeditor PRIVATE -sMAX_WEBGL_VERSION=1 -sMIN_WEBGL_VERSION=1 -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=1048576 -sEXPORTED_RUNTIME_METHODS=FS "--shell-file=${PROJECT_SOURCE_DIR}/web/shell.html"
        "--preload-file=${ose_ui_SOURCE_DIR}/fonts@/fonts" "--preload-file=${ose_ui_SOURCE_DIR}/themes@/themes"
        "--preload-file=${PROJECT_SOURCE_DIR}/assets@/blutmond/assets")
      if(BM_GBA_BUILD_DIR)
        target_compile_definitions(bmeditor PRIVATE "BM_EDITOR_GBA_ROM=\"/blutmond/game.gba\"")
        target_link_options(bmeditor PRIVATE "--preload-file=${BM_GBA_BUILD_DIR}/game.gba@/blutmond/game.gba")
        set_property(TARGET bmeditor APPEND PROPERTY LINK_DEPENDS ${BM_GBA_BUILD_DIR}/game.gba)
      endif()
    else()
      target_compile_definitions(bmeditor PRIVATE
        "BM_EDITOR_DEFAULT_PROJECT=\"${PROJECT_SOURCE_DIR}/assets/test.bmproj\""
        "BM_EDITOR_ENGINE_DIR=\"${PROJECT_SOURCE_DIR}/assets/engine\""
        "BM_EDITOR_GAME=\"$<TARGET_FILE:game>\""
        "BM_EDITOR_BMLINK=\"$<TARGET_FILE:bmlink>\"")
      # Run on the GBA multiboots the runtime from a GBA build (or set gba-image in editor.conf);
      # Export appends the pack to the cartridge image (or set gba-rom).
      if(BM_GBA_BUILD_DIR)
        target_compile_definitions(bmeditor PRIVATE "BM_EDITOR_GBA_IMAGE=\"${BM_GBA_BUILD_DIR}/game_runtime_mb.gba\""
          "BM_EDITOR_GBA_ROM=\"${BM_GBA_BUILD_DIR}/game.gba\"")
      endif()
      # Run needs the game and bmlink next to the editor.
      add_dependencies(bmeditor game bmlink)
    endif()
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
    COMMAND bmcook --profile gba -o ${BM_GAME_LEVEL_PACK} ${BM_TEST_LEVEL_CART_ROOMS} ${BM_TEST_LEVEL_ASSETS}
    DEPENDS bmcook ${BM_TEST_LEVEL_CART_ROOMS} ${BM_TEST_LEVEL_ASSETS} ${BM_TEST_LEVEL_SAMPLES}
      ${PROJECT_SOURCE_DIR}/assets/test_room/tileset.png ${PROJECT_SOURCE_DIR}/assets/test_room/torch_tiles.png ${PROJECT_SOURCE_DIR}/assets/test_room/torch_tiles_n.png
    COMMENT "Cooking the test level for the game")
  add_custom_target(game_level_pack ALL DEPENDS ${BM_GAME_LEVEL_PACK})
  add_dependencies(game game_level_pack)
  if(EMSCRIPTEN)
    # The game in the browser plays the test level, packed into the page's file system.
    target_compile_definitions(game PRIVATE "BM_GAME_DEFAULT_PACK=\"/blutmond/test_level.bmpk\"")
    set_target_properties(game PROPERTIES SUFFIX ".html")
    # Run from the editor in the browser it takes its packs from the editor (web_editor.h).
    target_sources(game PRIVATE apps/game/web_editor.c)
    target_link_options(game PRIVATE -sMAX_WEBGL_VERSION=1 -sMIN_WEBGL_VERSION=1 -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY
      -sEXPORTED_FUNCTIONS=_main,_malloc,_free
      "--shell-file=${PROJECT_SOURCE_DIR}/web/shell.html" "--preload-file=${BM_GAME_LEVEL_PACK}@/blutmond/test_level.bmpk")
    set_property(TARGET game APPEND PROPERTY LINK_DEPENDS ${PROJECT_SOURCE_DIR}/web/shell.html ${BM_GAME_LEVEL_PACK})
  else()
    target_compile_definitions(game PRIVATE "BM_GAME_DEFAULT_PACK=\"${BM_GAME_LEVEL_PACK}\"")
  endif()

  add_executable(bm_ppu_render tools/bm_ppu_render.c src/ppu/ppu.c)
  target_include_directories(bm_ppu_render PRIVATE src/ppu)
  target_compile_options(bm_ppu_render PRIVATE ${BM_WARNINGS})

  # Master side of the live link (PC tools and tests).
  add_library(bm_link_host INTERFACE)
  target_link_libraries(bm_link_host INTERFACE blutmond)
  # bmlink without mGBA (Xboo only); with libmgba it is built further down with both.
  # libmgba is optional: with it the GBA images are tested in the emulator. BM_USE_MGBA=OFF
  # builds as if it were not installed.
  option(BM_USE_MGBA "Use libmgba if it is installed (emulator tests, bmlink --mgba)" ON)
  set(BM_HAVE_MGBA OFF)
  if(BM_USE_MGBA)
    find_path(MGBA_INCLUDE_DIR mgba/core/core.h)
    find_library(MGBA_LIBRARY mgba)
    if(MGBA_INCLUDE_DIR AND MGBA_LIBRARY)
      set(BM_HAVE_MGBA ON)
    endif()
  endif()
  if(NOT BM_HAVE_MGBA)
    add_executable(bmlink tools/bmlink/bmlink.c tools/bmlink/xboo.c tools/bmlink/tcp_link.c tools/bmlink/gdb_bridge.c)
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
  add_executable(test_world tests/test_world.cpp)
  target_link_libraries(test_world PRIVATE bm_cooker)
  target_include_directories(test_world PRIVATE tests)
  target_compile_options(test_world PRIVATE ${BM_WARNINGS})
  add_test(NAME test_world COMMAND test_world ${CMAKE_CURRENT_BINARY_DIR}/test-output/world)
  add_executable(test_data tests/test_data.cpp)
  target_link_libraries(test_data PRIVATE bm_cooker blutmond)
  target_include_directories(test_data PRIVATE tests)
  target_compile_options(test_data PRIVATE ${BM_WARNINGS})
  add_test(NAME test_data COMMAND test_data ${CMAKE_CURRENT_BINARY_DIR}/test-output/data)
  add_executable(test_script tests/test_script.cpp)
  target_link_libraries(test_script PRIVATE bm_script)
  target_include_directories(test_script PRIVATE tests)
  target_compile_options(test_script PRIVATE ${BM_WARNINGS})
  # Where shared objects can be made and loaded, the script tests also run as native code:
  # the C the compiler writes is built with the host's C compiler.
  if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    target_compile_definitions(test_script PRIVATE "BM_TEST_CC=\"${CMAKE_C_COMPILER}\""
      "BM_TEST_SCRIPT_INCLUDE=\"${PROJECT_SOURCE_DIR}/src/script\"" "BM_TEST_CORE_INCLUDE=\"${PROJECT_SOURCE_DIR}/src/core/public\"")
    target_link_libraries(test_script PRIVATE ${CMAKE_DL_LIBS})
  endif()
  add_test(NAME test_script COMMAND test_script)
  add_executable(test_gdb_step tests/test_gdb_step.c tools/bmlink/gdb_bridge.c)
  target_link_libraries(test_gdb_step PRIVATE bm_link_host)
  target_include_directories(test_gdb_step PRIVATE tests tools/bmlink)
  target_compile_definitions(test_gdb_step PRIVATE _GNU_SOURCE)
  target_compile_options(test_gdb_step PRIVATE ${BM_WARNINGS})
  add_test(NAME test_gdb_step COMMAND test_gdb_step)
  add_executable(test_multiboot tests/test_multiboot.c)
  target_link_libraries(test_multiboot PRIVATE blutmond)
  target_include_directories(test_multiboot PRIVATE tests)
  target_compile_options(test_multiboot PRIVATE ${BM_WARNINGS})
  add_test(NAME test_multiboot COMMAND test_multiboot)
  add_executable(test_transfer tests/test_transfer.c)
  target_link_libraries(test_transfer PRIVATE blutmond)
  target_include_directories(test_transfer PRIVATE tests)
  target_compile_options(test_transfer PRIVATE ${BM_WARNINGS})
  add_test(NAME test_transfer COMMAND test_transfer)
  # With an ARM compiler around: the cooker's native modules through the real tool chain.
  find_program(BM_ARM_GCC arm-none-eabi-gcc)
  if(BM_ARM_GCC)
    add_executable(test_native_pack tests/test_native_pack.cpp)
    target_link_libraries(test_native_pack PRIVATE bm_cooker)
    target_include_directories(test_native_pack PRIVATE tests)
    target_compile_options(test_native_pack PRIVATE ${BM_WARNINGS})
    target_compile_definitions(test_native_pack PRIVATE "BM_TEST_ARM_CC=\"${BM_ARM_GCC}\"")
    add_test(NAME test_native_pack COMMAND test_native_pack)
  endif()
  add_executable(test_sound tests/test_sound.cpp)
  target_link_libraries(test_sound PRIVATE bm_cooker)
  target_include_directories(test_sound PRIVATE tests)
  target_compile_options(test_sound PRIVATE ${BM_WARNINGS})
  add_test(NAME test_sound COMMAND test_sound ${PROJECT_SOURCE_DIR}/assets)
  add_executable(test_sprite tests/test_sprite.cpp)
  target_link_libraries(test_sprite PRIVATE bm_editor_model)
  target_include_directories(test_sprite PRIVATE tests)
  target_compile_options(test_sprite PRIVATE ${BM_WARNINGS})
  add_test(NAME test_sprite COMMAND test_sprite ${PROJECT_SOURCE_DIR}/assets ${CMAKE_CURRENT_BINARY_DIR}/test-output/sprite)
  add_executable(test_data_file tests/test_data_file.cpp)
  target_link_libraries(test_data_file PRIVATE bm_editor_model)
  target_include_directories(test_data_file PRIVATE tests)
  target_compile_options(test_data_file PRIVATE ${BM_WARNINGS})
  add_test(NAME test_data_file COMMAND test_data_file)
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

  # The test level, and its base (what the native-script images play, see BM_TEST_LEVEL_BASE_ASSETS).
  add_test(NAME cook_test_level
    COMMAND bmcook --profile gba -o ${cooked}/test_level_gba.bmpk ${BM_TEST_LEVEL_ROOMS} ${BM_TEST_LEVEL_ASSETS})
  set_tests_properties(cook_test_level PROPERTIES FIXTURES_SETUP test_level_pack)
  # The game's built-in font (apps/game/builtin_font.h) is the engine font cooked on its own:
  # it must stay what the cooker makes of it.
  add_test(NAME cook_builtin_font
    COMMAND bmcook --profile gba -o ${cooked}/builtin_font_gba.bmpk ${PROJECT_SOURCE_DIR}/assets/engine/font.bmfont)
  set_tests_properties(cook_builtin_font PROPERTIES FIXTURES_SETUP builtin_font_pack)
  add_test(NAME builtin_font_header_is_current
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/embed_pack_header.py ${cooked}/builtin_font_gba.bmpk
            ${PROJECT_SOURCE_DIR}/apps/game/builtin_font.h bm_builtin_font_pack --check)
  set_tests_properties(builtin_font_header_is_current PROPERTIES FIXTURES_REQUIRED builtin_font_pack)
  add_test(NAME cook_test_level_base
    COMMAND bmcook --profile gba -o ${cooked}/test_level_base_gba.bmpk ${BM_TEST_LEVEL_BASE_ROOMS} ${BM_TEST_LEVEL_BASE_ASSETS})
  set_tests_properties(cook_test_level_base PROPERTIES FIXTURES_SETUP test_level_base_pack)
  add_executable(test_game tests/test_game.c apps/game/autopilot.c apps/game/game_live.c)
  target_link_libraries(test_game PRIVATE blutmond)
  target_include_directories(test_game PRIVATE apps/game)
  target_compile_definitions(test_game PRIVATE "BM_TEST_DATA_DIR=\"${PROJECT_SOURCE_DIR}/tests/data\"")
  target_compile_options(test_game PRIVATE ${BM_WARNINGS})
  # The room as an owner of attachments: the second room with `attach = Wind`.
  add_test(NAME make_wind_room
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/set_room_properties.py
      ${PROJECT_SOURCE_DIR}/assets/test_room/room2.tmx ${cooked}/room2_wind.tmx "attach=Wind" "Wind.Strength=1.0")
  set_tests_properties(make_wind_room PROPERTIES FIXTURES_SETUP wind_room)
  add_test(NAME cook_test_level_wind
    COMMAND bmcook --profile gba -o ${cooked}/test_level_wind_gba.bmpk
      ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx ${cooked}/room2_wind.tmx ${BM_TEST_LEVEL_BASE_ASSETS} ${PROJECT_SOURCE_DIR}/tests/scripts/wind.ms ${PROJECT_SOURCE_DIR}/tests/scripts/capable.ms ${PROJECT_SOURCE_DIR}/tests/scripts/shield.ms
      ${PROJECT_SOURCE_DIR}/tests/scripts/knight.ms ${PROJECT_SOURCE_DIR}/tests/scripts/knight.bmsprite ${PROJECT_SOURCE_DIR}/tests/scripts/heavy.ms ${PROJECT_SOURCE_DIR}/tests/scripts/drill.ms ${PROJECT_SOURCE_DIR}/tests/scripts/knife.ms ${PROJECT_SOURCE_DIR}/tests/scripts/thrower.ms ${PROJECT_SOURCE_DIR}/tests/scripts/bolt.ms ${PROJECT_SOURCE_DIR}/tests/scripts/turret.ms ${PROJECT_SOURCE_DIR}/tests/scripts/parry.ms
      ${PROJECT_SOURCE_DIR}/tests/scripts/jab.ms ${PROJECT_SOURCE_DIR}/tests/scripts/painter.ms
      ${PROJECT_SOURCE_DIR}/tests/scripts/aimer.ms ${PROJECT_SOURCE_DIR}/tests/scripts/spark_shot.ms ${PROJECT_SOURCE_DIR}/tests/scripts/lock_on.ms
      ${PROJECT_SOURCE_DIR}/tests/scripts/circler.ms)
  set_tests_properties(cook_test_level_wind PROPERTIES FIXTURES_REQUIRED wind_room FIXTURES_SETUP test_level_wind)
  # The game's own pack: with the streamed torch hall.
  add_test(NAME test_game COMMAND test_game ${CMAKE_BINARY_DIR}/test_level_gba.bmpk ${cooked}/test_level_wind_gba.bmpk ${cooked})
  set_tests_properties(test_game PROPERTIES FIXTURES_REQUIRED "test_level_pack;test_level_wind")
  # Live reload: a variant of the level with one background tile changed.
  add_test(NAME make_room_variant
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/make_room_variant.py
      ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx ${cooked}/room_variant.tmx)
  set_tests_properties(make_room_variant PROPERTIES FIXTURES_SETUP room_variant_tmx)
  set(variant_rooms ${BM_TEST_LEVEL_ROOMS})
  list(REMOVE_ITEM variant_rooms ${PROJECT_SOURCE_DIR}/assets/test_room/room.tmx)
  add_test(NAME cook_test_level_variant
    COMMAND bmcook --profile gba -o ${cooked}/test_level_variant_gba.bmpk ${cooked}/room_variant.tmx ${variant_rooms} ${BM_TEST_LEVEL_ASSETS})
  set_tests_properties(cook_test_level_variant PROPERTIES FIXTURES_REQUIRED room_variant_tmx FIXTURES_SETUP test_level_variant)
  add_executable(test_live tests/test_live.c apps/game/autopilot.c apps/game/game_live.c)
  target_link_libraries(test_live PRIVATE blutmond bm_link_host)
  target_include_directories(test_live PRIVATE apps/game)
  target_compile_options(test_live PRIVATE ${BM_WARNINGS})
  # The same level with a changed script (the door stays shut), for pushing scripts live.
  add_test(NAME make_script_variant
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/make_text_variant.py
      ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/door.ms ${cooked}/door.ms "enter_door();" "hide();")
  set_tests_properties(make_script_variant PROPERTIES FIXTURES_SETUP script_variant_source)
  set(script_assets ${BM_TEST_LEVEL_ASSETS})
  list(REMOVE_ITEM script_assets ${PROJECT_SOURCE_DIR}/assets/test_room/scripts/door.ms)
  add_test(NAME cook_test_level_scripts
    COMMAND bmcook --profile gba -o ${cooked}/test_level_scripts_gba.bmpk ${BM_TEST_LEVEL_ROOMS} ${script_assets} ${cooked}/door.ms)
  set_tests_properties(cook_test_level_scripts PROPERTIES FIXTURES_REQUIRED script_variant_source FIXTURES_SETUP test_level_scripts)
  add_test(NAME test_live COMMAND test_live ${cooked}/test_level_gba.bmpk ${cooked}/test_level_variant_gba.bmpk
    ${cooked}/test_level_scripts_gba.bmpk)
  set_tests_properties(test_live PROPERTIES FIXTURES_REQUIRED "test_level_pack;test_level_variant;test_level_scripts")
  add_test(NAME game_autoplay_host COMMAND game ${cooked}/test_level_gba.bmpk --autoplay --frames 1000
    --sound-check ${cooked}/sound_host.txt ${cooked}/game_host.ppm)
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
    # The Data tab with a line of dialogue (its speaker's portrait in the preview), and the
    # context menu of the boss in the boss hall.
    add_test(NAME editor_data_tab
      COMMAND bmeditor --frames 3 --screenshot ${cooked}/editor_data.ppm --record VesperWakes ${PROJECT_SOURCE_DIR}/assets/test.bmproj)
    add_test(NAME editor_object_menu
      COMMAND bmeditor --frames 3 --screenshot ${cooked}/editor_menu.ppm --room 2 --object-menu 2 ${PROJECT_SOURCE_DIR}/assets/test.bmproj)
    set_tests_properties(editor_data_tab editor_object_menu PROPERTIES FAIL_REGULAR_EXPRESSION "no record|no object"
      ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;EGL_PLATFORM=surfaceless;GALLIUM_DRIVER=llvmpipe;ASAN_OPTIONS=detect_leaks=0")
    # The World tab: the test level's rooms on the map (the layout loads, the world file is read).
    add_test(NAME editor_world_tab
      COMMAND bmeditor --frames 3 --screenshot ${cooked}/editor_world.ppm --tab 6 ${PROJECT_SOURCE_DIR}/assets/test.bmproj)
    set_tests_properties(editor_world_tab PROPERTIES FAIL_REGULAR_EXPRESSION "layout:|not a world file|world version"
      ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;EGL_PLATFORM=surfaceless;GALLIUM_DRIVER=llvmpipe;ASAN_OPTIONS=detect_leaks=0")
    # The figure view (layered-sprites.md §8): the player's figure composed in the Animation
    # tab, legs and torso joined at the waist point; and the torso's sprite with its points.
    add_test(NAME editor_figure_view
      COMMAND bmeditor --frames 30 --screenshot ${cooked}/editor_figure.ppm --open ${PROJECT_SOURCE_DIR}/assets/test_room/player.bmfigure
        ${PROJECT_SOURCE_DIR}/assets/test.bmproj)
    add_test(NAME editor_sprite_points
      COMMAND bmeditor --frames 30 --screenshot ${cooked}/editor_points.ppm --open ${PROJECT_SOURCE_DIR}/assets/test_room/player_legs.bmsprite
        ${PROJECT_SOURCE_DIR}/assets/test.bmproj)
    set_tests_properties(editor_figure_view editor_sprite_points PROPERTIES FAIL_REGULAR_EXPRESSION "layout:|cannot open|no such sprite"
      ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen;EGL_PLATFORM=surfaceless;GALLIUM_DRIVER=llvmpipe;ASAN_OPTIONS=detect_leaks=0")
  endif()

  add_test(NAME no_hardware_addresses
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/check_hw_addresses.py ${PROJECT_SOURCE_DIR})

  # mGBA runner (optional). With libmgba installed (Ubuntu: libmgba-dev) the host build gets
  # bm_mgba_run, which runs GBA images headless. Point BM_GBA_BUILD_DIR at a GBA build
  # directory to test its images:  cmake -S . -B build -DBM_GBA_BUILD_DIR=$PWD/build-gba
  if(BM_HAVE_MGBA)
    add_executable(bm_mgba_run tools/bm_mgba_run.c src/ppu/ppu.c)
    target_include_directories(bm_mgba_run PRIVATE ${MGBA_INCLUDE_DIR} src/ppu tools)
    target_link_libraries(bm_mgba_run PRIVATE ${MGBA_LIBRARY})
    # The libmgba headers need POSIX definitions such as PATH_MAX.
    target_compile_definitions(bm_mgba_run PRIVATE _GNU_SOURCE)

    add_executable(bmlink tools/bmlink/bmlink.c tools/bmlink/xboo.c tools/bmlink/tcp_link.c tools/bmlink/gdb_bridge.c tools/bmlink/mgba_link.c)
    target_include_directories(bmlink PRIVATE ${MGBA_INCLUDE_DIR} tools tools/bmlink)
    target_link_libraries(bmlink PRIVATE bm_link_host ${MGBA_LIBRARY})
    target_compile_definitions(bmlink PRIVATE _GNU_SOURCE BM_HAVE_MGBA)
    target_compile_options(bmlink PRIVATE -Wall -Wextra -Werror)

    # Two GBAs joined by a link cable.
    add_executable(bm_mgba_pair tools/bm_mgba_pair.c tools/bmlink/mgba_link.c)
    target_include_directories(bm_mgba_pair PRIVATE ${MGBA_INCLUDE_DIR} tools tools/bmlink)
    target_link_libraries(bm_mgba_pair PRIVATE bm_link_host ${MGBA_LIBRARY})
    target_compile_definitions(bm_mgba_pair PRIVATE _GNU_SOURCE BM_HAVE_MGBA)
    target_compile_options(bm_mgba_pair PRIVATE -Wall -Wextra -Werror)

    set(BM_GBA_BUILD_DIR "" CACHE PATH "GBA build directory whose images the tests run in mGBA")
    if(BM_GBA_BUILD_DIR)
      set(out ${CMAKE_CURRENT_BINARY_DIR}/test-output)
      file(MAKE_DIRECTORY ${out})
      set(compare ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/compare_images.py)

      foreach(image backdrop backdrop_mb)
        add_test(NAME mgba_${image}
          COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/${image}.gba --frames 90 --expect-backdrop)
      endforeach()

      # Export: the game's cartridge image with a pack appended plays that pack in mGBA.
      add_test(NAME export_mgba COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/check_export.py
        $<TARGET_FILE:bmexport> $<TARGET_FILE:bm_mgba_run> ${BM_GBA_BUILD_DIR}/game.gba ${BM_GBA_BUILD_DIR}/game.elf
        ${BM_GBA_BUILD_DIR}/test_level_gba.bmpk ${out}/export.gba)

      # Golden image: the test scene rendered by the software PPU on the host must match
      # the GBA image of the same scene running in mGBA, pixel for pixel.
      add_test(NAME scene_render_host COMMAND scene_test ${out}/scene_host.ppm)
      set_tests_properties(scene_render_host PROPERTIES FIXTURES_SETUP scene_host)
      add_test(NAME scene_effects_render_host COMMAND scene_test --effects ${out}/scene_effects_host.ppm)
      set_tests_properties(scene_effects_render_host PROPERTIES FIXTURES_SETUP scene_effects_host)
      foreach(image scene_effects scene_effects_mb)
        add_test(NAME scene_render_${image}
          COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/${image}.gba --frames 60 --ppm ${out}/${image}.ppm)
        set_tests_properties(scene_render_${image} PROPERTIES FIXTURES_SETUP scene_${image})
        add_test(NAME scene_matches_${image}
          COMMAND ${compare} ${out}/scene_effects_host.ppm ${out}/${image}.ppm --tolerance 1 --diff ${out}/${image}_diff.ppm)
        set_tests_properties(scene_matches_${image} PROPERTIES FIXTURES_REQUIRED "scene_effects_host;scene_${image}")
      endforeach()
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
        # On the GBA too: no deaths, one room change, the same sound as on the host (checksum
        # over every sample mixed), and few frames missed. From ROM: at most 5 (1 when the sword
        # is picked up and equipped, 2 for the snapshot written at frame 300, 2 while the new room
        # is loaded). The multiboot image runs everything from slow RAM (it is for
        # development): there the room load takes 3, the save point 1, the snapshot these images
        # write at game frame 300 for the state tests 3 (it is over 2 KB, written field by
        # field), and with music playing a busy frame can go over once more, so 8 are allowed.
        set(elf ${BM_GBA_BUILD_DIR}/${image}.elf)
        set(lag 5)
        if(image STREQUAL "game_autoplay_mb")
          set(lag 8)
        endif()
        add_test(NAME game_render_${image}
          COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/${image}.gba --frames 1300 --until ${elf}:bm_game_stats+24=1000
            --check-file ${elf}:bm_game_stats+32 ${cooked}/sound_host.txt
            --ppm ${out}/${image}.ppm --check ${elf}:bm_game_stats+8=0 --check ${elf}:bm_game_stats+12=1 --check "${elf}:bm_game_stats+4<=${lag}")
        set_tests_properties(game_render_${image} PROPERTIES FIXTURES_REQUIRED game_host FIXTURES_SETUP game_${image})
        add_test(NAME game_matches_${image}
          COMMAND ${compare} ${out}/game_host.ppm ${out}/${image}.ppm --tolerance 1 --diff ${out}/${image}_diff.ppm)
        set_tests_properties(game_matches_${image} PROPERTIES FIXTURES_REQUIRED "game_host;game_${image}")
      endforeach()

      # The whole test level at game frame 4100: the autopilot has fought the bat, gone through
      # the second room into the boss hall, pressed A through the moon bat's words, defeated it,
      # taken the Iron Helm it left and put it on in the Equipment menu (it does not dodge: it may
      # die in the fight and start again in the hall). The GBA shows the same picture as the PC,
      # with no script error and two room changes. Frames missed on
      # the GBA: the 5 of game_autoplay above, 2 entering the hall, 2 for the first words, about
      # 5 in the menus (interpreted scripts drawing them), so at most 16.
      add_test(NAME boss_host
        COMMAND game ${cooked}/test_level_gba.bmpk --autoplay --frames 4100 ${out}/boss_host.ppm)
      set_tests_properties(boss_host PROPERTIES FIXTURES_REQUIRED test_level_pack FIXTURES_SETUP boss_host)
      set(elf ${BM_GBA_BUILD_DIR}/game_autoplay.elf)
      add_test(NAME boss_gba
        COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/game_autoplay.gba --frames 4700 --until ${elf}:bm_game_stats+24=4100
          --ppm ${out}/boss_gba.ppm --print ${elf}:bm_game_stats+8 --check ${elf}:bm_game_stats+12=2
          --check ${elf}:bm_game_stats+56=0 --check "${elf}:bm_game_stats+4<=16")
      set_tests_properties(boss_gba PROPERTIES FIXTURES_SETUP boss_gba)
      add_test(NAME boss_matches
        COMMAND ${compare} ${out}/boss_host.ppm ${out}/boss_gba.ppm --tolerance 1 --diff ${out}/boss_diff.ppm)
      set_tests_properties(boss_matches PROPERTIES FIXTURES_REQUIRED "boss_host;boss_gba")

      # The debug overlay (the UI layer's first user) at game frame 1000: the same on the GBA as
      # on the PC, but for the frame time, which only the GBA measures (its digits are left out).
      add_test(NAME overlay_host
        COMMAND game ${cooked}/test_level_gba.bmpk --autoplay --overlay --frames 1000 ${out}/overlay_host.ppm)
      set_tests_properties(overlay_host PROPERTIES FIXTURES_REQUIRED test_level_pack FIXTURES_SETUP overlay_host)
      add_test(NAME overlay_gba
        COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/game_overlay.gba --frames 1300 --until ${BM_GBA_BUILD_DIR}/game_overlay.elf:bm_game_stats+24=1000
          --ppm ${out}/overlay_gba.ppm --check ${BM_GBA_BUILD_DIR}/game_overlay.elf:bm_game_stats+8=0)
      set_tests_properties(overlay_gba PROPERTIES FIXTURES_SETUP overlay_gba)
      add_test(NAME overlay_matches
        COMMAND ${compare} ${out}/overlay_host.ppm ${out}/overlay_gba.ppm --tolerance 1 --ignore 184 24 207 31 --diff ${out}/overlay_diff.ppm)
      set_tests_properties(overlay_matches PROPERTIES FIXTURES_REQUIRED "overlay_host;overlay_gba")

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
        COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/game_autoplay.gba --frames 1300 --until ${elf}:bm_game_stats+24=1000
          --poke 20 ${elf}:bm_state_inbox ${out}/state_host_300.bmst
          --ppm ${out}/state_host_to_gba.ppm --check ${elf}:bm_game_stats+8=0 --check ${elf}:bm_game_stats+12=1
          --check ${elf}:bm_game_stats+20=1 --print ${elf}:bm_game_stats+24)
      set_tests_properties(state_host_to_gba PROPERTIES FIXTURES_REQUIRED state_host FIXTURES_SETUP state_host_to_gba)
      add_test(NAME state_host_to_gba_matches
        COMMAND ${compare} ${out}/game_host.ppm ${out}/state_host_to_gba.ppm --tolerance 1)
      set_tests_properties(state_host_to_gba_matches PROPERTIES FIXTURES_REQUIRED "game_host;state_host_to_gba")

      # Scripts as native code (docs/spec/mondskript.md section 12): the level cooked with a
      # native module plays to the same picture and, at game frame 300, the same state byte for
      # byte as the host's interpreter; the functions really ran as native code; and a pack
      # without native code pushed over the link is interpreted again.
      foreach(image game_autoplay_native game_autoplay_native_mb)
        set(elf ${BM_GBA_BUILD_DIR}/${image}.elf)
        set(lag 4)
        if(image STREQUAL "game_autoplay_native_mb")
          set(lag 7)   # as game_autoplay_mb above
        endif()
        add_test(NAME native_render_${image}
          COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/${image}.gba --frames 1300 --until ${elf}:bm_game_stats+24=1000
            --ppm ${out}/${image}.ppm --check ${elf}:bm_game_stats+8=0 --check ${elf}:bm_game_stats+12=1 --check "${elf}:bm_game_stats+4<=${lag}"
            --check ${elf}:bm_game_stats+56=0 --print ${elf}:bm_game_stats+48 --print ${elf}:bm_game_stats+52)
        set_tests_properties(native_render_${image} PROPERTIES FIXTURES_REQUIRED game_host FIXTURES_SETUP native_${image}
          PASS_REGULAR_EXPRESSION "bm_game_stats\\+48 = [1-9][0-9][0-9]")
        add_test(NAME native_matches_${image}
          COMMAND ${compare} ${out}/game_host.ppm ${out}/${image}.ppm --tolerance 1 --diff ${out}/${image}_diff.ppm)
        set_tests_properties(native_matches_${image} PROPERTIES FIXTURES_REQUIRED "game_host;native_${image}")
      endforeach()
      set(elf ${BM_GBA_BUILD_DIR}/game_autoplay_native.elf)
      add_test(NAME native_state_snapshot
        COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/game_autoplay_native.gba --frames 400 --dump ${elf}:bm_state_outbox ${out}/state_native_300.bmst)
      set_tests_properties(native_state_snapshot PROPERTIES FIXTURES_SETUP state_native)
      # It plays the level's base (native code is only in that pack): the PC plays the same.
      add_test(NAME state_snapshot_host_base
        COMMAND game ${cooked}/test_level_base_gba.bmpk --autoplay --frames 400 --snapshot-at 300 ${out}/state_host_base_300.bmst)
      set_tests_properties(state_snapshot_host_base PROPERTIES FIXTURES_REQUIRED test_level_base_pack FIXTURES_SETUP state_host_base)
      add_test(NAME native_state_identical
        COMMAND ${CMAKE_COMMAND} -E compare_files ${out}/state_host_base_300.bmst ${out}/state_native_300.bmst)
      set_tests_properties(native_state_identical PROPERTIES FIXTURES_REQUIRED "state_host_base;state_native")
      add_test(NAME native_live_push_interprets
        COMMAND bmlink --mgba ${BM_GBA_BUILD_DIR}/game_autoplay_native.gba --errors 2000 2000 run 30 hello
          push ${cooked}/test_level_variant_gba.bmpk hot run 1300
          check ${elf}:bm_game_stats+28=1 check ${elf}:bm_game_stats+8=0 check ${elf}:bm_game_stats+12=2 check ${elf}:bm_game_stats+56=0 stats)
      set_tests_properties(native_live_push_interprets PROPERTIES FIXTURES_REQUIRED test_level_variant)
      # The debugger (docs/spec/link-protocol.md section 8): a session in GDB's remote protocol
      # against the game in mGBA: stop, a breakpoint on a script line found through the native
      # module's debug information, registers, memory, single steps, Ctrl-C, detach; then the
      # game plays on to its end.
      find_package(Python3 COMPONENTS Interpreter)
      find_program(BM_ARM_READELF arm-none-eabi-readelf)
      if(Python3_FOUND AND BM_ARM_READELF)
        add_test(NAME gdb_session_script_breakpoint
          COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/gdb_session.py $<TARGET_FILE:bmlink>
            ${BM_GBA_BUILD_DIR}/game_autoplay_native.gba ${BM_GBA_BUILD_DIR}/game_autoplay_native.elf
            ${BM_GBA_BUILD_DIR}/test_level_native.elf assets/test_room/scripts/bat.ms 27)
        set_tests_properties(gdb_session_script_breakpoint PROPERTIES TIMEOUT 120)
      endif()
      # GBA to GBA (docs/spec/link-protocol.md section 9), two emulated GBAs joined by a cable.
      # The autoplaying game, in the second room at frame 800, brings the other GBA to where it
      # is: a GBA running the game from a cartridge (it has the same pack already and gets only
      # the state: flags +72 = 0, one transfer applied), and a GBA that runs nothing but the
      # bootstrap (it gets the runtime through it first). The runtime carries a trimmed pack
      # (multiboot), so the sender pushes its own, fitted around the room it is in (flags +72 =
      # 6, two transfers applied: the pack and the state).
      # The other side then reports the sender's room and position; the sender plays on.
      set(send ${BM_GBA_BUILD_DIR}/game_send.elf:bm_game_stats)
      # The receiving GBA also carries the sender's inventory afterwards (the coin and the Short
      # Sword the autopilot took in the first room): bm_game_stats+92 counts the kinds of things
      # carried, +96 is the CRC of the inventory's keys and amounts.
      add_test(NAME gba_to_gba_state
        COMMAND bm_mgba_pair ${BM_GBA_BUILD_DIR}/game_send.gba ${BM_GBA_BUILD_DIR}/game.gba --frames 1500 --until a ${send}+24=1000
          --check a ${send}+60=1 --check a ${send}+76=1 --check a ${send}+72=0 --check a ${send}+8=0 --check a ${send}+12=1
          --check a ${send}+64 --check a ${send}+68
          --check b ${BM_GBA_BUILD_DIR}/game.elf:bm_game_stats+16=1 --check b ${BM_GBA_BUILD_DIR}/game.elf:bm_game_stats+28=1
          --check b ${BM_GBA_BUILD_DIR}/game.elf:bm_game_stats+56=0 --check b ${BM_GBA_BUILD_DIR}/game.elf:bm_game_stats+92=2
          --check b ${BM_GBA_BUILD_DIR}/game.elf:bm_game_stats+96=2982369528)
      add_test(NAME gba_to_gba_boot
        COMMAND bm_mgba_pair ${BM_GBA_BUILD_DIR}/game_send.gba ${BM_GBA_BUILD_DIR}/bootstrap_mb.gba --frames 1800 --until a ${send}+24=1000
          --check a ${send}+60=1 --check a ${send}+76=1 --check a ${send}+72=6 --check a ${send}+8=0 --check a ${send}+12=1
          --check a ${send}+64 --check a ${send}+80 --check a ${send}+68
          --check b ${BM_GBA_BUILD_DIR}/game_mb.elf:bm_game_stats+16=1 --check b ${BM_GBA_BUILD_DIR}/game_mb.elf:bm_game_stats+28=2
          --check b ${BM_GBA_BUILD_DIR}/game_mb.elf:bm_game_stats+56=0 --check b ${BM_GBA_BUILD_DIR}/game_mb.elf:bm_game_stats+92=2
          --check b ${BM_GBA_BUILD_DIR}/game_mb.elf:bm_game_stats+96=2982369528)

      # The PC game plays along with the GBA (docs/spec/link-protocol.md section 10): the GBA's
      # keys for each frame, mirrored by bmlink; the states are compared every 64 frames.
      if(Python3_FOUND)
        add_test(NAME input_mirror_gba_to_pc
          COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/mirror_session.py from $<TARGET_FILE:bmlink> $<TARGET_FILE:game>
            ${cooked}/test_level_gba.bmpk ${BM_GBA_BUILD_DIR}/game_autoplay.gba ${BM_GBA_BUILD_DIR}/game_autoplay.elf)
        add_test(NAME input_mirror_pc_to_gba
          COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tests/mirror_session.py to $<TARGET_FILE:bmlink> $<TARGET_FILE:game>
            ${cooked}/test_level_gba.bmpk ${BM_GBA_BUILD_DIR}/game.gba ${BM_GBA_BUILD_DIR}/game.elf)
        set_tests_properties(input_mirror_gba_to_pc input_mirror_pc_to_gba PROPERTIES FIXTURES_REQUIRED test_level_pack TIMEOUT 240)
      endif()

      # Native code takes at most 60 % of the interpreter's time for the benchmark script (from
      # ROM about 35 %), with the same result.
      foreach(image script_bench script_bench_mb)
        set(elf ${BM_GBA_BUILD_DIR}/${image}.elf)
        add_test(NAME native_speed_${image}
          COMMAND bm_mgba_run ${BM_GBA_BUILD_DIR}/${image}.gba --frames 120 --check ${elf}:bm_bench+0=1 --check ${elf}:bm_bench+12=1
            --check ${elf}:bm_bench+20=0 --check "${elf}:bm_bench+28<=60" --print ${elf}:bm_bench+4 --print ${elf}:bm_bench+8 --print ${elf}:bm_bench+28)
      endforeach()

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
      # The light (docs/plans/lighting.md) on the GBA: in both torch halls a frame's light stays
      # within its budget in processor cycles (the room's banks, the sprites' banks), from ROM.
      foreach(hall torch_hall torch_hall_layer torch_hall_fitted torch_hall_normals torch_hall_stream)
        add_test(NAME light_mgba_${hall}
          COMMAND bmlink --mgba ${BM_GBA_BUILD_DIR}/game.gba run 60 push ${CMAKE_BINARY_DIR}/test_level_gba.bmpk restart:${hall} run 120
            check "${elf}:bm_light_cycles+0<=40000" check "${elf}:bm_light_cycles+4<=20000" screenshot ${out}/light_${hall}.ppm)
        set_tests_properties(light_mgba_${hall} PROPERTIES FIXTURES_REQUIRED test_level_pack)
      endforeach()
      add_test(NAME live_mgba_hot_changes_one_tile
        COMMAND ${compare} ${out}/live_before.ppm ${out}/live_after.ppm --changed-in 0 0 15 15
          --ignore 32 96 47 127)  # the player's idle animation goes on meanwhile
      set_tests_properties(live_mgba_hot_changes_one_tile PROPERTIES FIXTURES_REQUIRED live_mgba_hot)
      # The video state read over the link (PPU_SNAPSHOT), rendered by the software PPU, is
      # what mGBA shows; with bit errors none of its 100 packets may be lost.
      add_test(NAME live_mgba_ppu_snapshot
        COMMAND bmlink --mgba ${BM_GBA_BUILD_DIR}/game.gba --errors 1000 1000 run 120 ppu-snapshot ${out}/live_snapshot.bmps
          screenshot ${out}/live_snapshot_mgba.ppm stats)
      set_tests_properties(live_mgba_ppu_snapshot PROPERTIES FIXTURES_SETUP live_mgba_ppu_snapshot)
      add_test(NAME live_mgba_ppu_snapshot_render COMMAND bm_ppu_render ${out}/live_snapshot.bmps ${out}/live_snapshot_ppu.ppm)
      set_tests_properties(live_mgba_ppu_snapshot_render PROPERTIES FIXTURES_REQUIRED live_mgba_ppu_snapshot FIXTURES_SETUP live_mgba_ppu_render)
      add_test(NAME live_mgba_ppu_snapshot_matches COMMAND ${compare} ${out}/live_snapshot_mgba.ppm ${out}/live_snapshot_ppu.ppm
          --tolerance 1 --ignore 32 96 47 127)  # the screenshot is a few frames later: the player's idle animation
      set_tests_properties(live_mgba_ppu_snapshot_matches PROPERTIES FIXTURES_REQUIRED live_mgba_ppu_render)
      # The runtime image (no pack of its own, what the editor boots): it waits with the
      # loading box, then runs the pack pushed to it.
      set(elf ${BM_GBA_BUILD_DIR}/game_runtime_mb.elf)
      add_test(NAME live_mgba_runtime_image
        COMMAND bmlink --mgba ${BM_GBA_BUILD_DIR}/game_runtime_mb.gba run 40 screenshot ${out}/runtime_waiting.ppm
          push ${cooked}/test_level_gba.bmpk restart run 60 screenshot ${out}/runtime_running.ppm
          check ${elf}:bm_game_stats+28=1 check ${elf}:bm_game_stats+8=0 stats)
      set_tests_properties(live_mgba_runtime_image PROPERTIES FIXTURES_REQUIRED test_level_pack)
      foreach(image game_autoplay game_autoplay_mb)
        set(elf ${BM_GBA_BUILD_DIR}/${image}.elf)
        # The full level, also on a multiboot GBA: the variant does not fit beside the embedded
        # pack there and replaces it (src/link/live.h). Into the boss hall within the run; on a
        # multiboot GBA the pack is fitted around the first room (runtime-abi.md §0): the second
        # room comes with it, the boss hall does not, and the run stays in the second room.
        set(level test_level)
        set(rooms 2)
        if(image MATCHES "_mb$")
          set(rooms 1)
        endif()
        add_test(NAME live_mgba_bit_errors_${image}
          COMMAND bmlink --mgba ${BM_GBA_BUILD_DIR}/${image}.gba --errors 2000 2000 run 30 hello
            push ${cooked}/${level}_variant_gba.bmpk hot
            state-get ${out}/live_${image}.bmst run 30 state-put ${out}/live_${image}.bmst
            push ${cooked}/${level}_gba.bmpk restart:test_room
            push ${cooked}/${level}_variant_gba.bmpk hot run 1500
            check ${elf}:bm_game_stats+28=4 check ${elf}:bm_game_stats+8=0 check ${elf}:bm_game_stats+12=${rooms} stats)
        set_tests_properties(live_mgba_bit_errors_${image} PROPERTIES FIXTURES_REQUIRED "test_level_pack;test_level_variant")
      endforeach()

      # Rooms on demand (runtime-abi.md §0): the multiboot autoplay walks into the boss hall's
      # door, which its pack left out; the game holds still with the box and asks; `serve`
      # answers with the level fitted around the boss hall, pushed hot; the game goes through.
      set(elf ${BM_GBA_BUILD_DIR}/game_autoplay_mb.elf)
      add_test(NAME live_mgba_room_on_demand
        COMMAND bmlink --mgba ${BM_GBA_BUILD_DIR}/game_autoplay_mb.gba run 1400 screenshot ${out}/room_wait.ppm
          check ${elf}:bm_game_stats+12=1 serve ${cooked}/test_level_gba.bmpk 1 100 run 200 screenshot ${out}/room_came.ppm
          check ${elf}:bm_game_stats+12=2 check ${elf}:bm_game_stats+8=0)
      set_tests_properties(live_mgba_room_on_demand PROPERTIES FIXTURES_REQUIRED test_level_pack)

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
