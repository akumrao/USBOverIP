cmake_minimum_required(VERSION 3.16)
project(WebRtcFirebaseSignaling VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =================================================================
# 1. DISCOVER CONAN 2 GENERATED TARGET PACKAGES
# =================================================================
find_package(OpenSSL REQUIRED)
find_package(google-webrtc REQUIRED)
find_package(firebase-cpp-sdk REQUIRED)
find_package(spdlog REQUIRED)

# =================================================================
# 2. EXECUTABLE BUILD MODULE CONFIGURATIONS
# =================================================================
add_executable(webrtc_signaling_app 
    main.cpp 
    signaling.cpp
    webrtc_stats.cpp
)

if(WIN32)
    target_compile_definitions(webrtc_signaling_app PRIVATE WEBRTC_WIN NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_definitions(webrtc_signaling_app PRIVATE WEBRTC_POSIX)
endif()

# =================================================================
# 3. LINK DEPENDENCIES & RESOLVE NATIVE SYSTEM KEYRINGS
# =================================================================
find_package(Threads REQUIRED)

if(WIN32)
    target_link_libraries(webrtc_signaling_app
        PRIVATE
        google-webrtc::google-webrtc
        firebase-cpp-sdk::firebase-cpp-sdk
        spdlog::spdlog
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        ws2_32 winmm secur32 crypt32 iphlpapi shlwapi user32 gdi32
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SECRET REQUIRED IMPORTED_TARGET libsecret-1)
    pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)

    target_link_libraries(webrtc_signaling_app 
        PRIVATE 
        google-webrtc::google-webrtc
        firebase-cpp-sdk::firebase-cpp-sdk 
        spdlog::spdlog
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        PkgConfig::SECRET
        PkgConfig::GLIB
        dl rt m pthread
    )
endif()