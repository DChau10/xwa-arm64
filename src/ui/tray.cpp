#include "tray.h"
#include "settings.h"
#include "../utils/log.h"
#include "../../res/resource.h"

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
#define IDM_AUTOSTART 1006
#define IDM_EXIT 1005

#define IDC_CHK_ENABLE 2001
#define IDC_CMB_PROVIDER 2002
#define IDC_CMB_TAP 2003
#define IDC_CMB_HOLD 2004
#define IDC_CMB_HOLD2 2005
#define IDC_CMB_HAPTIC 2006
#define IDC_CHK_AUTOSTART 2009
#define IDC_BTN_SAVE 2007
#define IDC_BTN_CANCEL 2008

static bool isAutoStartEnabled()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
        return false;
    }
    wchar_t val[MAX_PATH * 2] = { 0 };
    DWORD size = sizeof(val);
    DWORD type = 0;
    LONG res = RegQueryValueExW(hKey, L"xwa-arm64", NULL, &type, (LPBYTE)val, &size);
    RegCloseKey(hKey);
    return (res == ERROR_SUCCESS);
}

static bool setAutoStartEnabled(bool enable)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
    {
        return false;
    }

    if (enable)
    {
        wchar_t exePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, exePath, MAX_PATH);

        std::wstring cmd = L"\"" + std::wstring(exePath) + L"\" --hide";
        LONG res = RegSetValueExW(hKey, L"xwa-arm64", 0, REG_SZ,
            (const BYTE*)cmd.c_str(), static_cast<DWORD>((cmd.length() + 1) * sizeof(wchar_t)));
        RegDeleteValueW(hKey, L"xow-win"); // Clean up legacy key
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    else
    {
        LONG res = RegDeleteValueW(hKey, L"xwa-arm64");
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS || res == ERROR_FILE_NOT_FOUND);
    }
}

static HWND g_hTrayWnd = NULL;
static HWND g_hSettingsWnd = NULL;
static NOTIFYICONDATAW g_nid = {};
static SystemTray::PairCallback g_onPair;
static SystemTray::ExitCallback g_onExit;

static bool g_dongleActive = false;
static bool g_controllerConnected = false;
static std::string g_batteryLevel = "unknown";

static HFONT g_hFont = NULL;

#define IDC_BTN_CUSTOM_KEYS 2010

struct KeyItem {
    const wchar_t *name;
    uint8_t vk;
};

static const KeyItem g_keyItems[] = {
    { L"F1", 0x70 }, { L"F2", 0x71 }, { L"F3", 0x72 }, { L"F4", 0x73 },
    { L"F5", 0x74 }, { L"F6", 0x75 }, { L"F7", 0x76 }, { L"F8", 0x77 },
    { L"F9", 0x78 }, { L"F10", 0x79 }, { L"F11", 0x7A }, { L"F12", 0x7B },
    { L"Print Screen", 0x2C }, { L"Scroll Lock", 0x91 }, { L"Pause / Break", 0x13 },
    { L"Space", 0x20 }, { L"Enter", 0x0D }, { L"Tab", 0x09 }, { L"Escape", 0x1B },
    { L"Backspace", 0x08 }, { L"Delete", 0x2E }, { L"Insert", 0x2D },
    { L"Home", 0x24 }, { L"End", 0x23 }, { L"Page Up", 0x21 }, { L"Page Down", 0x22 },
    { L"A", 0x41 }, { L"B", 0x42 }, { L"C", 0x43 }, { L"D", 0x44 }, { L"E", 0x45 },
    { L"F", 0x46 }, { L"G", 0x47 }, { L"H", 0x48 }, { L"I", 0x49 }, { L"J", 0x4A },
    { L"K", 0x4B }, { L"L", 0x4C }, { L"M", 0x4D }, { L"N", 0x4E }, { L"O", 0x4F },
    { L"P", 0x50 }, { L"Q", 0x51 }, { L"R", 0x52 }, { L"S", 0x53 }, { L"T", 0x54 },
    { L"U", 0x55 }, { L"V", 0x56 }, { L"W", 0x57 }, { L"X", 0x58 }, { L"Y", 0x59 }, { L"Z", 0x5A },
    { L"0", 0x30 }, { L"1", 0x31 }, { L"2", 0x32 }, { L"3", 0x33 }, { L"4", 0x34 },
    { L"5", 0x35 }, { L"6", 0x36 }, { L"7", 0x37 }, { L"8", 0x38 }, { L"9", 0x39 },
    { L"Mute Audio", 0xAD }, { L"Volume Down", 0xAE }, { L"Volume Up", 0xAF },
    { L"Play / Pause", 0xB3 }, { L"Next Track", 0xB0 }, { L"Prev Track", 0xB1 }
};

static KeyBinding g_tempCustomTap;
static KeyBinding g_tempCustomHold;
static KeyBinding g_tempCustomHold2;

static int getIndexForVk(uint8_t vk)
{
    int count = sizeof(g_keyItems) / sizeof(g_keyItems[0]);
    for (int i = 0; i < count; ++i)
    {
        if (g_keyItems[i].vk == vk) return i;
    }
    return 11; // Default to F12
}

// Custom Keystrokes Sub-Dialog
#define IDC_CK_TAP_WIN   3001
#define IDC_CK_TAP_CTRL  3002
#define IDC_CK_TAP_ALT   3003
#define IDC_CK_TAP_SHIFT 3004
#define IDC_CMB_TAP_KEY  3005

#define IDC_CK_H1_WIN    3011
#define IDC_CK_H1_CTRL   3012
#define IDC_CK_H1_ALT    3013
#define IDC_CK_H1_SHIFT  3014
#define IDC_CMB_H1_KEY   3015

#define IDC_CK_H2_WIN    3021
#define IDC_CK_H2_CTRL   3022
#define IDC_CK_H2_ALT    3023
#define IDC_CK_H2_SHIFT  3024
#define IDC_CMB_H2_KEY   3025

#define IDC_BTN_CK_OK    3030
#define IDC_BTN_CK_CAN   3031

static HWND g_hCustomKeysWnd = NULL;

static LRESULT CALLBACK CustomKeysWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
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

        auto populateCombo = [&](HWND hCmb, uint8_t curVk) {
            SendMessageW(hCmb, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            int count = sizeof(g_keyItems) / sizeof(g_keyItems[0]);
            for (int i = 0; i < count; ++i)
            {
                SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)g_keyItems[i].name);
            }
            SendMessageW(hCmb, CB_SETCURSEL, getIndexForVk(curVk), 0);
        };

        // Section 1: Single Tap
        HWND hGrp1 = CreateWindowExW(0, L"BUTTON", L" Single Tap (< 1.25s) Custom Keystroke ",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 15, 10, 455, 65, hwnd, NULL, NULL, NULL);
        SendMessageW(hGrp1, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hWin1 = CreateWindowExW(0, L"BUTTON", L"Win", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 35, 50, 20, hwnd, (HMENU)IDC_CK_TAP_WIN, NULL, NULL);
        HWND hCtrl1 = CreateWindowExW(0, L"BUTTON", L"Ctrl", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 85, 35, 50, 20, hwnd, (HMENU)IDC_CK_TAP_CTRL, NULL, NULL);
        HWND hAlt1 = CreateWindowExW(0, L"BUTTON", L"Alt", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 140, 35, 45, 20, hwnd, (HMENU)IDC_CK_TAP_ALT, NULL, NULL);
        HWND hShf1 = CreateWindowExW(0, L"BUTTON", L"Shift", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 190, 35, 55, 20, hwnd, (HMENU)IDC_CK_TAP_SHIFT, NULL, NULL);
        SendMessageW(hWin1, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCtrl1, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hAlt1, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hShf1, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        SendMessageW(hWin1, BM_SETCHECK, g_tempCustomTap.win ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hCtrl1, BM_SETCHECK, g_tempCustomTap.ctrl ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hAlt1, BM_SETCHECK, g_tempCustomTap.alt ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hShf1, BM_SETCHECK, g_tempCustomTap.shift ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND hLblKey1 = CreateWindowExW(0, L"STATIC", L"+  Key:", WS_CHILD | WS_VISIBLE, 255, 37, 45, 16, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblKey1, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hCmbKey1 = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 305, 33, 150, 200, hwnd, (HMENU)IDC_CMB_TAP_KEY, NULL, NULL);
        populateCombo(hCmbKey1, g_tempCustomTap.vkCode);

        // Section 2: Tier 1 Hold
        HWND hGrp2 = CreateWindowExW(0, L"BUTTON", L" Tier 1 Hold (1.25s - 2.5s) Custom Keystroke ",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 15, 85, 455, 65, hwnd, NULL, NULL, NULL);
        SendMessageW(hGrp2, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hWin2 = CreateWindowExW(0, L"BUTTON", L"Win", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 110, 50, 20, hwnd, (HMENU)IDC_CK_H1_WIN, NULL, NULL);
        HWND hCtrl2 = CreateWindowExW(0, L"BUTTON", L"Ctrl", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 85, 110, 50, 20, hwnd, (HMENU)IDC_CK_H1_CTRL, NULL, NULL);
        HWND hAlt2 = CreateWindowExW(0, L"BUTTON", L"Alt", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 140, 110, 45, 20, hwnd, (HMENU)IDC_CK_H1_ALT, NULL, NULL);
        HWND hShf2 = CreateWindowExW(0, L"BUTTON", L"Shift", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 190, 110, 55, 20, hwnd, (HMENU)IDC_CK_H1_SHIFT, NULL, NULL);
        SendMessageW(hWin2, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCtrl2, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hAlt2, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hShf2, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        SendMessageW(hWin2, BM_SETCHECK, g_tempCustomHold.win ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hCtrl2, BM_SETCHECK, g_tempCustomHold.ctrl ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hAlt2, BM_SETCHECK, g_tempCustomHold.alt ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hShf2, BM_SETCHECK, g_tempCustomHold.shift ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND hLblKey2 = CreateWindowExW(0, L"STATIC", L"+  Key:", WS_CHILD | WS_VISIBLE, 255, 112, 45, 16, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblKey2, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hCmbKey2 = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 305, 108, 150, 200, hwnd, (HMENU)IDC_CMB_H1_KEY, NULL, NULL);
        populateCombo(hCmbKey2, g_tempCustomHold.vkCode);

        // Section 3: Tier 2 Hold
        HWND hGrp3 = CreateWindowExW(0, L"BUTTON", L" Tier 2 Hold (2.5s+) Custom Keystroke ",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 15, 160, 455, 65, hwnd, NULL, NULL, NULL);
        SendMessageW(hGrp3, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hWin3 = CreateWindowExW(0, L"BUTTON", L"Win", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 185, 50, 20, hwnd, (HMENU)IDC_CK_H2_WIN, NULL, NULL);
        HWND hCtrl3 = CreateWindowExW(0, L"BUTTON", L"Ctrl", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 85, 185, 50, 20, hwnd, (HMENU)IDC_CK_H2_CTRL, NULL, NULL);
        HWND hAlt3 = CreateWindowExW(0, L"BUTTON", L"Alt", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 140, 185, 45, 20, hwnd, (HMENU)IDC_CK_H2_ALT, NULL, NULL);
        HWND hShf3 = CreateWindowExW(0, L"BUTTON", L"Shift", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 190, 185, 55, 20, hwnd, (HMENU)IDC_CK_H2_SHIFT, NULL, NULL);
        SendMessageW(hWin3, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCtrl3, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hAlt3, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hShf3, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        SendMessageW(hWin3, BM_SETCHECK, g_tempCustomHold2.win ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hCtrl3, BM_SETCHECK, g_tempCustomHold2.ctrl ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hAlt3, BM_SETCHECK, g_tempCustomHold2.alt ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hShf3, BM_SETCHECK, g_tempCustomHold2.shift ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND hLblKey3 = CreateWindowExW(0, L"STATIC", L"+  Key:", WS_CHILD | WS_VISIBLE, 255, 187, 45, 16, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblKey3, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hCmbKey3 = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 305, 183, 150, 200, hwnd, (HMENU)IDC_CMB_H2_KEY, NULL, NULL);
        populateCombo(hCmbKey3, g_tempCustomHold2.vkCode);

        // Buttons
        HWND hBtnOk = CreateWindowExW(0, L"BUTTON", L"Apply Keys", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 255, 240, 105, 28, hwnd, (HMENU)IDC_BTN_CK_OK, NULL, NULL);
        HWND hBtnCan = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 370, 240, 95, 28, hwnd, (HMENU)IDC_BTN_CK_CAN, NULL, NULL);
        SendMessageW(hBtnOk, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hBtnCan, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_CK_OK)
        {
            auto readComboVk = [&](int cmbId) -> uint8_t {
                int sel = static_cast<int>(SendMessageW(GetDlgItem(hwnd, cmbId), CB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < (int)(sizeof(g_keyItems)/sizeof(g_keyItems[0])))
                    return g_keyItems[sel].vk;
                return 0x7B; // F12
            };

            g_tempCustomTap.win = (SendMessageW(GetDlgItem(hwnd, IDC_CK_TAP_WIN), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomTap.ctrl = (SendMessageW(GetDlgItem(hwnd, IDC_CK_TAP_CTRL), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomTap.alt = (SendMessageW(GetDlgItem(hwnd, IDC_CK_TAP_ALT), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomTap.shift = (SendMessageW(GetDlgItem(hwnd, IDC_CK_TAP_SHIFT), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomTap.vkCode = readComboVk(IDC_CMB_TAP_KEY);

            g_tempCustomHold.win = (SendMessageW(GetDlgItem(hwnd, IDC_CK_H1_WIN), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomHold.ctrl = (SendMessageW(GetDlgItem(hwnd, IDC_CK_H1_CTRL), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomHold.alt = (SendMessageW(GetDlgItem(hwnd, IDC_CK_H1_ALT), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomHold.shift = (SendMessageW(GetDlgItem(hwnd, IDC_CK_H1_SHIFT), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomHold.vkCode = readComboVk(IDC_CMB_H1_KEY);

            g_tempCustomHold2.win = (SendMessageW(GetDlgItem(hwnd, IDC_CK_H2_WIN), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomHold2.ctrl = (SendMessageW(GetDlgItem(hwnd, IDC_CK_H2_CTRL), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomHold2.alt = (SendMessageW(GetDlgItem(hwnd, IDC_CK_H2_ALT), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomHold2.shift = (SendMessageW(GetDlgItem(hwnd, IDC_CK_H2_SHIFT), BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_tempCustomHold2.vkCode = readComboVk(IDC_CMB_H2_KEY);

            DestroyWindow(hwnd);
            return 0;
        }
        else if (id == IDC_BTN_CK_CAN || id == IDCANCEL)
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
        g_hCustomKeysWnd = NULL;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void showCustomKeysDialog(HWND hParent)
{
    if (g_hCustomKeysWnd)
    {
        SetForegroundWindow(g_hCustomKeysWnd);
        return;
    }

    int width = 500;
    int height = 320;
    RECT rcParent = {};
    GetWindowRect(hParent, &rcParent);
    int x = rcParent.left + (rcParent.right - rcParent.left - width) / 2;
    int y = rcParent.top + (rcParent.bottom - rcParent.top - height) / 2;

    g_hCustomKeysWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"XwaArm64CustomKeysWnd",
        L"Configure Custom Keystrokes",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, width, height,
        hParent, NULL, GetModuleHandleW(NULL), NULL
    );

    if (g_hCustomKeysWnd)
    {
        ShowWindow(g_hCustomKeysWnd, SW_SHOW);
        SetForegroundWindow(g_hCustomKeysWnd);
    }
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        HINSTANCE hInst = GetModuleHandleW(NULL);
        HICON hBig = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
        HICON hSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
        if (hBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hBig);
        if (hSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hSmall);

        // Group Box: Share Button
        HWND hGroup = CreateWindowExW(0, L"BUTTON", L" Xbox Wireless Controller (Share Button Settings) ",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 15, 10, 485, 450, hwnd, NULL, NULL, NULL);
        SendMessageW(hGroup, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Enable Checkbox
        HWND hChkEnable = CreateWindowExW(0, L"BUTTON", L"Enable Share Button Actions (Opt-in)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 32, 420, 20, hwnd, (HMENU)IDC_CHK_ENABLE, NULL, NULL);
        SendMessageW(hChkEnable, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Capture Provider Label & Combo
        HWND hLblProv = CreateWindowExW(0, L"STATIC", L"Capture Mode / Provider:",
            WS_CHILD | WS_VISIBLE, 30, 58, 300, 16, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblProv, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hCmbProv = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 30, 76, 450, 120, hwnd, (HMENU)IDC_CMB_PROVIDER, NULL, NULL);
        SendMessageW(hCmbProv, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCmbProv, CB_ADDSTRING, 0, (LPARAM)L"Auto-Detect (Steam Recording in Steam games, Game Bar otherwise)");
        SendMessageW(hCmbProv, CB_ADDSTRING, 0, (LPARAM)L"Steam Game Recording Only (F12 Screenshot / Ctrl+F11 Clip)");
        SendMessageW(hCmbProv, CB_ADDSTRING, 0, (LPARAM)L"Windows Game Bar Only (Win+Alt+PrtScn / Win+Alt+G)");
        SendMessageW(hCmbProv, CB_ADDSTRING, 0, (LPARAM)L"Custom Keybindings");

        // Tap Action Label & Combo
        HWND hLblTap = CreateWindowExW(0, L"STATIC", L"Single Tap Action (< 1.25s):",
            WS_CHILD | WS_VISIBLE, 30, 106, 300, 16, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblTap, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hCmbTap = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 30, 124, 450, 120, hwnd, (HMENU)IDC_CMB_TAP, NULL, NULL);
        SendMessageW(hCmbTap, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCmbTap, CB_ADDSTRING, 0, (LPARAM)L"Take Screenshot (Steam F12 / Game Bar Win+Alt+PrtScn)");
        SendMessageW(hCmbTap, CB_ADDSTRING, 0, (LPARAM)L"Add Marker in Steam Timeline (Ctrl + F11)");
        SendMessageW(hCmbTap, CB_ADDSTRING, 0, (LPARAM)L"Custom Keystroke");
        SendMessageW(hCmbTap, CB_ADDSTRING, 0, (LPARAM)L"Disabled");

        // Hold Action Label & Combo - Tier 1
        HWND hLblHold = CreateWindowExW(0, L"STATIC", L"Hold Action — Tier 1 (1.25s – 2.5s hold):",
            WS_CHILD | WS_VISIBLE, 30, 154, 300, 16, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblHold, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hCmbHold = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 30, 172, 450, 120, hwnd, (HMENU)IDC_CMB_HOLD, NULL, NULL);
        SendMessageW(hCmbHold, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCmbHold, CB_ADDSTRING, 0, (LPARAM)L"Save Recent Clip (Steam Clip / Game Bar Win+Alt+G 30s)");
        SendMessageW(hCmbHold, CB_ADDSTRING, 0, (LPARAM)L"Toggle Recording Start / Stop (Win + Alt + R)");
        SendMessageW(hCmbHold, CB_ADDSTRING, 0, (LPARAM)L"Custom Keystroke");
        SendMessageW(hCmbHold, CB_ADDSTRING, 0, (LPARAM)L"Disabled");

        // Hold Action Label & Combo - Tier 2
        HWND hLblHold2 = CreateWindowExW(0, L"STATIC", L"Hold Action — Tier 2 (2.5s+ hold):",
            WS_CHILD | WS_VISIBLE, 30, 202, 300, 16, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblHold2, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hCmbHold2 = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 30, 220, 450, 120, hwnd, (HMENU)IDC_CMB_HOLD2, NULL, NULL);
        SendMessageW(hCmbHold2, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCmbHold2, CB_ADDSTRING, 0, (LPARAM)L"Toggle Recording Start / Stop (Win + Alt + R)");
        SendMessageW(hCmbHold2, CB_ADDSTRING, 0, (LPARAM)L"Save Recent Clip (Steam Clip / Game Bar Win+Alt+G 30s)");
        SendMessageW(hCmbHold2, CB_ADDSTRING, 0, (LPARAM)L"Custom Keystroke");
        SendMessageW(hCmbHold2, CB_ADDSTRING, 0, (LPARAM)L"Disabled");

        // Haptic feedback Combo
        HWND hLblHaptic = CreateWindowExW(0, L"STATIC", L"Controller Haptic && LED Feedback:",
            WS_CHILD | WS_VISIBLE, 30, 250, 300, 16, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblHaptic, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hCmbHaptic = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 30, 268, 450, 120, hwnd, (HMENU)IDC_CMB_HAPTIC, NULL, NULL);
        SendMessageW(hCmbHaptic, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hCmbHaptic, CB_ADDSTRING, 0, (LPARAM)L"Advanced (Ascending ramps, hardware detent notches, LED blinks)");
        SendMessageW(hCmbHaptic, CB_ADDSTRING, 0, (LPARAM)L"Simple (Single confirmation buzz)");
        SendMessageW(hCmbHaptic, CB_ADDSTRING, 0, (LPARAM)L"Off (Silent / No vibration)");

        // Button to configure custom keystrokes
        HWND hBtnCustom = CreateWindowExW(0, L"BUTTON", L"Configure Custom Keystrokes...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 30, 300, 230, 26, hwnd, (HMENU)IDC_BTN_CUSTOM_KEYS, NULL, NULL);
        SendMessageW(hBtnCustom, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Hint text with generous height so it never cuts off
        HWND hLblHint = CreateWindowExW(0, L"STATIC",
            L"• Ramping vibration indicates hold progression; detents fire at Tier 1 (1.25s) and Tier 2 (2.5s).\n• Distinct pauses follow each detent. Release anytime after a detent to trigger.",
            WS_CHILD | WS_VISIBLE, 30, 336, 450, 42, hwnd, NULL, NULL, NULL);
        SendMessageW(hLblHint, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Auto-Start Checkbox
        HWND hChkAutoStart = CreateWindowExW(0, L"BUTTON", L"Start xwa-arm64 automatically with Windows (run in system tray)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 30, 388, 450, 20, hwnd, (HMENU)IDC_CHK_AUTOSTART, NULL, NULL);
        SendMessageW(hChkAutoStart, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        SendMessageW(hChkAutoStart, BM_SETCHECK, isAutoStartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

        // Bottom buttons
        HWND hBtnSave = CreateWindowExW(0, L"BUTTON", L"Save && Apply",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 280, 475, 110, 28, hwnd, (HMENU)IDC_BTN_SAVE, NULL, NULL);
        SendMessageW(hBtnSave, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        HWND hBtnCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 400, 475, 100, 28, hwnd, (HMENU)IDC_BTN_CANCEL, NULL, NULL);
        SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)g_hFont, TRUE);

        // Populate controls from current settings
        const Settings &s = getGlobalSettings();
        g_tempCustomTap = s.customTap;
        g_tempCustomHold = s.customHold;
        g_tempCustomHold2 = s.customHold2;

        SendMessageW(hChkEnable, BM_SETCHECK, s.enableShareButton ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hCmbProv, CB_SETCURSEL, static_cast<int>(s.provider), 0);
        SendMessageW(hCmbTap, CB_SETCURSEL, static_cast<int>(s.tapAction), 0);
        SendMessageW(hCmbHold, CB_SETCURSEL, static_cast<int>(s.holdAction), 0);
        SendMessageW(hCmbHold2, CB_SETCURSEL, static_cast<int>(s.hold2Action), 0);
        SendMessageW(hCmbHaptic, CB_SETCURSEL, static_cast<int>(s.hapticMode), 0);

        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == IDC_BTN_CUSTOM_KEYS)
        {
            showCustomKeysDialog(hwnd);
            return 0;
        }
        else if (id == IDC_BTN_SAVE)
        {
            Settings s = getGlobalSettings();
            s.enableShareButton = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_ENABLE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            s.provider = static_cast<CaptureProvider>(SendMessageW(GetDlgItem(hwnd, IDC_CMB_PROVIDER), CB_GETCURSEL, 0, 0));
            s.tapAction = static_cast<TapAction>(SendMessageW(GetDlgItem(hwnd, IDC_CMB_TAP), CB_GETCURSEL, 0, 0));
            s.holdAction = static_cast<HoldAction>(SendMessageW(GetDlgItem(hwnd, IDC_CMB_HOLD), CB_GETCURSEL, 0, 0));
            s.hold2Action = static_cast<HoldAction>(SendMessageW(GetDlgItem(hwnd, IDC_CMB_HOLD2), CB_GETCURSEL, 0, 0));
            s.hapticMode = static_cast<HapticMode>(SendMessageW(GetDlgItem(hwnd, IDC_CMB_HAPTIC), CB_GETCURSEL, 0, 0));

            s.customTap = g_tempCustomTap;
            s.customHold = g_tempCustomHold;
            s.customHold2 = g_tempCustomHold2;

            s.save();
            setGlobalSettings(s);

            bool autoStartCheck = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_AUTOSTART), BM_GETCHECK, 0, 0) == BST_CHECKED);
            setAutoStartEnabled(autoStartCheck);

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
            AppendMenuW(hMenu, MF_STRING | MF_DISABLED, IDM_HEADER, L"xwa-arm64 v1.0.0 (ARM64)");

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

            UINT autoStartFlags = MF_STRING | (isAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(hMenu, autoStartFlags, IDM_AUTOSTART, L"Start with Windows");

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
        else if (id == IDM_AUTOSTART)
        {
            bool currentlyEnabled = isAutoStartEnabled();
            setAutoStartEnabled(!currentlyEnabled);
            Log::info("Start with Windows toggled: %s", !currentlyEnabled ? "ENABLED" : "DISABLED");
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
    wc.lpszClassName = L"XwaArm64TrayWnd";
    RegisterClassExW(&wc);

    WNDCLASSEXW swc = {};
    swc.cbSize = sizeof(WNDCLASSEXW);
    swc.lpfnWndProc = SettingsWndProc;
    swc.hInstance = hInstance;
    swc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    swc.lpszClassName = L"XwaArm64SettingsWnd";
    RegisterClassExW(&swc);

    WNDCLASSEXW ckwc = {};
    ckwc.cbSize = sizeof(WNDCLASSEXW);
    ckwc.lpfnWndProc = CustomKeysWndProc;
    ckwc.hInstance = hInstance;
    ckwc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    ckwc.lpszClassName = L"XwaArm64CustomKeysWnd";
    RegisterClassExW(&ckwc);

    g_hTrayWnd = CreateWindowExW(0, L"XwaArm64TrayWnd", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
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
    HINSTANCE hInst = hInstance ? hInstance : GetModuleHandleW(NULL);
    HICON hCustomIcon = (HICON)LoadImageW(
        hInst,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR
    );
    if (!hCustomIcon)
    {
        hCustomIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON));
    }
    if (!hCustomIcon)
    {
        hCustomIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    }
    g_nid.hIcon = hCustomIcon;
    wcscpy_s(g_nid.szTip, L"xwa-arm64: Xbox Wireless Adapter (ARM64)");

    Shell_NotifyIconW(NIM_ADD, &g_nid);
    return true;
}

void SystemTray::updateStatus(bool dongleActive, bool controllerConnected, const std::string &batteryLevel)
{
    g_dongleActive = dongleActive;
    g_controllerConnected = controllerConnected;
    g_batteryLevel = batteryLevel;

    std::wstring tip = L"xwa-arm64: Xbox Wireless Adapter (ARM64)";
    if (controllerConnected)
    {
        std::wstring b(batteryLevel.begin(), batteryLevel.end());
        tip = L"xwa-arm64: Controller connected (" + b + L")";
    }
    else if (!dongleActive)
    {
        tip = L"xwa-arm64: Waiting for adapter...";
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

    int width = 535;
    int height = 555;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    g_hSettingsWnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"XwaArm64SettingsWnd",
        L"xwa-arm64 Settings",
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
