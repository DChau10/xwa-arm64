#include "tray.h"
#include "settings.h"
#include "../utils/log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <sstream>

#pragma comment(lib, "comctl32.lib")

#define WM_TRAYICON (WM_APP + 1)
#define IDM_HEADER 1001
#define IDM_STATUS 1002
#define IDM_SETTINGS 1003
#define IDM_PAIR 1004
#define IDM_EXIT 1005

#define IDC_CHK_ENABLE 2001
#define IDC_CMB_PROVIDER 2002
#define IDC_CMB_TAP 2003
#define IDC_CMB_HOLD 2004
#define IDC_CHK_HAPTIC 2005
#define IDC_BTN_SAVE 2006
#define IDC_BTN_CANCEL 2007

static HWND g_hTrayWnd = NULL;
static HWND g_hSettingsWnd = NULL;
static NOTIFYICONDATAW g_nid = {};
static SystemTray::PairCallback g_onPair;
static SystemTray::ExitCallback g_onExit;

static bool g_dongleActive = false;
static bool g_controllerConnected = false;
static std::string g_batteryLevel = "unknown";

static HFONT g_hFont = NULL;

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        // Group Box: Share Button
        HWND hGroup = CreateWindowExW(0, L"BUTTON", L" Xbox Wireless Controller (Share Button Settings) ",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 15, 10, 470, 240, hwnd, NULL, NULL, NULL);
        SendMessageW(hGroup, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Enable Checkbox
        HWND hChkEnable = CreateWindowExW(0, L"BUTTON", L"Enable Share Button Actions (Opt-in)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 35, 350, 20, hwnd, (HMENU)IDC_CHK_ENABLE, NULL, NULL);
        SendMessageW(hChkEnable, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Capture Provider Label & Combo
        HWND hLblProv = CreateWindowExW(0, L"STATIC", L"Capture Mode / Provider:",
            WS_CHILD | WS_VISIBLE, 30, 65, 200, 16, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblProv, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hCmbProv = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 30, 85, 435, 120, hwnd, (HMENU)IDC_CMB_PROVIDER, NULL, NULL);
        SendMessageW(hCmbProv, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCmbProv, CB_ADDSTRING, 0, (LPARAM)L"Auto-Detect (Steam Recording in Steam games, Game Bar otherwise)");
        SendMessageW(hCmbProv, CB_ADDSTRING, 0, (LPARAM)L"Steam Game Recording Only (F12 Screenshot / Ctrl+F11 Clip)");
        SendMessageW(hCmbProv, CB_ADDSTRING, 0, (LPARAM)L"Windows Game Bar Only (Win+Alt+PrtScn / Win+Alt+G)");
        SendMessageW(hCmbProv, CB_ADDSTRING, 0, (LPARAM)L"Custom Keybindings");

        // Tap Action Label & Combo
        HWND hLblTap = CreateWindowExW(0, L"STATIC", L"Single Tap Action (< 800ms):",
            WS_CHILD | WS_VISIBLE, 30, 115, 200, 16, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblTap, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hCmbTap = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 30, 135, 435, 120, hwnd, (HMENU)IDC_CMB_TAP, NULL, NULL);
        SendMessageW(hCmbTap, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCmbTap, CB_ADDSTRING, 0, (LPARAM)L"Take Screenshot (Steam F12 / Game Bar Win+Alt+PrtScn)");
        SendMessageW(hCmbTap, CB_ADDSTRING, 0, (LPARAM)L"Add Marker in Steam Timeline (Ctrl + F11)");
        SendMessageW(hCmbTap, CB_ADDSTRING, 0, (LPARAM)L"Custom Keystroke");
        SendMessageW(hCmbTap, CB_ADDSTRING, 0, (LPARAM)L"Disabled");

        // Hold Action Label & Combo
        HWND hLblHold = CreateWindowExW(0, L"STATIC", L"Long Press Hold Action (>= 800ms):",
            WS_CHILD | WS_VISIBLE, 30, 165, 250, 16, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblHold, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hCmbHold = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 30, 185, 435, 120, hwnd, (HMENU)IDC_CMB_HOLD, NULL, NULL);
        SendMessageW(hCmbHold, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCmbHold, CB_ADDSTRING, 0, (LPARAM)L"Save Recent Clip (Steam Clip / Game Bar Win+Alt+G 30s)");
        SendMessageW(hCmbHold, CB_ADDSTRING, 0, (LPARAM)L"Toggle Recording Start / Stop (Win + Alt + R)");
        SendMessageW(hCmbHold, CB_ADDSTRING, 0, (LPARAM)L"Custom Keystroke");
        SendMessageW(hCmbHold, CB_ADDSTRING, 0, (LPARAM)L"Disabled");

        // Haptic feedback Checkbox
        HWND hChkHaptic = CreateWindowExW(0, L"BUTTON", L"Vibrate controller (rumble feedback) when long press triggers",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 215, 420, 20, hwnd, (HMENU)IDC_CHK_HAPTIC, NULL, NULL);
        SendMessageW(hChkHaptic, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Bottom buttons
        HWND hBtnSave = CreateWindowExW(0, L"BUTTON", L"Save & Apply",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 280, 265, 100, 28, hwnd, (HMENU)IDC_BTN_SAVE, NULL, NULL);
        SendMessageW(hBtnSave, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 390, 265, 95, 28, hwnd, (HMENU)IDC_BTN_CANCEL, NULL, NULL);
        SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Populate controls from current settings
        const Settings &s = getGlobalSettings();
        SendMessageW(hChkEnable, BM_SETCHECK, s.enableShareButton ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hCmbProv, CB_SETCURSEL, static_cast<int>(s.provider), 0);
        SendMessageW(hCmbTap, CB_SETCURSEL, static_cast<int>(s.tapAction), 0);
        SendMessageW(hCmbHold, CB_SETCURSEL, static_cast<int>(s.holdAction), 0);
        SendMessageW(hChkHaptic, BM_SETCHECK, s.hapticFeedback ? BST_CHECKED : BST_UNCHECKED, 0);

        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_SAVE)
        {
            Settings s = getGlobalSettings();
            s.enableShareButton = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_ENABLE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            s.provider = static_cast<CaptureProvider>(SendMessageW(GetDlgItem(hwnd, IDC_CMB_PROVIDER), CB_GETCURSEL, 0, 0));
            s.tapAction = static_cast<TapAction>(SendMessageW(GetDlgItem(hwnd, IDC_CMB_TAP), CB_GETCURSEL, 0, 0));
            s.holdAction = static_cast<HoldAction>(SendMessageW(GetDlgItem(hwnd, IDC_CMB_HOLD), CB_GETCURSEL, 0, 0));
            s.hapticFeedback = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_HAPTIC), BM_GETCHECK, 0, 0) == BST_CHECKED);

            s.save();
            setGlobalSettings(s);

            DestroyWindow(hwnd);
            return 0;
        }
        else if (id == IDC_BTN_CANCEL || id == IDCANCEL)
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
        g_hSettingsWnd = NULL;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
    {
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK)
        {
            if (lParam == WM_LBUTTONDBLCLK)
            {
                SystemTray::getInstance().showSettingsDialog();
                return 0;
            }

            POINT pt;
            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_HEADER, L"xow-win v1.0.0");

            std::wstring statusStr = L"Dongle: Active";
            if (g_controllerConnected)
            {
                std::wstring bStr(g_batteryLevel.begin(), g_batteryLevel.end());
                statusStr = L"Controller: Connected (Battery: " + bStr + L")";
            }
            else if (!g_dongleActive)
            {
                statusStr = L"Dongle: Waiting / Unplugged";
            }
            else
            {
                statusStr = L"Controller: None Connected";
            }

            AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_STATUS, statusStr.c_str());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"Settings...");
            AppendMenuW(hMenu, MF_STRING, IDM_PAIR, L"Pair New Controller");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");

            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDM_SETTINGS)
        {
            SystemTray::getInstance().showSettingsDialog();
        }
        else if (id == IDM_PAIR)
        {
            if (g_onPair) g_onPair();
        }
        else if (id == IDM_EXIT)
        {
            if (g_onExit) g_onExit();
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

SystemTray &SystemTray::getInstance()
{
    static SystemTray instance;
    return instance;
}

bool SystemTray::init(HINSTANCE hInstance, PairCallback onPair, ExitCallback onExit)
{
    g_onPair = std::move(onPair);
    g_onExit = std::move(onExit);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"XowWinTrayWnd";
    RegisterClassExW(&wc);

    WNDCLASSEXW swc = {};
    swc.cbSize = sizeof(WNDCLASSEXW);
    swc.lpfnWndProc = SettingsWndProc;
    swc.hInstance = hInstance;
    swc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    swc.lpszClassName = L"XowWinSettingsWnd";
    RegisterClassExW(&swc);

    g_hTrayWnd = CreateWindowExW(0, L"XowWinTrayWnd", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
    if (!g_hTrayWnd)
    {
        Log::error("Failed to create tray window");
        return false;
    }

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hTrayWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"xow-win: Xbox Wireless Adapter");

    Shell_NotifyIconW(NIM_ADD, &g_nid);
    return true;
}

void SystemTray::updateStatus(bool dongleActive, bool controllerConnected, const std::string &batteryLevel)
{
    g_dongleActive = dongleActive;
    g_controllerConnected = controllerConnected;
    g_batteryLevel = batteryLevel;

    std::wstring tip = L"xow-win: Xbox Wireless Adapter";
    if (controllerConnected)
    {
        std::wstring b(batteryLevel.begin(), batteryLevel.end());
        tip = L"xow-win: Controller connected (" + b + L")";
    }
    else if (!dongleActive)
    {
        tip = L"xow-win: Waiting for adapter...";
    }

    wcsncpy_s(g_nid.szTip, tip.c_str(), sizeof(g_nid.szTip) / sizeof(wchar_t) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void SystemTray::showSettingsDialog()
{
    if (g_hSettingsWnd)
    {
        SetForegroundWindow(g_hSettingsWnd);
        return;
    }

    int width = 515;
    int height = 345;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    g_hSettingsWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"XowWinSettingsWnd",
        L"xow-win Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, width, height,
        NULL, NULL, GetModuleHandleW(NULL), NULL
    );

    if (g_hSettingsWnd)
    {
        ShowWindow(g_hSettingsWnd, SW_SHOW);
        SetForegroundWindow(g_hSettingsWnd);
    }
}

void SystemTray::processMessages()
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void SystemTray::shutdown()
{
    if (g_hSettingsWnd)
    {
        DestroyWindow(g_hSettingsWnd);
        g_hSettingsWnd = NULL;
    }

    Shell_NotifyIconW(NIM_DELETE, &g_nid);

    if (g_hTrayWnd)
    {
        DestroyWindow(g_hTrayWnd);
        g_hTrayWnd = NULL;
    }
}
