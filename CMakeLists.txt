project(blutmond LANGUAGES C)

file(GLOB_RECURSE SOURCES src/public/*.c src/private/*.c)

add_compile_options(-Wall -Wextra -Werror -pedantic)

add_executable(blutmond ${SOURCES})

target_include_directories(blutmond PRIVATE src/private)
target_include_directories(blutmond PUBLIC src/public)