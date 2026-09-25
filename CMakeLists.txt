cmake_minimum_required(VERSION 3.16)

project(gg-editor LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(gg editor.c)

add_executable(editor_tests tests.c)

enable_testing()
add_test(NAME editor_tests COMMAND editor_tests)
