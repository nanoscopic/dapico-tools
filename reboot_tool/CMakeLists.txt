cmake_minimum_required(VERSION 3.16)

project(pico_reboot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(_picotool_arches "${CMAKE_OSX_ARCHITECTURES}")
    if(NOT _picotool_arches)
        set(_picotool_arches "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
    list(FIND _picotool_arches "x86_64" _picotool_has_x86)
    list(FIND _picotool_arches "arm64" _picotool_has_arm)
    if(_picotool_has_x86 GREATER -1 AND _picotool_has_arm EQUAL -1)
        set(_picotool_brew_prefix "/usr/local")
    elseif(_picotool_has_arm GREATER -1 AND _picotool_has_x86 EQUAL -1)
        set(_picotool_brew_prefix "/opt/homebrew")
    elseif(_picotool_has_arm GREATER -1 AND _picotool_has_x86 GREATER -1)
        set(_picotool_pkg_paths "/usr/local/lib/pkgconfig" "/opt/homebrew/lib/pkgconfig")
        list(FILTER _picotool_pkg_paths INCLUDE REGEX "^/.+")
        string(JOIN ":" _picotool_pkg_path "${_picotool_pkg_paths}")
        set(ENV{PKG_CONFIG_PATH} "${_picotool_pkg_path}:$ENV{PKG_CONFIG_PATH}")
    endif()
    if(_picotool_brew_prefix)
        if(EXISTS "${_picotool_brew_prefix}/bin/pkg-config")
            set(PKG_CONFIG_EXECUTABLE "${_picotool_brew_prefix}/bin/pkg-config")
        endif()
        if(EXISTS "${_picotool_brew_prefix}/lib/pkgconfig")
            set(ENV{PKG_CONFIG_PATH} "${_picotool_brew_prefix}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
        endif()
    endif()
endif()

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
