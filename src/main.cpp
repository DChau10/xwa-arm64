#include "compat.h"
#include "utils/log.h"
#include "dongle/usb.h"
#include "dongle/dongle.h"
#include "dongle/driver_setup.h"
#include "ui/settings_dialog.h"
#include "ui/imgui_renderer.h"
#include "ui/tray.h"
#include "ui/settings.h"
#include "gamepad/vigem_gamepad.h"

#include <iostream>
#include <atomic>
#include <csignal>
#include <memory>
#include <mutex>
#include <condition_variable>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define XWA_ARM64_VERSION "1.0.0"

static std::atomic<bool> g_running{true};
static std::atomic<Dongle*> g_currentDongle{nullptr};
static std::atomic<bool> g_pairRequested{false};
static std::mutex g_dongleMutex;
static std::condition_variable g_dongleCv;
static std::atomic<bool> g_checkRequested{true};
static std::atomic<bool> g_isSuspended{false};

// On-demand Dear ImGui settings window managed on the main Win32 UI thread
static std::unique_ptr<SettingsDialog> g_settingsDialog;

#if defined(_WIN32)
static BOOL WINAPI consoleCtrlHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT)
    {
        Log::info("Shutdown signal received, closing xwa-arm64...");
        g_running = false;
        g_currentDongle.store(nullptr);
        g_dongleCv.notify_all();
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

    // Enable Per-Monitor V2 DPI awareness for sharp UI rendering
    ImGuiRenderer::enableProcessDpiAwareness();

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
            // UI thread: never touch the Dongle directly (its lifetime is owned by the
            // worker thread). Just flag it; the worker performs the pairing beacon write.
            if (g_currentDongle.load())
            {
                Log::info("Pairing mode requested via System Tray");
                g_pairRequested = true;
            }
            else
            {
                Log::info("Dongle not connected, cannot pair");
            }
        },
        []() {
            Log::info("Exit requested via System Tray");
            g_running = false;
            g_currentDongle.store(nullptr);
            g_dongleCv.notify_all();
            PostQuitMessage(0);
        },
        []() {
            Log::info("WinUSB driver setup requested via System Tray");
            showWinUsbAssistant(SystemTray::getInstance().getTrayWindow());
        }
    );

    // Register Settings callback (modeless ImGui dialog with fallback to legacy Win32)
    SystemTray::getInstance().setSettingsCallback([]() {
        if (g_settingsDialog && g_settingsDialog->isOpen())
        {
            g_settingsDialog->focus();
            return;
        }

        if (!g_settingsDialog)
        {
            g_settingsDialog = std::make_unique<SettingsDialog>();
        }

        SettingsDialog::Callbacks cb;
        cb.onSave = [](const Settings &updated, bool autoStart) {
            updated.save();
            setGlobalSettings(updated);
            setAutoStartEnabled(autoStart);
            Log::info("Settings saved and applied.");
        };
        cb.onPair = []() {
            if (g_currentDongle.load())
            {
                Log::info("Pairing mode requested via Settings window");
                g_pairRequested = true;
            }
            else
            {
                Log::info("Dongle not connected, cannot pair");
            }
        };
        cb.onWinUsbGuide = []() {
            showWinUsbAssistant(SystemTray::getInstance().getTrayWindow());
        };
        cb.getStatus = []() {
            return SystemTray::getInstance().getStatusSnapshot();
        };

        Log::info("Settings requested - opening ImGui settings window");
        if (!g_settingsDialog->open(getGlobalSettings(), isAutoStartEnabled(), std::move(cb)))
        {
            Log::error("ImGui settings dialog failed; falling back to legacy dialog");
            g_settingsDialog.reset();
            SystemTray::getInstance().showLegacySettingsDialog();
        }
    });

    // Register PnP arrival and power management callbacks
    SystemTray::getInstance().setDeviceChangeCallback([&](bool arrived) {
        if (arrived)
        {
            Log::debug("USB arrival event detected: waking dongle acquisition");
            g_checkRequested = true;
            g_dongleCv.notify_all();
        }
        else
        {
            // A PnP removal event fires for ANY USB device (mice, keyboards, phones),
            // not just the dongle. We must NOT tear the dongle session down here, or
            // unplugging a mouse would kill the wireless link. The USB bulk layer
            // already detects the real dongle loss and calls terminate() itself.
            // We only wake the acquisition loop so it can re-verify quickly.
            Log::debug("USB removal event detected: requesting re-check (not tearing down session)");
            g_checkRequested = true;
            g_dongleCv.notify_all();
        }
    });

    SystemTray::getInstance().setPowerChangeCallback([&](bool suspend) {
        if (suspend)
        {
            Log::info("Power management: entering suspend mode, releasing dongle resources");
            g_isSuspended = true;
            g_currentDongle.store(nullptr);
            g_dongleCv.notify_all();
        }
        else
        {
            Log::info("Power management: resumed from sleep, queuing adapter check");
            g_isSuspended = false;
            g_checkRequested = true;
            g_dongleCv.notify_all();
        }
    });

#if defined(_WIN32)
    if (showConsole)
    {
        SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
    }
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
            // If system is suspended (sleep / Modern Standby), wait until resumed
            if (g_isSuspended)
            {
                std::unique_lock<std::mutex> lock(g_dongleMutex);
                g_dongleCv.wait(lock, [&]() { return !g_isSuspended || !g_running; });
                if (!g_running) break;
                // Brief pause after resume to allow USB host controller to power rails
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            // 1. Verify driver status via SetupAPI
            AdapterDriverStatus driverStatus = checkAdapterDriverStatus();
            if (driverStatus == AdapterDriverStatus::DRIVER_MISSING)
            {
                SystemTray::getInstance().updateDriverState(DriverUiState::DriverMissing);
                std::unique_lock<std::mutex> lock(g_dongleMutex);
                g_dongleCv.wait_for(lock, std::chrono::seconds(10), [&]() {
                    return !g_running || g_isSuspended || g_checkRequested.load();
                });
                g_checkRequested = false;
                continue;
            }
            else if (driverStatus == AdapterDriverStatus::NOT_PRESENT)
            {
                SystemTray::getInstance().updateDriverState(DriverUiState::WaitingUnplugged);
                SystemTray::getInstance().updateStatus(false, false, "none");
                std::unique_lock<std::mutex> lock(g_dongleMutex);
                // Dormant wait: sleeps until Windows fires a USB device arrival or wake event!
                g_dongleCv.wait_for(lock, std::chrono::seconds(15), [&]() {
                    return !g_running || g_isSuspended || g_checkRequested.load();
                });
                g_checkRequested = false;
                continue;
            }

            // 2. Attempt to acquire USB device
            try
            {
                std::unique_ptr<UsbDevice> device = manager.tryGetDevice({
                    { DONGLE_VID, DONGLE_PID_SLIM },
                    { DONGLE_VID, DONGLE_PID_BULKY },
                    { DONGLE_VID, DONGLE_PID_SURFACE }
                }, [&]() {
                    Log::info("Dongle unplugged / disconnected");
                    SystemTray::getInstance().updateDriverState(DriverUiState::WaitingUnplugged);
                    g_currentDongle.store(nullptr);
                    g_checkRequested = true;
                    g_dongleCv.notify_all();
                });

                if (!device)
                {
                    SystemTray::getInstance().updateDriverState(DriverUiState::WaitingUnplugged);
                    std::unique_lock<std::mutex> lock(g_dongleMutex);
                    // Dormant wait: sleeps until Windows fires a USB device arrival or wake event!
                    // 15-second fallback ensures 0% CPU, no bus polling, and zero Modern Standby disruption.
                    g_dongleCv.wait_for(lock, std::chrono::seconds(15), [&]() {
                        return !g_running || g_isSuspended || g_checkRequested.load();
                    });
                    g_checkRequested = false;
                    continue;
                }

                g_checkRequested = false;

                Dongle dongle(std::move(device));
                g_currentDongle.store(&dongle);

                SystemTray::getInstance().updateDriverState(DriverUiState::Active);
                SystemTray::getInstance().updateStatus(true, false, "none");

                // Automatically enable pairing / reconnection window on acquire
                Log::info("Enabling automatic controller connection window...");
                dongle.setPairingStatus(true);
                requestPair = false;
                g_pairRequested = false;

                Log::info("Dongle is active and listening for controllers.");

                // Idle while the dongle is alive. The reader threads (spawned by the Dongle)
                // handle all input; this loop also services tray Pair requests on the thread
                // that owns the Dongle, avoiding any cross-thread use-after-free.
                while (g_running && g_currentDongle.load() && !g_isSuspended)
                {
                    if (g_pairRequested.exchange(false))
                    {
                        Log::info("Performing pairing beacon write (worker thread)");
                        dongle.setPairingStatus(true);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                g_currentDongle.store(nullptr);
                SystemTray::getInstance().updateDriverState(DriverUiState::WaitingUnplugged);
                SystemTray::getInstance().updateStatus(false, false, "none");

                // Debounce delay after disconnect: allows physical pin contact bounce
                // and Windows USB hub port reset/teardown to settle completely.
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                g_checkRequested = false;
            }
            catch (const std::exception &ex)
            {
                g_currentDongle.store(nullptr);
                SystemTray::getInstance().updateDriverState(DriverUiState::WaitingUnplugged);
                if (g_running && !g_isSuspended)
                {
                    Log::debug("Dongle notice: %s", ex.what());
                    std::unique_lock<std::mutex> lock(g_dongleMutex);
                    g_dongleCv.wait_for(lock, std::chrono::seconds(5), [&]() {
                        return !g_running || g_isSuspended || g_checkRequested.load();
                    });
                    g_checkRequested = false;
                }
            }
        }
    });

    if (requestSettings)
    {
        SystemTray::getInstance().showSettingsDialog();
    }

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

        if (g_settingsDialog && g_settingsDialog->isOpen())
        {
            g_settingsDialog->tick();
            if (!g_settingsDialog->isOpen())
            {
                g_settingsDialog.reset();
            }
        }
        else
        {
            MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
        }
    }

    if (g_settingsDialog)
    {
        g_settingsDialog->close();
        g_settingsDialog.reset();
    }

    g_running = false;
    g_currentDongle.store(nullptr);
    g_dongleCv.notify_all();
    if (dongleThread.joinable())
    {
        dongleThread.join();
    }

    SystemTray::getInstance().shutdown();
    Log::info("xwa-arm64 stopped.");
    return 0;
}
