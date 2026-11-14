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

struct TrayStatusSnapshot {
    DriverUiState driverState = DriverUiState::WaitingUnplugged;
    bool dongleActive = false;
    bool controllerConnected = false;
    std::string batteryLevel = "none";
};

class SystemTray
{
public:
    using PairCallback = std::function<void()>;
    using ExitCallback = std::function<void()>;
    using InstallDriverCallback = std::function<void()>;
    using SettingsCallback = std::function<void()>;
    using DeviceChangeCallback = std::function<void(bool arrived)>;
    using PowerChangeCallback = std::function<void(bool suspend)>;

    static SystemTray &getInstance();

    bool init(HINSTANCE hInstance, PairCallback onPair, ExitCallback onExit, 
              InstallDriverCallback onInstallDriver = nullptr, SettingsCallback onSettings = nullptr);
    void setDeviceChangeCallback(DeviceChangeCallback callback);
    void setPowerChangeCallback(PowerChangeCallback callback);
    void setSettingsCallback(SettingsCallback callback);
    void updateStatus(bool dongleActive, bool controllerConnected, const std::string &batteryLevel);
    void updateDriverState(DriverUiState state);
    TrayStatusSnapshot getStatusSnapshot() const;
    void showSettingsDialog();
    void showLegacySettingsDialog();
    void processMessages();
    void shutdown();

    HWND getTrayWindow() const;

private:
    SystemTray() = default;
    ~SystemTray() = default;
};

// ─── Free functions (exposed for main.cpp / settings UI) ────────
bool isAutoStartEnabled();
bool setAutoStartEnabled(bool enable);
