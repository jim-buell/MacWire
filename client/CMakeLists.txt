cmake_minimum_required(VERSION 3.10)
project(MacWire C)

option(RSS_DEMO_TRANSPORT "Use captured backend packets in Mini vMac" OFF)
option(RSS_DEMO_SLOW_DOWNLOADS "Simulate slow, cancellable downloads in the demo build" OFF)
option(TICKERTAPE_UI_SMOKE "Run native Toolbox UI checks and exit (QA only)" OFF)
option(TICKERTAPE_RENDER_ONLY "Stop native QA after reader/feed rendering checks" OFF)
set(TICKERTAPE_PACKET_HEADER "" CACHE FILEPATH "Optional captured-packet fixture header for local QA")

# MacWire — builds a classic 68K Mac application with Retro68.
# Configure with the Retro68 toolchain file, e.g.:
#   mkdir build && cd build
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=/tmp/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
#   make

add_application(MacWire
    CREATOR "RSSR"
    RSSReader.c
    Network.c
    Feeds.c
    Connection.c
    UserActions.c
    WaitCursor.c
    ConnectionConfig.c
    HTTPResponse.c
    MacTCPTransport.c
    UI.c
    Clipboard.c
    DialogSupport.c
    Typography.c
    ReadState.c
    ReadHistory.c
    ListPosition.c
    Footnotes.c
    LinkDisplay.c
    ReaderMarkup.c
    ReaderFont.c
    ReaderUnderline.c
    ReaderViewport.c
    Articles.c
    Protocol.c
    Wire.c
    DemoTransport.c
    shim.c
    RSSReader.r
    TickerTapeIcons.r
    )

if(RSS_DEMO_TRANSPORT)
    target_compile_definitions(MacWire PRIVATE RSS_DEMO_TRANSPORT=1)
endif()
if(RSS_DEMO_SLOW_DOWNLOADS)
    if(NOT RSS_DEMO_TRANSPORT)
        message(FATAL_ERROR "RSS_DEMO_SLOW_DOWNLOADS requires RSS_DEMO_TRANSPORT")
    endif()
    target_compile_definitions(MacWire PRIVATE RSS_DEMO_SLOW_DOWNLOADS=1)
endif()
if(TICKERTAPE_UI_SMOKE)
    target_sources(MacWire PRIVATE tests/ui_smoke.c)
    target_compile_definitions(MacWire PRIVATE TICKERTAPE_UI_SMOKE=1)
endif()
if(TICKERTAPE_RENDER_ONLY)
    target_compile_definitions(MacWire PRIVATE TICKERTAPE_RENDER_ONLY=1)
endif()
if(TICKERTAPE_PACKET_HEADER)
    target_compile_definitions(MacWire PRIVATE TICKERTAPE_PACKET_HEADER="${TICKERTAPE_PACKET_HEADER}")
endif()

# Recreate the floppy after each build. Retro68's --cc output has retained an
# older resource fork during incremental builds; a fresh HFS image round trips
# the current MacBinary forks and is checked before replacing the disk.
add_custom_target(MacWire_HFS ALL
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/package_hfs_disk.sh"
            "${CMAKE_CURRENT_BINARY_DIR}/MacWire.bin"
            "${CMAKE_CURRENT_BINARY_DIR}/MacWire.dsk"
    DEPENDS MacWire_APPL
    VERBATIM)

# Make the result as small as possible by removing unused code and
# stripping macsbug symbol names on 68K (skip this when debugging).
set_target_properties(MacWire PROPERTIES COMPILE_OPTIONS "-ffunction-sections;-Werror=unused-label")
if(CMAKE_SYSTEM_NAME MATCHES Retro68)
    set_target_properties(MacWire PROPERTIES LINK_FLAGS "-Wl,-gc-sections -Wl,--mac-strip-macsbug")
else()
    set_target_properties(MacWire PROPERTIES LINK_FLAGS "-Wl,-gc-sections")
endif()
