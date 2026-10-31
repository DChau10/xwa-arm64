#include "driver_setup.h"
#include "../compat.h"
#include "../utils/log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "comctl32.lib")

// Official Zadig 2.9 release from https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe
static const wchar_t *ZADIG_DOWNLOAD_URL = L"https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe";
static const char *ZADIG_SHA256 = "4ECAA95DF3DA3621486A043AEF8B3050B8BAFE7C901402871E816229EF82039B";

AdapterDriverStatus checkAdapterDriverStatus(std::wstring *outDesc, std::wstring *outHardwareId)
{
    HDEVINFO devInfo = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devInfo == INVALID_HANDLE_VALUE)
    {
        return AdapterDriverStatus::NOT_PRESENT;
    }

    SP_DEVINFO_DATA devData = {};
    devData.cbSize = sizeof(SP_DEVINFO_DATA);

    AdapterDriverStatus result = AdapterDriverStatus::NOT_PRESENT;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(devInfo, i, &devData); ++i)
    {
        wchar_t hwId[4096] = { 0 };
        if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_HARDWAREID, NULL, (PBYTE)hwId, sizeof(hwId), NULL))
        {
            for (const wchar_t *p = hwId; *p; p += wcslen(p) + 1)
            {
                if (wcsstr(p, L"VID_045E&PID_02FE") || wcsstr(p, L"VID_045E&PID_02E6") || wcsstr(p, L"VID_045E&PID_02D1"))
                {
                    if (outHardwareId) *outHardwareId = p;

                    wchar_t desc[512] = { 0 };
                    if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_DEVICEDESC, NULL, (PBYTE)desc, sizeof(desc), NULL))
                    {
                        if (outDesc) *outDesc = desc;
                    }
                    else if (outDesc)
                    {
                        *outDesc = L"Xbox Wireless Adapter";
                    }

                    wchar_t service[256] = { 0 };
                    if (SetupDiGetDeviceRegistryPropertyW(devInfo, &devData, SPDRP_SERVICE, NULL, (PBYTE)service, sizeof(service), NULL))
                    {
                        if (_wcsicmp(service, L"WinUSB") == 0)
                        {
                            result = AdapterDriverStatus::WINUSB_READY;
                        }
                        else
                        {
                            result = AdapterDriverStatus::DRIVER_MISSING;
                        }
                    }
                    else
                    {
                        // No service assigned (Code 28 missing driver)
                        result = AdapterDriverStatus::DRIVER_MISSING;
                    }

                    SetupDiDestroyDeviceInfoList(devInfo);
                    return result;
                }
            }
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

static bool verifyFileHash(const std::wstring &filePath, const std::string &expectedHex)
{
    std::ifstream stream(filePath, std::ios::binary);
    if (!stream.is_open())
    {
        return false;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (buffer.empty())
    {
        return false;
    }

    std::string actualHex;
    if (!calculateSha256(buffer.data(), buffer.size(), actualHex))
    {
        return false;
    }
    return (_stricmp(actualHex.c_str(), expectedHex.c_str()) == 0);
}

static bool downloadFile(const std::wstring &url, const std::wstring &destPath)
{
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
                       L"\"[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
                       L"(New-Object Net.WebClient).DownloadFile('" + url + L"', '" + destPath + L"')\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(NULL, const_cast<LPWSTR>(cmd.c_str()), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        return false;
    }

    WaitForSingleObject(pi.hProcess, 60000);

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exitCode == 0 && GetFileAttributesW(destPath.c_str()) != INVALID_FILE_ATTRIBUTES);
}

bool promptAndInstallWinUsb(HWND hParent)
{
    std::wstring deviceDesc, hwId;
    AdapterDriverStatus status = checkAdapterDriverStatus(&deviceDesc, &hwId);

    if (status == AdapterDriverStatus::WINUSB_READY)
    {
        MessageBoxW(
            hParent,
            L"The Xbox Wireless Adapter is already properly configured with the WinUSB driver and ready for use.",
            L"xwa-arm64: Driver Ready",
            MB_OK | MB_ICONINFORMATION
        );
        return true;
    }

    if (status == AdapterDriverStatus::NOT_PRESENT)
    {
        int res = MessageBoxW(
            hParent,
            L"No Xbox Wireless Adapter was detected. Please plug in your Xbox Wireless USB Adapter and try again.\n\n"
            L"Would you still like to open the driver setup tool?",
            L"xwa-arm64: Adapter Not Found",
            MB_YESNO | MB_ICONWARNING
        );
        if (res != IDYES)
        {
            return false;
        }
    }
    else
    {
        std::wstring msg = L"Xbox Wireless Adapter ('" + deviceDesc + L"') was detected, but the required WinUSB driver is missing (Code 28).\n\n"
                           L"xwa-arm64 can download the official driver setup tool (Zadig, cryptographically verified with SHA-256) and launch it to configure WinUSB for your adapter in one click.\n\n"
                           L"Would you like to configure the WinUSB driver now?";

        int res = MessageBoxW(
            hParent,
            msg.c_str(),
            L"xwa-arm64: WinUSB Driver Required",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1
        );
        if (res != IDYES)
        {
            return false;
        }
    }

    // Determine storage location in %LOCALAPPDATA%\xwa-arm64
    wchar_t localAppData[MAX_PATH] = { 0 };
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData)))
    {
        wcscpy_s(localAppData, L"C:\\Temp");
    }

    std::wstring dirPath = std::wstring(localAppData) + L"\\xwa-arm64";
    CreateDirectoryW(dirPath.c_str(), NULL);

    std::wstring zadigExePath = dirPath + L"\\zadig-2.9.exe";
    std::wstring zadigIniPath = dirPath + L"\\zadig.ini";
    std::wstring zadigCfgPath = dirPath + L"\\zadig_preset.cfg";

    // Download Zadig if needed
    if (!verifyFileHash(zadigExePath, ZADIG_SHA256))
    {
        Log::info("Downloading official Zadig driver utility from %ls...", ZADIG_DOWNLOAD_URL);
        if (!downloadFile(ZADIG_DOWNLOAD_URL, zadigExePath))
        {
            MessageBoxW(
                hParent,
                L"Failed to download Zadig driver setup tool. Please check your internet connection.",
                L"xwa-arm64: Download Error",
                MB_OK | MB_ICONERROR
            );
            return false;
        }

        if (!verifyFileHash(zadigExePath, ZADIG_SHA256))
        {
            DeleteFileW(zadigExePath.c_str());
            MessageBoxW(
                hParent,
                L"Security validation failed: Downloaded driver installer does not match expected cryptographic hash.",
                L"xwa-arm64: Security Error",
                MB_OK | MB_ICONERROR
            );
            return false;
        }
        Log::info("Zadig binary verified successfully (SHA-256: %s)", ZADIG_SHA256);
    }

    // Write pre-configured zadig.ini
    std::ofstream iniFile(zadigIniPath);
    if (iniFile.is_open())
    {
        iniFile << "[general]\n"
                << "  advanced_mode = false\n"
                << "  exit_on_success = true\n"
                << "  log_level = 1\n\n"
                << "[device]\n"
                << "  list_all = true\n"
                << "  include_hubs = false\n"
                << "  trim_whitespaces = true\n\n"
                << "[driver]\n"
                << "  default_driver = 0\n"; // 0 = WinUSB
        iniFile.close();
    }

    // Write preset configuration targeting Xbox adapter VID/PID
    std::ofstream cfgFile(zadigCfgPath);
    if (cfgFile.is_open())
    {
        cfgFile << "[device]\n"
                << "  Description = \"Xbox Wireless Adapter (WinUSB)\"\n"
                << "  VID = 0x045E\n"
                << "  PID = 0x02FE\n";
        cfgFile.close();
    }

    // Launch Zadig elevated
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = hParent;
    sei.lpVerb = L"runas";
    sei.lpFile = zadigExePath.c_str();
    sei.lpDirectory = dirPath.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei))
    {
        DWORD err = GetLastError();
        if (err != ERROR_CANCELLED) // User didn't just cancel UAC
        {
            MessageBoxW(
                hParent,
                L"Unable to launch driver installer with administrator privileges.",
                L"xwa-arm64: Elevation Error",
                MB_OK | MB_ICONERROR
            );
        }
        return false;
    }

    if (sei.hProcess)
    {
        CloseHandle(sei.hProcess);
    }

    MessageBoxW(
        hParent,
        L"Driver installer opened!\n\n"
        L"1. Ensure 'XBOX ACC' or 'Xbox Wireless Adapter' is selected in the list.\n"
        L"2. Ensure the target driver is 'WinUSB'.\n"
        L"3. Click 'Install Driver' (or 'Replace Driver').\n\n"
        L"xwa-arm64 will automatically detect the adapter as soon as installation completes.",
        L"xwa-arm64: Driver Setup In Progress",
        MB_OK | MB_ICONINFORMATION
    );

    return true;
}
