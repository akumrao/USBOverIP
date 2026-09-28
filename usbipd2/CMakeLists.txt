cmake_minimum_required(VERSION 3.20)
project(usbip_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    UsbDeviceMonitor.cpp
    UsbIpServerDaemon.cpp
)

set(HEADERS
    UsbDeviceMonitor.hpp
    UsbIpServerDaemon.hpp
)

add_executable(usbip_server ${SOURCES} ${HEADERS})

target_link_libraries(usbip_server PRIVATE 
    ws2_32       # Networking
    setupapi     # Device Discovery
    newdev       # Driver Update / INF Installer API
    winusb       # Real Physical Hardware USB I/O
)

if(MSVC)
    target_compile_options(usbip_server PRIVATE /W4)
endif()