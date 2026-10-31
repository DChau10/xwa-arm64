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
        std::string newPath = dir + "\\settings.json";

        // Migrate from legacy xow-win if present
        std::string oldPath = std::string(localAppData) + "\\xow-win\\settings.json";
        std::ifstream nf(newPath);
        if (!nf.good())
        {
            std::ifstream of(oldPath);
            if (of.good())
            {
                of.close();
                CopyFileA(oldPath.c_str(), newPath.c_str(), FALSE);
            }
        }
        return newPath;
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
        if (val.back() == ',') val.pop_back();
        val = trim(val);

        if (key == "enableShareButton") s.enableShareButton = (val == "true" || val == "1");
        else if (key == "provider") s.provider = static_cast<CaptureProvider>(std::stoi(val));
        else if (key == "tapAction") s.tapAction = static_cast<TapAction>(std::stoi(val));
        else if (key == "holdAction") s.holdAction = static_cast<HoldAction>(std::stoi(val));
        else if (key == "hold2Action") s.hold2Action = static_cast<HoldAction>(std::stoi(val));
        else if (key == "hapticMode") s.hapticMode = static_cast<HapticMode>(std::stoi(val));
        else if (key == "holdDurationMs") s.holdDurationMs = std::stoi(val);
        else if (key == "hold2DurationMs") s.hold2DurationMs = std::stoi(val);
        else if (key == "customTapVk") s.customTap.vkCode = static_cast<uint8_t>(std::stoi(val));
        else if (key == "customTapWin") s.customTap.win = (val == "true" || val == "1");
        else if (key == "customTapCtrl") s.customTap.ctrl = (val == "true" || val == "1");
        else if (key == "customTapAlt") s.customTap.alt = (val == "true" || val == "1");
        else if (key == "customTapShift") s.customTap.shift = (val == "true" || val == "1");
        else if (key == "customHoldVk") s.customHold.vkCode = static_cast<uint8_t>(std::stoi(val));
        else if (key == "customHoldWin") s.customHold.win = (val == "true" || val == "1");
        else if (key == "customHoldCtrl") s.customHold.ctrl = (val == "true" || val == "1");
        else if (key == "customHoldAlt") s.customHold.alt = (val == "true" || val == "1");
        else if (key == "customHoldShift") s.customHold.shift = (val == "true" || val == "1");
        else if (key == "customHold2Vk") s.customHold2.vkCode = static_cast<uint8_t>(std::stoi(val));
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
    std::ofstream file(path);
    if (!file.is_open())
    {
        Log::error("Failed to open settings file for writing: %s", path.c_str());
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

    Log::info("Saved settings to %s", path.c_str());
    return true;
}
