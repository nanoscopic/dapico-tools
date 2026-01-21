cmake_minimum_required(VERSION 3.16)

project(pico_reboot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pico-reboot
    src/main.cpp
)

target_include_directories(pico-reboot
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(pico-reboot PRIVATE NO_PICO_PLATFORM=1)

target_link_libraries(pico-reboot
    PRIVATE
        "-framework CoreFoundation"
        "-framework IOKit"
)
