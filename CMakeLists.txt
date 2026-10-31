cmake_minimum_required(VERSION 3.16)
project(xow-win LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose build type" FORCE)
endif()

# Place output binary in bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin")

include(FetchContent)

# 1. Fetch ViGEmClient
message(STATUS "Configuring ViGEmClient...")
FetchContent_Declare(
    ViGEmClient
    GIT_REPOSITORY https://github.com/nefarius/ViGEmClient.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(ViGEmClient)

# 2. Fetch libusb
message(STATUS "Configuring libusb...")
FetchContent_Declare(
    libusb
    GIT_REPOSITORY https://github.com/libusb/libusb.git
    GIT_TAG        v1.0.27
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libusb" FORCE)
set(LIBUSB_BUILD_EXAMPLES OFF CACHE BOOL "Disable libusb examples" FORCE)
set(LIBUSB_BUILD_TESTS OFF CACHE BOOL "Disable libusb tests" FORCE)
FetchContent_MakeAvailable(libusb)

# Source files
set(SOURCES
    src/main.cpp
    src/compat.h
    src/utils/log.h
    src/utils/log.cpp
    src/utils/bytes.h
    src/utils/buffer.h
    src/dongle/usb.h
    src/dongle/usb.cpp
    src/dongle/mt76.h
    src/dongle/mt76.cpp
    src/dongle/dongle.h
    src/dongle/dongle.cpp
    src/controller/gip.h
    src/controller/gip.cpp
    src/controller/controller.h
    src/controller/controller.cpp
    src/gamepad/gamepad.h
    src/gamepad/vigem_gamepad.h
    src/gamepad/vigem_gamepad.cpp
)

add_executable(xow-win ${SOURCES})

target_include_directories(xow-win PRIVATE
    src
    ${vigemclient_SOURCE_DIR}/include
    ${libusb_SOURCE_DIR}/libusb
)

target_link_libraries(xow-win PRIVATE
    ViGEmClient
    usb-1.0
    setupapi
)

if(MSVC)
    target_compile_options(xow-win PRIVATE /W3 /utf-8 /permissive-)
    target_link_options(xow-win PRIVATE /SUBSYSTEM:CONSOLE)
endif()
