cmake_minimum_required(VERSION 3.16)

project(dapico_reboot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(dapico-reboot
    src/main.cpp
)

target_include_directories(dapico-reboot
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(dapico-reboot PRIVATE NO_PICO_PLATFORM=1)

target_link_libraries(dapico-reboot
    PRIVATE
        "-framework CoreFoundation"
        "-framework IOKit"
)

install(TARGETS dapico-reboot RUNTIME DESTINATION bin)
