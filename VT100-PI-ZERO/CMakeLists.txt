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
pkg_check_modules(FREETYPE REQUIRED freetype2)

add_executable(vt100-pi-zero
        src/main.c
        src/settings.c
        src/setup.c
        src/terminal/vtparse.c
        src/terminal/screen.c
        src/terminal/vt100.c
        src/video/textmode.c
        src/video/glyphs.c
        src/io/serial_linux.c
        src/io/kbd_evdev.c
        src/io/host_link.c
        )

target_include_directories(vt100-pi-zero PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${DRM_INCLUDE_DIRS}
        ${FREETYPE_INCLUDE_DIRS}
        )
target_compile_options(vt100-pi-zero PRIVATE -Wall -Wextra ${DRM_CFLAGS_OTHER} ${FREETYPE_CFLAGS_OTHER})
target_link_libraries(vt100-pi-zero PRIVATE ${DRM_LIBRARIES} ${FREETYPE_LIBRARIES})
target_link_directories(vt100-pi-zero PRIVATE ${DRM_LIBRARY_DIRS} ${FREETYPE_LIBRARY_DIRS})

# Compile-in the path to the bundled TTF so the binary finds it when run from
# the build tree; glyphs.c also falls back to FONT_TTF_PATH and system fonts.
target_compile_definitions(vt100-pi-zero PRIVATE
        ASSET_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/DejaVuSansMono.ttf")
