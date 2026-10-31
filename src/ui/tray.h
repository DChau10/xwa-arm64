#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <functional>

enum class DriverUiState {
    WaitingUnplugged,
    DriverMissing,
    Active,
    ControllerConnected
};

class SystemTray
{
public:
    using PairCallback = std::function<void()>;
    using ExitCallback = std::function<void()>;
    using InstallDriverCallback = std::function<void()>;

    static SystemTray &getInstance();

    bool init(HINSTANCE hInstance, PairCallback onPair, ExitCallback onExit, InstallDriverCallback onInstallDriver = nullptr);
    void updateStatus(bool dongleActive, bool controllerConnected, const std::string &batteryLevel);
    void updateDriverState(DriverUiState state);
    void showSettingsDialog();
    void processMessages();
    void shutdown();
    HWND getTrayWindow() const;

private:
    SystemTray() = default;
    ~SystemTray() = default;
};
