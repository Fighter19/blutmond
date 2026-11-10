# Cross-compiling for the Game Boy Advance with arm-none-eabi-gcc (Ubuntu package
# gcc-arm-none-eabi; devkitPro is not required).
#
#   cmake -S . -B build-gba -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-gba.cmake

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(BM_PLATFORM gba CACHE STRING "Target platform" FORCE)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy CACHE FILEPATH "objcopy")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(BM_GBA_ARCH_FLAGS "-mcpu=arm7tdmi -mtune=arm7tdmi -mthumb -mthumb-interwork")
set(CMAKE_C_FLAGS_INIT "${BM_GBA_ARCH_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_ASM_FLAGS_INIT "-mcpu=arm7tdmi -mthumb-interwork")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${BM_GBA_ARCH_FLAGS} -nostartfiles -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
