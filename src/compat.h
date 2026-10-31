#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <bcrypt.h>
    #include <intrin.h>
    #pragma comment(lib, "bcrypt.lib")

    inline bool calculateSha256(const uint8_t *data, size_t length, std::string &outHex)
    {
        BCRYPT_ALG_HANDLE hAlg = NULL;
        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0)
            return false;

        UCHAR hash[32] = { 0 };
        NTSTATUS status = BCryptHash(hAlg, NULL, 0, (PUCHAR)data, static_cast<ULONG>(length), hash, sizeof(hash));
        BCryptCloseAlgorithmProvider(hAlg, 0);

        if (status != 0)
            return false;

        char hexBuf[65] = { 0 };
        for (int i = 0; i < 32; ++i)
        {
            sprintf_s(hexBuf + (i * 2), 3, "%02x", hash[i]);
        }
        outHex = hexBuf;
        return true;
    }
#endif

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

    // 2. Check %ProgramData%\xwa-arm64\FW_ACC_00U.bin
    char* programData = nullptr;
    size_t len = 0;
    std::string pdPath;
    if (_dupenv_s(&programData, &len, "ProgramData") == 0 && programData) {
        pdPath = std::string(programData) + "\\xwa-arm64";
        free(programData);
        std::string fwPath = pdPath + "\\FW_ACC_00U.bin";
        DWORD attrib = GetFileAttributesA(fwPath.c_str());
        if (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY)) {
            return fwPath;
        }
    } else {
        pdPath = "C:\\ProgramData\\xwa-arm64";
    }

    // 3. Auto-download on-demand from Microsoft Update CDN if not found
    CreateDirectoryA(pdPath.c_str(), NULL);
    std::string targetFw = pdPath + "\\FW_ACC_00U.bin";

    // Check if get-firmware.ps1 script exists relative to executable
    std::string scriptPath;
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
        std::string dir = exePath;
        size_t pos = dir.find_last_of("\\/");
        if (pos != std::string::npos) {
            std::string p1 = dir.substr(0, pos + 1) + "get-firmware.ps1";
            std::string p2 = dir.substr(0, pos + 1) + "..\\scripts\\get-firmware.ps1";
            if (GetFileAttributesA(p1.c_str()) != INVALID_FILE_ATTRIBUTES) scriptPath = p1;
            else if (GetFileAttributesA(p2.c_str()) != INVALID_FILE_ATTRIBUTES) scriptPath = p2;
        }
    }

    std::string cmd;
    if (!scriptPath.empty()) {
        cmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + scriptPath + "\" -DestinationDir \"" + pdPath + "\"";
    } else {
        cmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \""
              "$u='http://download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/07/1cd6a87c-623f-4407-a52d-c31be49e925c_e19f60808bdcbfbd3c3df6be3e71ffc52e43261e.cab';"
              "$c=\\\"$env:TEMP\\fw.cab\\\";"
              "$e=\\\"$env:TEMP\\fwe\\\";"
              "Invoke-WebRequest -Uri $u -OutFile $c -UseBasicParsing;"
              "New-Item -ItemType Directory -Path $e -Force | Out-Null;"
              "& expand.exe $c -F:FW_ACC_00U.bin $e | Out-Null;"
              "$h=(Get-FileHash -Path \\\"$e\\FW_ACC_00U.bin\\\" -Algorithm SHA256).Hash;"
              "if ($h -eq '48084D9FA53B9BB04358F3BB127B7495DC8F7BB0B3CA1437BD24EF2B6EABDF66') {"
              "  Copy-Item \\\"$e\\FW_ACC_00U.bin\\\" -Destination \\\"" + targetFw + "\\\" -Force;"
              "  Remove-Item $c -Force -ErrorAction SilentlyContinue;"
              "  Remove-Item $e -Recurse -Force -ErrorAction SilentlyContinue;"
              "  exit 0;"
              "} else { exit 1; }\"";
    }

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');

    if (CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode == 0 && GetFileAttributesA(targetFw.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return targetFw;
        }
    }

    // Default fallback
    return "FW_ACC_00U.bin";
#else
    return "/lib/firmware/xow_dongle.bin";
#endif
}
