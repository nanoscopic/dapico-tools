cmake_minimum_required(VERSION 3.16)

project(pico_load LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pico-load
    src/main.cpp
    src/elf.cc
)

target_include_directories(pico-load
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(pico-load PRIVATE NO_PICO_PLATFORM=1)

target_link_libraries(pico-load
    PRIVATE
        "-framework CoreFoundation"
        "-framework IOKit"
)
