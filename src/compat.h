#pragma once

#include <cstdint>
#include <ctime>
#include <string>

#if defined(_MSC_VER)
    #define PACKED_STRUCT_BEGIN __pragma(pack(push, 1))
    #define PACKED_STRUCT_END __pragma(pack(pop))
    #define PACKED
#else
    #define PACKED_STRUCT_BEGIN
    #define PACKED_STRUCT_END
    #define PACKED __attribute__((packed))
#endif

// Portable localtime_r replacement
inline struct tm* portable_localtime(const time_t* timep, struct tm* result)
{
#if defined(_WIN32)
    if (localtime_s(result, timep) == 0) {
        return result;
    }
    return nullptr;
#else
    return localtime_r(timep, result);
#endif
}

// Default firmware search paths for Windows and POSIX
inline std::string get_firmware_path()
{
#if defined(_WIN32)
    // 1. Check local directory next to executable
    char exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
        std::string dir = exePath;
        size_t pos = dir.find_last_of("\\/");
        if (pos != std::string::npos) {
            std::string localFw = dir.substr(0, pos + 1) + "FW_ACC_00U.bin";
            DWORD attrib = GetFileAttributesA(localFw.c_str());
            if (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
                return localFw;
            }
        }
    }

    // 2. Check %ProgramData%\xow-win\FW_ACC_00U.bin
    const char* programData = getenv("ProgramData");
    if (programData) {
        std::string pdFw = std::string(programData) + "\\xow-win\\FW_ACC_00U.bin";
        DWORD attrib = GetFileAttributesA(pdFw.c_str());
        if (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
            return pdFw;
        }
    }

    // Default fallback
    return "FW_ACC_00U.bin";
#else
    return "/lib/firmware/xow_dongle.bin";
#endif
}
