# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(VT100-PI C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Pico-PIO-USB library (provides the PIO USB host implementation TinyUSB uses).
add_subdirectory(lib/Pico-PIO-USB)

# Add executable. Default name is the project name, version 0.1

add_executable(VT100-PI
        VT100-PI.c
        src/video/hstx_dvi.c
        src/video/textmode.c
        src/io/serial.c
        src/io/sys_clock.c
        src/io/kbd_host.c
        src/settings.c
        src/setup.c
        src/fault.c
        src/terminal/screen.c
        src/terminal/vtparse.c
        src/terminal/vt100.c
        src/net/net.c
        src/net/telnet.c
        # PIO-USB host controller driver (not auto-included by tinyusb_host).
        ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
        )

target_compile_definitions(VT100-PI PRIVATE PIO_USB_USE_TINYUSB)

pico_set_program_name(VT100-PI "VT100-PI")
pico_set_program_version(VT100-PI "0.1")

# Native USB is used as a PIO-USB *host* (keyboard), so no USB stdio; UARTs are
# for RS232, so no UART stdio either. Debug output goes to the LCD.
pico_enable_stdio_uart(VT100-PI 0)
pico_enable_stdio_usb(VT100-PI 0)

# Include the project source root (for "config.h", "video/..") and this dir so
# TinyUSB finds tusb_config.h.
target_include_directories(VT100-PI PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(VT100-PI
        pico_stdlib
        hardware_dma
        hardware_uart
        hardware_pll
        hardware_clocks
        hardware_flash
        pico_multicore
        tinyusb_host
        pico_pio_usb
        pico_cyw43_arch_lwip_poll
        )

pico_add_extra_outputs(VT100-PI)

