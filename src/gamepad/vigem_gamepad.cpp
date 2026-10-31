#include "vigem_gamepad.h"
#include "../utils/log.h"
#include "../compat.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <fstream>
#include <vector>
#include <string>
#include <ViGEm/Client.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")


static void CALLBACK vigemNotificationCallback(
    PVIGEM_CLIENT client,
    PVIGEM_TARGET target,
    UCHAR largeMotor,
    UCHAR smallMotor,
    UCHAR ledNumber,
    LPVOID userData
)
{
    auto* gamepad = static_cast<ViGEmGamepad*>(userData);
    if (gamepad)
    {
        gamepad->onNotification(largeMotor, smallMotor);
    }
}

ViGEmGamepad::ViGEmGamepad()
{
    client = vigem_alloc();
    if (!client)
    {
        Log::error("Failed to allocate ViGEm client");
        return;
    }

    VIGEM_ERROR error = vigem_connect(client);
    if (!VIGEM_SUCCESS(error))
    {
        Log::error("Failed to connect to ViGEmBus: 0x%08X (Is ViGEmBus installed?)", error);
        vigem_free(client);
        client = nullptr;
    }
}

ViGEmGamepad::~ViGEmGamepad()
{
    disconnect();

    if (client)
    {
        vigem_disconnect(client);
        vigem_free(client);
        client = nullptr;
    }
}

bool ViGEmGamepad::connect(uint16_t vendorId, uint16_t productId)
{
    if (!client)
    {
        Log::error("Cannot connect virtual gamepad: ViGEm client unavailable");
        return false;
    }

    if (connected)
    {
        return true;
    }

    target = vigem_target_x360_alloc();
    if (!target)
    {
        Log::error("Failed to allocate virtual Xbox 360 controller target");
        return false;
    }

    VIGEM_ERROR error = vigem_target_add(client, target);
    if (!VIGEM_SUCCESS(error))
    {
        Log::error("Failed to plug in virtual controller: 0x%08X", error);
        vigem_target_free(target);
        target = nullptr;
        return false;
    }

    // Register rumble force-feedback notification callback
    vigem_target_x360_register_notification(client, target, &vigemNotificationCallback, this);

    connected = true;
    Log::info("Virtual Xbox 360 controller plugged in successfully (ViGEmBus)");
    return true;
}

void ViGEmGamepad::disconnect()
{
    if (!connected || !client || !target)
    {
        return;
    }

    vigem_target_x360_unregister_notification(target);
    vigem_target_remove(client, target);
    vigem_target_free(target);
    target = nullptr;
    connected = false;

    Log::info("Virtual Xbox 360 controller unplugged");
}

void ViGEmGamepad::updateState(const GamepadState &state)
{
    if (!connected || !client || !target)
    {
        return;
    }

    XUSB_REPORT report = {};

    // Digital Buttons bitmask
    if (state.dpadUp) report.wButtons |= XUSB_GAMEPAD_DPAD_UP;
    if (state.dpadDown) report.wButtons |= XUSB_GAMEPAD_DPAD_DOWN;
    if (state.dpadLeft) report.wButtons |= XUSB_GAMEPAD_DPAD_LEFT;
    if (state.dpadRight) report.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT;
    if (state.start) report.wButtons |= XUSB_GAMEPAD_START;
    if (state.back) report.wButtons |= XUSB_GAMEPAD_BACK;
    if (state.stickLeft) report.wButtons |= XUSB_GAMEPAD_LEFT_THUMB;
    if (state.stickRight) report.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB;
    if (state.bumperLeft) report.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (state.bumperRight) report.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    if (state.guide) report.wButtons |= XUSB_GAMEPAD_GUIDE;
    if (state.a) report.wButtons |= XUSB_GAMEPAD_A;
    if (state.b) report.wButtons |= XUSB_GAMEPAD_B;
    if (state.x) report.wButtons |= XUSB_GAMEPAD_X;
    if (state.y) report.wButtons |= XUSB_GAMEPAD_Y;

    // Triggers: Map GIP 10-bit range (0-1023) to XUSB 8-bit range (0-255)
    report.bLeftTrigger = static_cast<BYTE>(state.triggerLeft / 4);
    report.bRightTrigger = static_cast<BYTE>(state.triggerRight / 4);

    // Sticks: 16-bit signed range (-32768 to 32767)
    report.sThumbLX = state.thumbLX;
    report.sThumbLY = state.thumbLY;
    report.sThumbRX = state.thumbRX;
    report.sThumbRY = state.thumbRY;

    VIGEM_ERROR err = vigem_target_x360_update(client, target, report);
    if (!VIGEM_SUCCESS(err))
    {
        static bool errorLogged = false;
        if (!errorLogged)
        {
            Log::error("vigem_target_x360_update failed: 0x%08X", err);
            errorLogged = true;
        }
    }
}

void ViGEmGamepad::setRumbleCallback(RumbleCallback callback)
{
    std::lock_guard<std::mutex> lock(callbackMutex);
    rumbleCallback = std::move(callback);
}

void ViGEmGamepad::onNotification(uint8_t largeMotor, uint8_t smallMotor)
{
    std::lock_guard<std::mutex> lock(callbackMutex);
    if (rumbleCallback)
    {
        rumbleCallback(largeMotor, smallMotor);
    }
}

bool ViGEmGamepad::isConnected() const
{
    return connected;
}

bool ViGEmGamepad::isDriverInstalled()
{
    PVIGEM_CLIENT testClient = vigem_alloc();
    if (!testClient) return false;

    VIGEM_ERROR err = vigem_connect(testClient);
    if (VIGEM_SUCCESS(err))
    {
        vigem_disconnect(testClient);
        vigem_free(testClient);
        return true;
    }

    vigem_free(testClient);
    return false;
}

bool ViGEmGamepad::ensureDriverInstalled(void *hParent)
{
    if (isDriverInstalled())
    {
        return true;
    }

    Log::info("ViGEmBus driver not detected on system. Prompting user for installation...");

    HWND hwndParent = static_cast<HWND>(hParent);

    // Initialize common controls
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    // Prompt user with TaskDialog or fallback to MessageBox
    TASKDIALOGCONFIG tc = { sizeof(tc) };
    tc.hwndParent = hwndParent;
    tc.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
    tc.pszWindowTitle = L"xwa-arm64";
    tc.pszMainIcon = TD_INFORMATION_ICON;
    tc.pszMainInstruction = L"ViGEmBus Driver Required";
    tc.pszContent = L"xwa-arm64 requires the official ViGEmBus driver (Virtual Gamepad Emulation Bus) to emulate Xbox controllers in Windows.\n\nViGEmBus was not detected on this system.";

    TASKDIALOG_BUTTON buttons[] = {
        { 101, L"Download and Install ViGEmBus Automatically (Recommended)\nDownloads official installer from GitHub, verifies cryptographic SHA-256 hash, and runs setup." },
        { 102, L"Open ViGEmBus GitHub Releases in Web Browser\nDownload and run the official installer package yourself." },
        { IDCANCEL, L"Exit xwa-arm64" }
    };
    tc.pButtons = buttons;
    tc.cButtons = 3;
    tc.nDefaultButton = 101;

    int buttonPressed = 0;
    HRESULT hr = E_FAIL;
    typedef HRESULT (WINAPI *PFN_TaskDialogIndirect)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    HMODULE hComCtl = LoadLibraryW(L"comctl32.dll");
    if (hComCtl)
    {
        PFN_TaskDialogIndirect pfnTaskDialog = (PFN_TaskDialogIndirect)GetProcAddress(hComCtl, "TaskDialogIndirect");
        if (pfnTaskDialog)
        {
            hr = pfnTaskDialog(&tc, &buttonPressed, NULL, NULL);
        }
        FreeLibrary(hComCtl);
    }
    if (FAILED(hr))
    {
        int res = MessageBoxW(hwndParent,
            L"xwa-arm64 requires the ViGEmBus driver to emulate Xbox controllers in Windows.\n\n"
            L"Would you like xwa-arm64 to automatically download and install ViGEmBus now?\n\n"
            L"• Click 'Yes' to download and install automatically.\n"
            L"• Click 'No' to open the ViGEmBus GitHub release page in your browser.\n"
            L"• Click 'Cancel' to exit.",
            L"xwa-arm64 — ViGEmBus Required",
            MB_YESNOCANCEL | MB_ICONQUESTION);

        if (res == IDYES) buttonPressed = 101;
        else if (res == IDNO) buttonPressed = 102;
        else return false;
    }

    if (buttonPressed == 102)
    {
        ShellExecuteW(NULL, L"open", L"https://github.com/nefarius/ViGEmBus/releases", NULL, NULL, SW_SHOWNORMAL);
        return false;
    }
    else if (buttonPressed != 101)
    {
        return false;
    }

    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string installerPath = std::string(tempPath) + "ViGEmBus_Setup.exe";

    Log::info("Fetching official ViGEmBus installer from GitHub...");

    std::string dlCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \""
        "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 -bor [Net.SecurityProtocolType]::Tls13; "
        "$u='https://github.com/nefarius/ViGEmBus/releases/download/v1.22.0/ViGEmBus_1.22.0_x64_x86_arm64.exe'; "
        "Invoke-WebRequest -Uri $u -OutFile '" + installerPath + "' -UseBasicParsing;\"";

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::vector<char> cmdBuf(dlCmd.begin(), dlCmd.end());
    cmdBuf.push_back('\0');

    bool downloadOk = false;
    if (CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 60000);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        downloadOk = (exitCode == 0 && GetFileAttributesA(installerPath.c_str()) != INVALID_FILE_ATTRIBUTES);
    }

    if (!downloadOk)
    {
        MessageBoxW(hwndParent, L"Failed to download the ViGEmBus installer.\nPlease check your network connection or download it manually from GitHub.", L"Download Error", MB_ICONERROR);
        return false;
    }

    // Read and verify SHA-256
    std::ifstream instFile(installerPath, std::ios::binary | std::ios::ate);
    if (!instFile)
    {
        DeleteFileA(installerPath.c_str());
        MessageBoxW(hwndParent, L"Failed to open downloaded installer file.", L"Error", MB_ICONERROR);
        return false;
    }

    size_t instSize = static_cast<size_t>(instFile.tellg());
    std::vector<uint8_t> instBuf(instSize);
    instFile.seekg(0, std::ios::beg);
    instFile.read(reinterpret_cast<char*>(instBuf.data()), instSize);
    instFile.close();

    std::string actualHash;
    const std::string expectedHash = "89220a7865076b342892f98865f3499fb7c4cfd673159e89d352c360fd014c6a";

    if (!calculateSha256(instBuf.data(), instBuf.size(), actualHash) || _stricmp(actualHash.c_str(), expectedHash.c_str()) != 0)
    {
        DeleteFileA(installerPath.c_str());
        MessageBoxW(hwndParent, L"SECURITY ERROR: The downloaded ViGEmBus installer failed SHA-256 checksum verification.\nInstallation aborted to protect your device.", L"Security Alert", MB_ICONERROR);
        return false;
    }

    Log::info("ViGEmBus installer verified (SHA-256: %s). Launching setup with elevation...", actualHash.c_str());

    std::wstring wInstallerPath(installerPath.begin(), installerPath.end());
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = wInstallerPath.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei) && sei.hProcess)
    {
        WaitForSingleObject(sei.hProcess, INFINITE);
        CloseHandle(sei.hProcess);
    }

    DeleteFileA(installerPath.c_str());

    if (isDriverInstalled())
    {
        Log::info("ViGEmBus driver successfully installed and operational.");
        MessageBoxW(hwndParent, L"ViGEmBus was installed successfully!\n\nxwa-arm64 is now ready to emulate Xbox controllers.", L"ViGEmBus Installed", MB_ICONINFORMATION);
        return true;
    }
    else
    {
        Log::info("ViGEmBus setup was cancelled or incomplete.");
        MessageBoxW(hwndParent, L"ViGEmBus driver setup was not completed.\nxwa-arm64 cannot emulate controllers without ViGEmBus.", L"Installation Incomplete", MB_ICONWARNING);
        return false;
    }
}
