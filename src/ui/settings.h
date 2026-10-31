#pragma once

#include <string>
#include <cstdint>

enum class CaptureProvider : int
{
    Auto = 0,       // Steam in Steam games, Game Bar in others
    SteamOnly = 1,
    GameBarOnly = 2,
    Custom = 3
};

enum class TapAction : int
{
    Screenshot = 0,         // Steam F12 / Game Bar Win+Alt+PrtScn
    SteamMarker = 1,        // Steam Timeline Marker (Ctrl+F11)
    CustomKey = 2,
    Disabled = 3
};

enum class HoldAction : int
{
    RecordClip = 0,         // Steam Save Clip / Game Bar Record Last 30s (Win+Alt+G)
    ToggleRecording = 1,    // Game Bar / Steam Record Toggle
    CustomKey = 2,
    Disabled = 3
};

struct KeyBinding
{
    uint8_t vkCode = 0;
    bool win = false;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
};

struct Settings
{
    bool enableShareButton = false; // Opt-in by default
    CaptureProvider provider = CaptureProvider::Auto;
    TapAction tapAction = TapAction::Screenshot;
    HoldAction holdAction = HoldAction::RecordClip;
    int holdDurationMs = 800;
    bool hapticFeedback = true;

    // Default Keybindings
    KeyBinding steamScreenshot = { 0x7B, false, false, false, false };       // VK_F12
    KeyBinding steamClip = { 0x7A, false, true, false, false };             // Ctrl + VK_F11
    KeyBinding gameBarScreenshot = { 0x2C, true, false, true, false };      // Win + Alt + VK_SNAPSHOT
    KeyBinding gameBarClip = { 0x47, true, false, true, false };            // Win + Alt + 'G'
    KeyBinding gameBarToggle = { 0x52, true, false, true, false };          // Win + Alt + 'R'
    KeyBinding customTap = { 0x7B, false, false, false, false };            // VK_F12
    KeyBinding customHold = { 0x47, true, false, true, false };             // Win + Alt + 'G'

    static std::string getDefaultConfigPath();
    static Settings load(const std::string &path = "");
    bool save(const std::string &path = "") const;
};

// Global settings instance accessible across threads
Settings &getGlobalSettings();
void setGlobalSettings(const Settings &s);
