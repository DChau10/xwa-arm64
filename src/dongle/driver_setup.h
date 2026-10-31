#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

enum class AdapterDriverStatus {
    NOT_PRESENT,
    DRIVER_MISSING,  // Plugged in but lacks WinUSB driver (e.g. Code 28 or unconfigured)
    WINUSB_READY     // Plugged in and bound to WinUSB.sys
};

// Check connected hardware status via SetupAPI
AdapterDriverStatus checkAdapterDriverStatus(std::wstring *outDesc = nullptr, std::wstring *outHardwareId = nullptr);

// Shows the persistent floating WinUSB Setup Assistant dialog alongside Device Manager
void showWinUsbAssistant(HWND hParent = NULL);
