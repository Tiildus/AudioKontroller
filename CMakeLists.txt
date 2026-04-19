cmake_minimum_required(VERSION 3.16)
project(AudioKontroller)

# Default to Release (with -O2) if no build type is specified.
# _FORTIFY_SOURCE requires optimization to be enabled.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE REQUIRED libpulse)
pkg_check_modules(HIDAPI REQUIRED hidapi-libusb)

include_directories(
    ${HIDAPI_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
)

add_executable(audiokontroller
    main.cpp
    PCPanelHandler.cpp PCPanelHandler.h
    AudioHandler.cpp AudioHandler.h
    Overlay.cpp Overlay.h
    FocusMonitor.cpp FocusMonitor.h
    ButtonHandler.cpp ButtonHandler.h
    DiscordIPC.cpp DiscordIPC.h
    ConfigManager.cpp ConfigManager.h
    Logger.cpp Logger.h
    Util.h
)

target_compile_options(audiokontroller PRIVATE
    -Wall -Wextra -fstack-protector-strong -D_FORTIFY_SOURCE=2)
target_link_options(audiokontroller PRIVATE
    -Wl,-z,relro -Wl,-z,now)

target_link_libraries(audiokontroller
    Qt6::Core
    Qt6::DBus
    Qt6::Network
    ${HIDAPI_LIBRARIES}
    ${PULSE_LIBRARIES}
    pthread
)
