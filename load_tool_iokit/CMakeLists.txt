cmake_minimum_required(VERSION 3.16)

project(pico_load_iokit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pico-load-iokit
    src/main.cpp
    ../elf/elf_file.cpp
    ../errors/errors.cpp
)

target_include_directories(pico-load-iokit
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../elf
        ${CMAKE_CURRENT_SOURCE_DIR}/../errors
        ${CMAKE_CURRENT_SOURCE_DIR}/../model
)

target_compile_definitions(pico-load-iokit PRIVATE NO_PICO_PLATFORM=1)

target_link_libraries(pico-load-iokit
    PRIVATE
        "-framework CoreFoundation"
        "-framework IOKit"
)
