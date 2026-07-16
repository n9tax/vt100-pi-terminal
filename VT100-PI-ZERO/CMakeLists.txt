cmake_minimum_required(VERSION 3.13)

project(VT100-PI-ZERO C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(DRM REQUIRED libdrm)

add_executable(vt100-pi-zero
        src/main.c
        src/terminal/vtparse.c
        src/terminal/screen.c
        src/terminal/vt100.c
        src/video/textmode.c
        src/io/serial_linux.c
        src/io/kbd_evdev.c
        src/io/host_link.c
        )

target_include_directories(vt100-pi-zero PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${DRM_INCLUDE_DIRS}
        )
target_compile_options(vt100-pi-zero PRIVATE -Wall -Wextra ${DRM_CFLAGS_OTHER})
target_link_libraries(vt100-pi-zero PRIVATE ${DRM_LIBRARIES})
target_link_directories(vt100-pi-zero PRIVATE ${DRM_LIBRARY_DIRS})
