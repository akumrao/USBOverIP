cmake_minimum_required(VERSION 3.20)
project(usbip_server CXX)

# Force C++17 or higher for standard modern threading support
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define the executable binary target pointing to source files
add_executable(usbip_server main.cpp usbipd.cpp)

# Target-specific links to critical Windows Kernel System libraries
target_link_libraries(usbip_server PRIVATE 
    ws2_32       # Windows Sockets 2 API for TCP/IP Streaming
    setupapi     # SetupAPI for USB hardware descriptor lookup
)

# Enforce static compilation settings for standalone portable execution
if(MSVC)
    target_compile_options(usbip_server PRIVATE /W4)
endif()

# List of required VirtualBox driver files to deploy beside the compiled executable
set(VBOX_DRIVER_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/VBoxUSBMon.sys"
    "${CMAKE_CURRENT_SOURCE_DIR}/VBoxUSB.sys"
    "${CMAKE_CURRENT_SOURCE_DIR}/VBoxUSB.inf"
    "${CMAKE_CURRENT_SOURCE_DIR}/VBoxUSB.cat"
)

# Post-build command: Copy driver files to the target build folder (build/Debug or build/Release)
add_custom_command(
    TARGET usbip_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VBOX_DRIVER_FILES}
        "$<TARGET_FILE_DIR:usbip_server>"
    COMMENT "Copying VirtualBox driver runtime dependencies to output directory..."
)