cmake_minimum_required(VERSION 3.10)
project(abcparser C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Library
add_library(abc_parser STATIC
    abc_parser.c
    abc_parser.h
)

target_include_directories(abc_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Executable
add_executable(abcparser main.c)
target_link_libraries(abcparser PRIVATE abc_parser)

# Math library on Unix
if(UNIX)
    target_link_libraries(abcparser PRIVATE m)
endif()

# Test executable
add_executable(test_parser test_parser.c)
target_link_libraries(test_parser PRIVATE abc_parser)
if(UNIX)
    target_link_libraries(test_parser PRIVATE m)
endif()

# Enable CTest
enable_testing()
add_test(NAME parser_tests COMMAND test_parser)
