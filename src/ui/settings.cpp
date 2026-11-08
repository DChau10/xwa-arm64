#include "settings.h"
#include "../utils/log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <mutex>

static Settings g_settings;
static std::mutex g_settingsMutex;

Settings &getGlobalSettings()
{
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    return g_settings;
}

void setGlobalSettings(const Settings &s)
{
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings = s;
}

std::string Settings::getDefaultConfigPath()
{
    char localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData)))
    {
        std::string dir = std::string(localAppData) + "\\xwa-arm64";
        CreateDirectoryA(dir.c_str(), NULL);
        return dir + "\\settings.json";
    }

    return "settings.json";
}

static std::string trim(const std::string &s)
{
    size_t start = s.find_first_not_of(" \t\r\n\"");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n\"");
    return s.substr(start, end - start + 1);
}

// Tolerant integer parse: returns `def` on any failure (empty / non-numeric /
// overflow) instead of throwing std::invalid_argument / std::out_of_range, which
// would otherwise crash the app at startup on a malformed or truncated settings.json.
static int safeStoi(const std::string &s, int def)
{
    try
    {
        if (s.empty()) return def;
        return std::stoi(s);
    }
    catch (...)
    {
        return def;
    }
}

Settings Settings::load(const std::string &configPath)
{
    Settings s;
    std::string path = configPath.empty() ? getDefaultConfigPath() : configPath;

    // Check Windows Registry for GameDVR default overrides
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD keyVal = 0, modVal = 0, size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"CustomShortcut_TakeScreenshotKey", NULL, NULL, (LPBYTE)&keyVal, &size) == ERROR_SUCCESS && keyVal != 0)
        {
            size = sizeof(DWORD);
            RegQueryValueExW(hKey, L"CustomShortcut_TakeScreenshotModifiers", NULL, NULL, (LPBYTE)&modVal, &size);
            s.gameBarScreenshot.vkCode = static_cast<uint8_t>(keyVal);
            s.gameBarScreenshot.win = (modVal & 0x08) != 0;
            s.gameBarScreenshot.alt = (modVal & 0x01) != 0;
            s.gameBarScreenshot.ctrl = (modVal & 0x02) != 0;
            s.gameBarScreenshot.shift = (modVal & 0x04) != 0;
        }

        size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"CustomShortcut_RecordPreviousKey", NULL, NULL, (LPBYTE)&keyVal, &size) == ERROR_SUCCESS && keyVal != 0)
        {
            size = sizeof(DWORD);
            RegQueryValueExW(hKey, L"CustomShortcut_RecordPreviousModifiers", NULL, NULL, (LPBYTE)&modVal, &size);
            s.gameBarClip.vkCode = static_cast<uint8_t>(keyVal);
            s.gameBarClip.win = (modVal & 0x08) != 0;
            s.gameBarClip.alt = (modVal & 0x01) != 0;
            s.gameBarClip.ctrl = (modVal & 0x02) != 0;
            s.gameBarClip.shift = (modVal & 0x04) != 0;
        }

        size = sizeof(DWORD);
        if (RegQueryValueExW(hKey, L"CustomShortcut_ToggleRecordingKey", NULL, NULL, (LPBYTE)&keyVal, &size) == ERROR_SUCCESS && keyVal != 0)
        {
            size = sizeof(DWORD);
            RegQueryValueExW(hKey, L"CustomShortcut_ToggleRecordingModifiers", NULL, NULL, (LPBYTE)&modVal, &size);
            s.gameBarToggle.vkCode = static_cast<uint8_t>(keyVal);
            s.gameBarToggle.win = (modVal & 0x08) != 0;
            s.gameBarToggle.alt = (modVal & 0x01) != 0;
            s.gameBarToggle.ctrl = (modVal & 0x02) != 0;
            s.gameBarToggle.shift = (modVal & 0x04) != 0;
        }

        RegCloseKey(hKey);
    }

    std::ifstream file(path);
    if (!file.is_open())
    {
        Log::info("Generating initial settings file at %s", path.c_str());
        s.save(path);
        return s;
    }

    std::string line;
    while (std::getline(file, line))
    {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim(line.substr(0, colon));
        std::string val = trim(line.substr(colon + 1));
        if (!val.empty() && val.back() == ',') val.pop_back();
        val = trim(val);

        // Every numeric parse goes through safeStoi so a malformed/truncated value
        // falls back to the field's default instead of throwing and crashing startup.
        if (key == "enableShareButton") s.enableShareButton = (val == "true" || val == "1");
        else if (key == "provider") s.provider = static_cast<CaptureProvider>(safeStoi(val, static_cast<int>(s.provider)));
        else if (key == "tapAction") s.tapAction = static_cast<TapAction>(safeStoi(val, static_cast<int>(s.tapAction)));
        else if (key == "holdAction") s.holdAction = static_cast<HoldAction>(safeStoi(val, static_cast<int>(s.holdAction)));
        else if (key == "hold2Action") s.hold2Action = static_cast<HoldAction>(safeStoi(val, static_cast<int>(s.hold2Action)));
        else if (key == "hapticMode") s.hapticMode = static_cast<HapticMode>(safeStoi(val, static_cast<int>(s.hapticMode)));
        else if (key == "holdDurationMs") s.holdDurationMs = safeStoi(val, s.holdDurationMs);
        else if (key == "hold2DurationMs") s.hold2DurationMs = safeStoi(val, s.hold2DurationMs);
        else if (key == "customTapVk") s.customTap.vkCode = static_cast<uint8_t>(safeStoi(val, s.customTap.vkCode));
        else if (key == "customTapWin") s.customTap.win = (val == "true" || val == "1");
        else if (key == "customTapCtrl") s.customTap.ctrl = (val == "true" || val == "1");
        else if (key == "customTapAlt") s.customTap.alt = (val == "true" || val == "1");
        else if (key == "customTapShift") s.customTap.shift = (val == "true" || val == "1");
        else if (key == "customHoldVk") s.customHold.vkCode = static_cast<uint8_t>(safeStoi(val, s.customHold.vkCode));
        else if (key == "customHoldWin") s.customHold.win = (val == "true" || val == "1");
        else if (key == "customHoldCtrl") s.customHold.ctrl = (val == "true" || val == "1");
        else if (key == "customHoldAlt") s.customHold.alt = (val == "true" || val == "1");
        else if (key == "customHoldShift") s.customHold.shift = (val == "true" || val == "1");
        else if (key == "customHold2Vk") s.customHold2.vkCode = static_cast<uint8_t>(safeStoi(val, s.customHold2.vkCode));
        else if (key == "customHold2Win") s.customHold2.win = (val == "true" || val == "1");
        else if (key == "customHold2Ctrl") s.customHold2.ctrl = (val == "true" || val == "1");
        else if (key == "customHold2Alt") s.customHold2.alt = (val == "true" || val == "1");
        else if (key == "customHold2Shift") s.customHold2.shift = (val == "true" || val == "1");
    }

    Log::info("Loaded settings from %s (Share: %s, HapticMode: %d)", path.c_str(),
              s.enableShareButton ? "ENABLED" : "DISABLED", static_cast<int>(s.hapticMode));
    return s;
}

bool Settings::save(const std::string &configPath) const
{
    std::string path = configPath.empty() ? getDefaultConfigPath() : configPath;
    // Write to a temp file, then atomically replace. An interrupted write can no
    // longer leave a half-written settings.json (which would crash the next parse).
    std::string tmpPath = path + ".tmp";
    std::ofstream file(tmpPath);
    if (!file.is_open())
    {
        Log::error("Failed to open settings file for writing: %s", tmpPath.c_str());
        return false;
    }

    file << "{\n"
         << "  \"enableShareButton\": " << (enableShareButton ? "true" : "false") << ",\n"
         << "  \"provider\": " << static_cast<int>(provider) << ",\n"
         << "  \"tapAction\": " << static_cast<int>(tapAction) << ",\n"
         << "  \"holdAction\": " << static_cast<int>(holdAction) << ",\n"
         << "  \"hold2Action\": " << static_cast<int>(hold2Action) << ",\n"
         << "  \"hapticMode\": " << static_cast<int>(hapticMode) << ",\n"
         << "  \"holdDurationMs\": " << holdDurationMs << ",\n"
         << "  \"hold2DurationMs\": " << hold2DurationMs << ",\n"
         << "  \"customTapVk\": " << (int)customTap.vkCode << ",\n"
         << "  \"customTapWin\": " << (customTap.win ? "true" : "false") << ",\n"
         << "  \"customTapCtrl\": " << (customTap.ctrl ? "true" : "false") << ",\n"
         << "  \"customTapAlt\": " << (customTap.alt ? "true" : "false") << ",\n"
         << "  \"customTapShift\": " << (customTap.shift ? "true" : "false") << ",\n"
         << "  \"customHoldVk\": " << (int)customHold.vkCode << ",\n"
         << "  \"customHoldWin\": " << (customHold.win ? "true" : "false") << ",\n"
         << "  \"customHoldCtrl\": " << (customHold.ctrl ? "true" : "false") << ",\n"
         << "  \"customHoldAlt\": " << (customHold.alt ? "true" : "false") << ",\n"
         << "  \"customHoldShift\": " << (customHold.shift ? "true" : "false") << ",\n"
         << "  \"customHold2Vk\": " << (int)customHold2.vkCode << ",\n"
         << "  \"customHold2Win\": " << (customHold2.win ? "true" : "false") << ",\n"
         << "  \"customHold2Ctrl\": " << (customHold2.ctrl ? "true" : "false") << ",\n"
         << "  \"customHold2Alt\": " << (customHold2.alt ? "true" : "false") << ",\n"
         << "  \"customHold2Shift\": " << (customHold2.shift ? "true" : "false") << "\n"
         << "}\n";
    file.close();

    if (MoveFileExA(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        Log::info("Saved settings to %s", path.c_str());
        return true;
    }

    DeleteFileA(tmpPath.c_str());
    Log::error("Failed to save settings (replace failed): %s", path.c_str());
    return false;
}
