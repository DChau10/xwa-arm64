#include "driver_setup.h"
#include "../compat.h"
#include "../utils/log.h"
#include "../../res/resource.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <shellapi.h>
#include <commctrl.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "comctl32.lib")

#define IDC_BTN_OPEN_DEVMGMT 4001
#define IDC_BTN_CLOSE_ASSIST 4002
#define IDC_LBL_STATUS_TEXT  4003

static HWND g_hAssistantWnd = NULL;
static HWND g_hStatusLabel = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontHeader = NULL;
static bool g_winUsbDetected = false;

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
                // Match official dongle PIDs: 02FE (Slim), 02E6 (Bulky), 02D1, 091E (Surface)
                if (wcsstr(p, L"VID_045E&PID_02FE") || wcsstr(p, L"vid_045e&pid_02fe") ||
                    wcsstr(p, L"VID_045E&PID_02E6") || wcsstr(p, L"vid_045e&pid_02e6") ||
                    wcsstr(p, L"VID_045E&PID_02D1") || wcsstr(p, L"vid_045e&pid_02d1") ||
                    wcsstr(p, L"VID_045E&PID_091E") || wcsstr(p, L"vid_045e&pid_091e"))
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

static LRESULT CALLBACK AssistantWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        HINSTANCE hInst = GetModuleHandleW(NULL);
        HICON hBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
        HICON hSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        if (hBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hBig);
        if (hSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmall);

        NONCLIENTMETRICSW ncm = { sizeof(NONCLIENTMETRICSW) };
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        g_hFontNormal = CreateFontIndirectW(&ncm.lfMessageFont);

        LOGFONTW lfBold = ncm.lfMessageFont;
        lfBold.lfWeight = FW_BOLD;
        g_hFontBold = CreateFontIndirectW(&lfBold);

        LOGFONTW lfHeader = ncm.lfMessageFont;
        lfHeader.lfHeight = (LONG)(lfHeader.lfHeight * 1.25);
        lfHeader.lfWeight = FW_BOLD;
        g_hFontHeader = CreateFontIndirectW(&lfHeader);

        // Header Title
        HWND hTitle = CreateWindowExW(0, L"STATIC", L"Xbox Wireless Adapter — WinUSB Setup Guide",
            WS_CHILD | WS_VISIBLE, 20, 14, 520, 24, hwnd, NULL, NULL, NULL);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hFontHeader, TRUE);

        // Subtitle
        HWND hSub = CreateWindowExW(0, L"STATIC",
            L"Device Manager is open. Follow these steps to bind Microsoft's in-box WinUSB driver:",
            WS_CHILD | WS_VISIBLE, 20, 42, 520, 18, hwnd, NULL, NULL, NULL);
        SendMessageW(hSub, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // Groupbox for Steps
        HWND hGrp = CreateWindowExW(0, L"BUTTON", L" Step-by-Step Instructions ",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 15, 68, 530, 275, hwnd, NULL, NULL, NULL);
        SendMessageW(hGrp, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        const wchar_t *stepsText =
            L"1. In Device Manager, find \"XBOX ACC\" (under Other devices or USB).\n\n"
            L"2. Right-click \"XBOX ACC\" and select \"Update driver\".\n\n"
            L"3. Click \"Browse my computer for drivers\".\n\n"
            L"4. Click \"Let me pick from a list of available drivers on my computer\".\n\n"
            L"5. Select \"Universal Serial Bus devices\" from the list and click Next.\n"
            L"   (If device categories are not shown, proceed directly to Step 6)\n\n"
            L"6. In the left column select \"WinUsb Device\", and in the right select \"WinUsb Device\".\n\n"
            L"7. Click Next, confirm the Windows prompt (\"Yes\"), and click Close.";

        HWND hSteps = CreateWindowExW(0, L"STATIC", stepsText,
            WS_CHILD | WS_VISIBLE, 28, 92, 505, 240, hwnd, NULL, NULL, NULL);
        SendMessageW(hSteps, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // Status Group Box
        HWND hStatusGrp = CreateWindowExW(0, L"BUTTON", L" Live Detection Status ",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 15, 350, 530, 58, hwnd, NULL, NULL, NULL);
        SendMessageW(hStatusGrp, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        g_hStatusLabel = CreateWindowExW(0, L"STATIC",
            L"⏳ Waiting for WinUSB driver configuration in Device Manager...",
            WS_CHILD | WS_VISIBLE, 28, 374, 505, 24, hwnd, (HMENU)IDC_LBL_STATUS_TEXT, NULL, NULL);
        SendMessageW(g_hStatusLabel, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

        // Buttons
        HWND hBtnDev = CreateWindowExW(0, L"BUTTON", L"Open Device Manager",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 215, 420, 185, 32, hwnd, (HMENU)IDC_BTN_OPEN_DEVMGMT, NULL, NULL);
        SendMessageW(hBtnDev, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        HWND hBtnClose = CreateWindowExW(0, L"BUTTON", L"Close",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 415, 420, 130, 32, hwnd, (HMENU)IDC_BTN_CLOSE_ASSIST, NULL, NULL);
        SendMessageW(hBtnClose, WM_SETFONT, (WPARAM)g_hFontNormal, TRUE);

        // Check if WinUSB is already configured
        AdapterDriverStatus initialSt = checkAdapterDriverStatus();
        if (initialSt == AdapterDriverStatus::WINUSB_READY)
        {
            g_winUsbDetected = true;
            SetWindowTextW(g_hStatusLabel, L"✔ Success! Microsoft WinUSB driver is active! xwa-arm64 is ready.");
            SetWindowTextW(hBtnClose, L"Done");
        }
        else
        {
            g_winUsbDetected = false;
            SetTimer(hwnd, 1, 500, NULL);
            ShellExecuteW(NULL, L"open", L"devmgmt.msc", NULL, NULL, SW_SHOWNORMAL);
        }

        return 0;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == g_hStatusLabel)
        {
            if (g_winUsbDetected)
            {
                SetTextColor(hdc, RGB(0, 130, 20)); // Forest green
            }
            else
            {
                SetTextColor(hdc, RGB(180, 90, 0)); // Dark amber/orange
            }
        }
        SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_TIMER:
    {
        if (wParam == 1)
        {
            AdapterDriverStatus st = checkAdapterDriverStatus();
            if (st == AdapterDriverStatus::WINUSB_READY)
            {
                g_winUsbDetected = true;
                SetWindowTextW(g_hStatusLabel, L"✔ Success! Microsoft WinUSB driver detected! xwa-arm64 is now active.");
                SetWindowTextW(GetDlgItem(hwnd, IDC_BTN_CLOSE_ASSIST), L"Done");
                InvalidateRect(g_hStatusLabel, NULL, TRUE);
                KillTimer(hwnd, 1);
                SetTimer(hwnd, 2, 4000, NULL); // Auto-close 4 seconds after completion
            }
        }
        else if (wParam == 2)
        {
            KillTimer(hwnd, 2);
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_OPEN_DEVMGMT)
        {
            ShellExecuteW(NULL, L"open", L"devmgmt.msc", NULL, NULL, SW_SHOWNORMAL);
            return 0;
        }
        else if (id == IDC_BTN_CLOSE_ASSIST || id == IDCANCEL)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
    {
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        if (g_hFontNormal) { DeleteObject(g_hFontNormal); g_hFontNormal = NULL; }
        if (g_hFontBold) { DeleteObject(g_hFontBold); g_hFontBold = NULL; }
        if (g_hFontHeader) { DeleteObject(g_hFontHeader); g_hFontHeader = NULL; }
        g_hAssistantWnd = NULL;
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void showWinUsbAssistant(HWND hParent)
{
    if (g_hAssistantWnd)
    {
        SetForegroundWindow(g_hAssistantWnd);
        return;
    }

    HINSTANCE hInst = GetModuleHandleW(NULL);

    static bool s_classRegistered = false;
    if (!s_classRegistered)
    {
        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = AssistantWndProc;
        wc.hInstance = hInst;
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"XwaArm64WinUsbAssistantWnd";
        RegisterClassExW(&wc);
        s_classRegistered = true;
    }

    RECT rc = { 0, 0, 560, 465 };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    DWORD exStyle = WS_EX_TOPMOST;
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    g_hAssistantWnd = CreateWindowExW(
        exStyle,
        L"XwaArm64WinUsbAssistantWnd",
        L"xwa-arm64 — WinUSB Setup Assistant",
        style | WS_VISIBLE,
        x, y, width, height,
        NULL, NULL, hInst, NULL
    );

    if (g_hAssistantWnd)
    {
        ShowWindow(g_hAssistantWnd, SW_SHOW);
        SetForegroundWindow(g_hAssistantWnd);
    }
}
