#include "compat.h"
#include "utils/log.h"
#include "dongle/usb.h"
#include "dongle/dongle.h"

#include <iostream>
#include <atomic>
#include <csignal>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define XOW_WIN_VERSION "1.0.0"

static std::atomic<bool> g_running{true};
static Dongle* g_currentDongle = nullptr;

#if defined(_WIN32)
static BOOL WINAPI consoleCtrlHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT)
    {
        Log::info("Shutdown signal received, closing xow-win...");
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
    Log::init();

    Log::info("==================================================");
    Log::info("  xow-win v%s (Xbox Wireless Adapter for Windows)", XOW_WIN_VERSION);
    Log::info("  Derived from xow by Severin v. W. / Medusalix");
    Log::info("==================================================");

    bool requestPair = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--pair" || arg == "-p")
        {
            requestPair = true;
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cout << "Usage: xow-win [OPTIONS]\n"
                      << "Options:\n"
                      << "  -p, --pair      Enable wireless pairing mode on startup\n"
                      << "  -h, --help      Display this help message\n";
            return 0;
        }
    }

#if defined(_WIN32)
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
#else
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#endif

    UsbDeviceManager manager;

    while (g_running)
    {
        try
        {
            std::unique_ptr<UsbDevice> device = manager.getDevice({
                { DONGLE_VID, DONGLE_PID_OLD },
                { DONGLE_VID, DONGLE_PID_NEW },
                { DONGLE_VID, DONGLE_PID_ORIGINAL },
                { DONGLE_VID, DONGLE_PID_SURFACE }
            }, [&]() {
                Log::info("Dongle unplugged / disconnected");
            });

            if (!device || !g_running)
            {
                break;
            }

            Dongle dongle(std::move(device));
            g_currentDongle = &dongle;

            if (requestPair)
            {
                Log::info("Enabling pairing mode...");
                dongle.setPairingStatus(true);
                requestPair = false; // only on first launch
            }

            Log::info("Dongle is active and listening for controllers.");

            // Keep main thread alive while dongle is running
            while (g_running)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }

            g_currentDongle = nullptr;
        }
        catch (const std::exception &ex)
        {
            g_currentDongle = nullptr;
            if (g_running)
            {
                Log::error("Error: %s", ex.what());
                Log::info("Retrying in 2 seconds...");
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        }
    }

    Log::info("xow-win stopped.");
    return 0;
}
