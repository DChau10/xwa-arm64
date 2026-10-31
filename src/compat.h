#pragma once

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <intrin.h>
    #include <stdlib.h>
#endif

#include <cstdint>
#include <ctime>
#include <string>

#if defined(_MSC_VER)
    #define PACKED_STRUCT_BEGIN __pragma(pack(push, 1))
    #define PACKED_STRUCT_END __pragma(pack(pop))
    #define PACKED

    inline int portable_ffs(int x)
    {
        unsigned long index;
        if (_BitScanForward(&index, static_cast<unsigned long>(x)))
        {
            return static_cast<int>(index + 1);
        }
        return 0;
    }
    #define __builtin_ffs portable_ffs
    #define __builtin_bswap32 _byteswap_ulong
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

    // 2. Check %ProgramData%\xwa-arm64\FW_ACC_00U.bin (with xow-win legacy fallback)
    char* programData = nullptr;
    size_t len = 0;
    if (_dupenv_s(&programData, &len, "ProgramData") == 0 && programData) {
        std::string newFw = std::string(programData) + "\\xwa-arm64\\FW_ACC_00U.bin";
        std::string oldFw = std::string(programData) + "\\xow-win\\FW_ACC_00U.bin";
        free(programData);
        DWORD attrib = GetFileAttributesA(newFw.c_str());
        if (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
            return newFw;
        }
        attrib = GetFileAttributesA(oldFw.c_str());
        if (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
            return oldFw;
        }
    }

    // Default fallback
    return "FW_ACC_00U.bin";
#else
    return "/lib/firmware/xow_dongle.bin";
#endif
}
