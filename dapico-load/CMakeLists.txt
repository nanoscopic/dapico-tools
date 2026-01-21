cmake_minimum_required(VERSION 3.16)

project(dapico_load LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(dapico-load
    src/main.cpp
    src/elf.cc
)

target_include_directories(dapico-load
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(dapico-load PRIVATE NO_PICO_PLATFORM=1)

target_link_libraries(dapico-load
    PRIVATE
        "-framework CoreFoundation"
        "-framework IOKit"
)

install(TARGETS dapico-load RUNTIME DESTINATION bin)
