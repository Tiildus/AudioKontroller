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

find_package(Qt6 REQUIRED COMPONENTS Core DBus)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE REQUIRED libpulse)
pkg_check_modules(HIDAPI REQUIRED hidapi-libusb)

include_directories(
    ${HIDAPI_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
)

add_executable(audiokontrollerd
    main.cpp
    PCPanelHandler.cpp PCPanelHandler.h
    AudioHandler.cpp AudioHandler.h
    Overlay.cpp Overlay.h
    FocusMonitor.cpp FocusMonitor.h
    ButtonHandler.cpp ButtonHandler.h
    ConfigManager.cpp ConfigManager.h
    Logger.cpp Logger.h
)

target_compile_options(audiokontrollerd PRIVATE
    -Wall -Wextra -fstack-protector-strong -D_FORTIFY_SOURCE=2)
target_link_options(audiokontrollerd PRIVATE
    -Wl,-z,relro -Wl,-z,now)

target_link_libraries(audiokontrollerd
    Qt6::Core
    Qt6::DBus
    ${HIDAPI_LIBRARIES}
    ${PULSE_LIBRARIES}
    pthread
)
