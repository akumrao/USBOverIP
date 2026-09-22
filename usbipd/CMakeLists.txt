cmake_minimum_required(VERSION 3.20)
project(usbip_server CXX)

# Force C++17 or higher for standard modern threading support
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define the executable binary target pointing to the renamed source file
add_executable(usbip_server usbipd.cpp)

# Target-specific links to critical Windows Kernel System libraries
target_link_libraries(usbip_server PRIVATE 
    ws2_32       # Windows Sockets 2 API for TCP/IP Streaming
    setupapi     # SetupAPI for USB hardware descriptor lookup
)

# Enforce static compilation settings for standalone portable execution
if(MSVC)
    target_compile_options(usbip_server PRIVATE /W4)
endif()

