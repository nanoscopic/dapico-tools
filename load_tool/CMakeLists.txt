cmake_minimum_required(VERSION 3.16)

project(pico_load LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

add_executable(pico-load
    src/main.cpp
    ../elf/elf_file.cpp
    ../errors/errors.cpp
)

target_include_directories(pico-load
    PRIVATE
        ${LIBUSB_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../elf
        ${CMAKE_CURRENT_SOURCE_DIR}/../errors
        ${CMAKE_CURRENT_SOURCE_DIR}/../model
)

target_compile_definitions(pico-load PRIVATE NO_PICO_PLATFORM=1)

target_compile_options(pico-load PRIVATE ${LIBUSB_CFLAGS_OTHER})

target_link_directories(pico-load PRIVATE ${LIBUSB_LIBRARY_DIRS})

target_link_options(pico-load PRIVATE ${LIBUSB_LDFLAGS_OTHER})

target_link_libraries(pico-load PRIVATE ${LIBUSB_LIBRARIES})
