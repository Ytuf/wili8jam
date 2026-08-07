cmake_minimum_required(VERSION 3.30)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PICO_PLATFORM rp2350)
set(PICO_BOARD freewili2 CACHE STRING "Board type")
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/wilibsp/bsp/boards)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/wilibsp/bsp/third_party/Pico-PIO-USB")
include(pico_sdk_import.cmake)

project(wili8jam C CXX ASM)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
pico_sdk_init()
add_subdirectory(wilibsp/bsp)
add_subdirectory(wilibsp/libs/onewili)

# --- Lua 5.4.7 static library ---
set(LUA_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lua-5.4.7/src)

file(GLOB LUA_CORE_SOURCES ${LUA_SRC_DIR}/*.c)
# Remove standalone program mains
list(REMOVE_ITEM LUA_CORE_SOURCES
    ${LUA_SRC_DIR}/lua.c
    ${LUA_SRC_DIR}/luac.c
)

add_library(lua54 STATIC ${LUA_CORE_SOURCES})

target_include_directories(lua54 PUBLIC ${LUA_SRC_DIR})

# luaconf overrides for embedded use (luaconf.h has #ifndef guards for these)
target_compile_definitions(lua54 PUBLIC
    LUA_32BITS=1
    LUAI_MAXSTACK=2000
)
target_compile_definitions(lua54 PRIVATE
    LUA_USE_C89
)

# Ensure Lua is built as C
set_target_properties(lua54 PROPERTIES LINKER_LANGUAGE C)

# --- TLSF allocator (vendored) ---
add_library(tlsf STATIC tlsf/tlsf.c)
target_include_directories(tlsf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- Main executable ---
add_executable(wili8jam
    src/main.cpp
    src/newlib_stubs.c
    src/fw2_fs.c
    src/dvi.c
    src/gfx.c
    src/input.c
    src/audio.c
    src/p8_preprocess.c
    src/p8_api.c
    src/p8_sfx.c
    src/p8_cart.c
    src/p8_png.c
    src/p8_console.c
    src/p8_editor.c
)

target_include_directories(wili8jam PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LUA_SRC_DIR}
)

target_link_libraries(wili8jam PRIVATE
    lua54
    tlsf
    onewili_fwgui
    freewili2_bsp
    pico_stdlib
    hardware_pio
    hardware_spi
    hardware_sync
    hardware_dma
    hardware_clocks
    hardware_i2c
)

# Increase stack size: Lua call chains and file adapters need more than 2 KB
target_compile_definitions(wili8jam PRIVATE PICO_STACK_SIZE=0x1000)

# FreeWili diagnostics are RTT-only; text entry comes from the board keyboard.
pico_enable_stdio_usb(wili8jam 0)
pico_enable_stdio_uart(wili8jam 0)

fw2_psram_app(wili8jam
    VERSION 001
    DESCRIPTION "PICO-8 console with DVI, audio, and SD cartridges"
    REPOSITORY "https://github.com/evaderkrub/wili8jam"
)
