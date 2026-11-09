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

# The web view's page, compiled in (regenerated whenever web/index.html changes).
set(WEB_INDEX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html)
set(WEB_INDEX_C   ${CMAKE_CURRENT_BINARY_DIR}/web_index.c)
add_custom_command(
        OUTPUT  ${WEB_INDEX_C}
        COMMAND ${CMAKE_COMMAND} -DIN=${WEB_INDEX_SRC} -DOUT=${WEB_INDEX_C}
                -DNAME=web_index_html -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
        DEPENDS ${WEB_INDEX_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
        VERBATIM)

add_executable(vt100-pi-zero
        src/main.c
        src/settings.c
        src/flow.c
        src/autospeed.c
        src/setup.c
        src/service.c
        src/update.c
        src/terminal/vtparse.c
        src/terminal/screen.c
        src/terminal/vt100.c
        src/video/textmode.c
        src/video/glyphs.c
        src/video/fonts.c
        src/video/themes.c
        src/io/serial_linux.c
        src/io/kbd_evdev.c
        src/io/host_link.c
        src/net/telnet.c
        src/net/netlink.c
        src/net/hostconn.c
        src/net/webview.c
        ${WEB_INDEX_C}
        )

target_include_directories(vt100-pi-zero PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${DRM_INCLUDE_DIRS}
        ${FREETYPE_INCLUDE_DIRS}
        )
target_compile_options(vt100-pi-zero PRIVATE -Wall -Wextra ${DRM_CFLAGS_OTHER} ${FREETYPE_CFLAGS_OTHER})
target_link_libraries(vt100-pi-zero PRIVATE ${DRM_LIBRARIES} ${FREETYPE_LIBRARIES})
target_link_directories(vt100-pi-zero PRIVATE ${DRM_LIBRARY_DIRS} ${FREETYPE_LIBRARY_DIRS})

# Compile-in the bundled font locations so the binary finds them when run from
# the build tree; fonts.c/glyphs.c also fall back to install and system dirs.
target_compile_definitions(vt100-pi-zero PRIVATE
        ASSET_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        ASSET_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/DejaVuSansMono.ttf")
