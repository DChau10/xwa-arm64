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

# 2. Fetch libusb source
message(STATUS "Configuring libusb...")
FetchContent_Declare(
    libusb
    GIT_REPOSITORY https://github.com/libusb/libusb.git
    GIT_TAG        v1.0.27
)
FetchContent_GetProperties(libusb)
if(NOT libusb_POPULATED)
    FetchContent_Populate(libusb)
endif()

# Build libusb static library for Windows
set(LIBUSB_SRC
    ${libusb_SOURCE_DIR}/libusb/core.c
    ${libusb_SOURCE_DIR}/libusb/descriptor.c
    ${libusb_SOURCE_DIR}/libusb/hotplug.c
    ${libusb_SOURCE_DIR}/libusb/io.c
    ${libusb_SOURCE_DIR}/libusb/strerror.c
    ${libusb_SOURCE_DIR}/libusb/sync.c
    ${libusb_SOURCE_DIR}/libusb/os/events_windows.c
    ${libusb_SOURCE_DIR}/libusb/os/threads_windows.c
    ${libusb_SOURCE_DIR}/libusb/os/windows_common.c
    ${libusb_SOURCE_DIR}/libusb/os/windows_winusb.c
    ${libusb_SOURCE_DIR}/libusb/os/windows_usbdk.c
)

add_library(libusb STATIC ${LIBUSB_SRC})
target_include_directories(libusb PUBLIC
    ${libusb_SOURCE_DIR}/libusb
    ${libusb_SOURCE_DIR}/msvc
)
target_compile_definitions(libusb PRIVATE
    DEFAULT_VISIBILITY=
    _CRT_SECURE_NO_WARNINGS
)

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
    libusb
    setupapi
)

if(MSVC)
    target_compile_options(xow-win PRIVATE /W3 /utf-8 /permissive-)
    target_link_options(xow-win PRIVATE /SUBSYSTEM:CONSOLE)
endif()
