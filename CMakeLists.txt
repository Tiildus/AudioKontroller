cmake_minimum_required(VERSION 3.16)
project(AudioKontroller)

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
    WindowUtils.cpp WindowUtils.h
    ButtonHandler.cpp ButtonHandler.h
    ConfigManager.cpp ConfigManager.h
    Logger.cpp Logger.h
)

target_link_libraries(audiokontrollerd
    Qt6::Core
    Qt6::DBus
    ${HIDAPI_LIBRARIES}
    ${PULSE_LIBRARIES}
    pthread
)
