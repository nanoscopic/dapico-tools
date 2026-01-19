cmake_minimum_required(VERSION 3.16)

project(pico_reboot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

add_executable(pico-reboot
    src/main.cpp
)

target_include_directories(pico-reboot
    PRIVATE
        ${LIBUSB_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(pico-reboot PRIVATE NO_PICO_PLATFORM=1)

target_compile_options(pico-reboot PRIVATE ${LIBUSB_CFLAGS_OTHER})

target_link_directories(pico-reboot PRIVATE ${LIBUSB_LIBRARY_DIRS})

target_link_options(pico-reboot PRIVATE ${LIBUSB_LDFLAGS_OTHER})

target_link_libraries(pico-reboot PRIVATE ${LIBUSB_LIBRARIES})
