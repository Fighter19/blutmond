project(blutmond LANGUAGES C)

set(USE_SANITIZER OFF CACHE BOOL "Enable AddressSanitizer")

if(USE_SANITIZER)
    add_compile_options(-fsanitize=address -fsanitize=undefined)
    add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

file(GLOB_RECURSE SOURCES src/public/*.c src/private/*.c)

add_compile_options(-Wall -Wextra -Werror -pedantic)

add_executable(blutmond ${SOURCES})

target_include_directories(blutmond PRIVATE src/private)
target_include_directories(blutmond PUBLIC src/public)