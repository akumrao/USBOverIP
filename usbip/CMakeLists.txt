cmake_minimum_required(VERSION 3.10)
project(usbip_client CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(usbip_client usbip.cpp)

# Link Windows Socket library required for network operations
target_link_libraries(usbip_client PRIVATE ws2_32)