#include "driver_setup.h"
#include "../compat.h"
#include "../utils/log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <commctrl.h>
#include <shellapi.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "comctl32.lib")

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

bool promptAndInstallWinUsb(HWND hParent)
{
    std::wstring deviceDesc, hwId;
    AdapterDriverStatus status = checkAdapterDriverStatus(&deviceDesc, &hwId);

    if (status == AdapterDriverStatus::WINUSB_READY)
    {
        MessageBoxW(
            hParent,
            L"The Xbox Wireless Adapter is already properly configured with Microsoft's in-box WinUSB driver and ready for use.",
            L"xwa-arm64: WinUSB Ready",
            MB_OK | MB_ICONINFORMATION
        );
        return true;
    }

    std::wstring adapterName = deviceDesc.empty() ? L"XBOX ACC" : deviceDesc;

    std::wstring message =
        L"Xbox Wireless Adapter ('" + adapterName + L"') is connected, "
        L"but requires Microsoft's native WinUSB driver to work with xwa-arm64 on ARM64.\n\n"
        L"You can configure this directly using Windows Device Manager (100% native, no third-party tools):\n\n"
        L"1. Click 'Yes' below to open Device Manager.\n"
        L"2. Right-click '" + adapterName + L"' (under 'Other devices' or 'Universal Serial Bus') -> 'Update driver'.\n"
        L"3. Click 'Browse my computer for drivers'.\n"
        L"4. Click 'Let me pick from a list of available drivers on my computer'.\n"
        L"5. Select 'Universal Serial Bus devices' -> click Next.\n"
        L"6. Select Manufacturer: 'WinUsb Device', Model: 'WinUsb Device'.\n"
        L"7. Click Next, confirm the prompt, and finish.\n\n"
        L"xwa-arm64 will automatically detect the adapter as soon as you finish!\n\n"
        L"Would you like to open Device Manager now?";

    int res = MessageBoxW(
        hParent,
        message.c_str(),
        L"xwa-arm64: Native WinUSB Driver Setup",
        MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1
    );

    if (res == IDYES)
    {
        ShellExecuteW(NULL, L"open", L"devmgmt.msc", NULL, NULL, SW_SHOWNORMAL);
        return true;
    }

    return false;
}
