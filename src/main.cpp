#include "compat.h"
#include "utils/log.h"
#include "dongle/usb.h"
#include "dongle/dongle.h"
#include "dongle/driver_setup.h"
#include "ui/settings.h"
#include "ui/tray.h"
#include "gamepad/vigem_gamepad.h"

#include <iostream>
#include <atomic>
#include <csignal>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define XWA_ARM64_VERSION "1.0.0"

static std::atomic<bool> g_running{true};
static Dongle* g_currentDongle = nullptr;

#if defined(_WIN32)
static BOOL WINAPI consoleCtrlHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT)
    {
        Log::info("Shutdown signal received, closing xwa-arm64...");
        g_running = false;
        return TRUE;
    }
    return FALSE;
}
#else
static void signalHandler(int signal)
{
    Log::info("Shutdown signal received...");
    g_running = false;
}
#endif

int main(int argc, char *argv[])
{
    bool requestPair = false;
    bool requestSettings = false;
    bool showConsole = false;
    bool showHelp = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--console" || arg == "-c")
        {
            showConsole = true;
        }
        else if (arg == "--pair" || arg == "-p")
        {
            requestPair = true;
        }
        else if (arg == "--settings" || arg == "-s")
        {
            requestSettings = true;
        }
        else if (arg == "--hide")
        {
            // Backwards-compatible no-op: quiet GUI is now the default
        }
        else if (arg == "--help" || arg == "-h")
        {
            showHelp = true;
        }
    }

#if defined(_WIN32)
    if (showConsole || showHelp)
    {
        DWORD fileType = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
        bool isRedirected = (fileType == FILE_TYPE_DISK || fileType == FILE_TYPE_PIPE);

        if (!isRedirected)
        {
            if (!AttachConsole(ATTACH_PARENT_PROCESS))
            {
                AllocConsole();
            }
            FILE* fpOut = nullptr;
            FILE* fpErr = nullptr;
            freopen_s(&fpOut, "CONOUT$", "w", stdout);
            freopen_s(&fpErr, "CONOUT$", "w", stderr);
        }
        std::cout.clear();
        std::cerr.clear();
    }
#endif

    Log::init();

    if (showHelp)
    {
        std::cout << "\n==================================================\n"
                  << "  xwa-arm64 v" << XWA_ARM64_VERSION << " (Xbox Wireless Adapter for Windows ARM64)\n"
                  << "==================================================\n"
                  << "Usage: xwa-arm64 [OPTIONS]\n\n"
                  << "Options:\n"
                  << "  -c, --console   Show real-time diagnostic console and log output\n"
                  << "  -p, --pair      Enable wireless pairing mode on startup\n"
                  << "  -s, --settings  Open settings window on startup\n"
                  << "  -h, --help      Display this help message\n\n";
        return 0;
    }

    if (showConsole)
    {
        Log::info("==================================================");
        Log::info("  xwa-arm64 v%s (Xbox Wireless Adapter for Windows ARM64)", XWA_ARM64_VERSION);
        Log::info("  Derived from xow by Severin v. W. / Medusalix");
        Log::info("==================================================");
    }

    // 0. Verify ViGEmBus installation (prompts for one-click install if missing)
    if (!ViGEmGamepad::ensureDriverInstalled())
    {
        return 1;
    }

    // 1. Load configuration
    setGlobalSettings(Settings::load());

    // 2. Initialize System Tray
    SystemTray::getInstance().init(
        GetModuleHandle(NULL),
        []() {
            if (g_currentDongle)
            {
                Log::info("Pairing mode initiated via System Tray");
                g_currentDongle->setPairingStatus(true);
            }
            else
            {
                Log::info("Dongle not connected, cannot pair");
            }
        },
        []() {
            Log::info("Exit requested via System Tray");
            g_running = false;
            PostQuitMessage(0);
        },
        []() {
            Log::info("WinUSB driver setup requested via System Tray");
            showWinUsbAssistant(SystemTray::getInstance().getTrayWindow());
        }
    );

    if (requestSettings)
    {
        SystemTray::getInstance().showSettingsDialog();
    }

#if defined(_WIN32)
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
#else
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#endif

    // Check adapter driver status on startup
    AdapterDriverStatus initialDriverStatus = checkAdapterDriverStatus();
    if (initialDriverStatus == AdapterDriverStatus::DRIVER_MISSING)
    {
        Log::info("Xbox Wireless Adapter detected, but required WinUSB driver is missing (Code 28).");
        SystemTray::getInstance().updateDriverState(DriverUiState::DriverMissing);
        showWinUsbAssistant(SystemTray::getInstance().getTrayWindow());
    }

    // Launch dongle acquisition & controller handling in a dedicated worker thread
    std::thread dongleThread([&]() {
        UsbDeviceManager manager;

        while (g_running)
        {
            // 1. Verify driver status via SetupAPI
            AdapterDriverStatus driverStatus = checkAdapterDriverStatus();
            if (driverStatus == AdapterDriverStatus::DRIVER_MISSING)
            {
                SystemTray::getInstance().updateDriverState(DriverUiState::DriverMissing);
                for (int i = 0; i < 20 && g_running; ++i)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                continue;
            }

            // 2. Attempt to acquire USB device
            try
            {
                std::unique_ptr<UsbDevice> device = manager.tryGetDevice({
                    { DONGLE_VID, DONGLE_PID_OLD },
                    { DONGLE_VID, DONGLE_PID_NEW },
                    { DONGLE_VID, DONGLE_PID_ORIGINAL },
                    { DONGLE_VID, DONGLE_PID_SURFACE }
                }, [&]() {
                    Log::info("Dongle unplugged / disconnected");
                    SystemTray::getInstance().updateDriverState(DriverUiState::WaitingUnplugged);
                    g_currentDongle = nullptr;
                });

                if (!device)
                {
                    SystemTray::getInstance().updateDriverState(DriverUiState::WaitingUnplugged);
                    for (int i = 0; i < 10 && g_running; ++i)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    continue;
                }

                Dongle dongle(std::move(device));
                g_currentDongle = &dongle;

                SystemTray::getInstance().updateDriverState(DriverUiState::Active);
                SystemTray::getInstance().updateStatus(true, false, "none");

                // Automatically enable pairing / reconnection window on startup (PR #176)
                Log::info("Enabling automatic controller connection window...");
                dongle.setPairingStatus(true);
                requestPair = false;

                Log::info("Dongle is active and listening for controllers.");

                while (g_running && g_currentDongle)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                g_currentDongle = nullptr;
            }
            catch (const std::exception &ex)
            {
                g_currentDongle = nullptr;
                SystemTray::getInstance().updateDriverState(DriverUiState::WaitingUnplugged);
                if (g_running)
                {
                    Log::debug("Dongle notice: %s", ex.what());
                    for (int i = 0; i < 20 && g_running; ++i)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }
            }
        }
    });

    // Main thread owns the Win32 message loop - UI is ALWAYS responsive!
    MSG msg;
    while (g_running)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                g_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!g_running)
        {
            break;
        }

        MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
    }

    g_running = false;
    if (dongleThread.joinable())
    {
        dongleThread.join();
    }

    SystemTray::getInstance().shutdown();
    Log::info("xwa-arm64 stopped.");
    return 0;
}
