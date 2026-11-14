#pragma once

#include "imgui_renderer.h"
#include "settings.h"
#include "tray.h"
#include <chrono>
#include <functional>
#include <string>

class SettingsDialog
{
public:
    struct Callbacks
    {
        std::function<void(const Settings &, bool autoStart)> onSave;
        std::function<void()> onPair;
        std::function<void()> onWinUsbGuide;
        std::function<TrayStatusSnapshot()> getStatus;
    };

    SettingsDialog() = default;
    ~SettingsDialog();
    SettingsDialog(const SettingsDialog &) = delete;
    SettingsDialog &operator=(const SettingsDialog &) = delete;

    // Open a non-blocking modeless Settings window. Returns false if D3D11/Win32 creation failed.
    bool open(const Settings &current, bool autoStart, Callbacks callbacks);
    void close();
    bool isOpen() const;
    void focus();

    // Advance one frame from the main Win32 loop.
    void tick();

private:
    void renderFrame();
    void renderStatusBanner(float dpi);
    void renderGeneralTab(float dpi);
    void renderShareButtonTab(float dpi);
    void renderKeystrokesTab(float dpi);
    void renderKeyBindingCard(
        const char *title,
        const char *subtitle,
        const char *idSuffix,
        KeyBinding &binding,
        float dpi);

    ImGuiRenderer m_renderer;
    Callbacks m_callbacks;
    Settings m_working{};
    bool m_autoStart = false;
    bool m_open = false;
    std::string m_configPath;
    std::chrono::steady_clock::time_point m_copiedUntil{};
};
