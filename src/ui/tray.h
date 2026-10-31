#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <functional>

class SystemTray
{
public:
    using PairCallback = std::function<void()>;
    using ExitCallback = std::function<void()>;

    static SystemTray &getInstance();

    bool init(HINSTANCE hInstance, PairCallback onPair, ExitCallback onExit);
    void updateStatus(bool dongleActive, bool controllerConnected, const std::string &batteryLevel);
    void showSettingsDialog();
    void processMessages();
    void shutdown();

private:
    SystemTray() = default;
    ~SystemTray() = default;
};
