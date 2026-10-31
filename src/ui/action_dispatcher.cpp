#include "action_dispatcher.h"
#include "../utils/log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>

bool ActionDispatcher::isSteamGameForeground()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return false;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;

    wchar_t path[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    BOOL ok = QueryFullProcessImageNameW(hProcess, 0, path, &size);
    CloseHandle(hProcess);

    if (!ok) return false;

    std::wstring p = path;
    for (auto &c : p) c = towlower(c);

    // If running from SteamApps directory, it is an active Steam game
    return (p.find(L"steamapps") != std::wstring::npos);
}

void ActionDispatcher::sendKeyCombination(const KeyBinding &binding)
{
    if (binding.vkCode == 0) return;

    std::vector<INPUT> inputs;

    auto addKey = [&](WORD vk, bool down) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = vk;
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        inputs.push_back(input);
    };

    // Press modifiers down
    if (binding.win) addKey(VK_LWIN, true);
    if (binding.ctrl) addKey(VK_CONTROL, true);
    if (binding.alt) addKey(VK_MENU, true);
    if (binding.shift) addKey(VK_SHIFT, true);

    // Press and release primary key
    addKey(binding.vkCode, true);
    addKey(binding.vkCode, false);

    // Release modifiers up
    if (binding.shift) addKey(VK_SHIFT, false);
    if (binding.alt) addKey(VK_MENU, false);
    if (binding.ctrl) addKey(VK_CONTROL, false);
    if (binding.win) addKey(VK_LWIN, false);

    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

bool ActionDispatcher::executeTapAction(const Settings &settings)
{
    if (!settings.enableShareButton || settings.tapAction == TapAction::Disabled) return false;

    Log::info("Share Button: Tap triggered");

    if (settings.tapAction == TapAction::CustomKey)
    {
        sendKeyCombination(settings.customTap);
        return true;
    }

    if (settings.tapAction == TapAction::SteamMarker)
    {
        Log::info("Triggering Steam Timeline Marker (Ctrl+F11)");
        sendKeyCombination(settings.steamClip);
        return true;
    }

    // Default: Screenshot
    bool inSteam = isSteamGameForeground();
    if (settings.provider == CaptureProvider::SteamOnly ||
        (settings.provider == CaptureProvider::Auto && inSteam))
    {
        Log::info("Triggering Steam Screenshot (F12)");
        sendKeyCombination(settings.steamScreenshot);
    }
    else
    {
        Log::info("Triggering Windows Game Bar Screenshot (Win+Alt+PrtScn)");
        sendKeyCombination(settings.gameBarScreenshot);
    }

    return true;
}

bool ActionDispatcher::executeHoldAction(const Settings &settings)
{
    if (!settings.enableShareButton || settings.holdAction == HoldAction::Disabled) return false;

    Log::info("Share Button: Tier 1 Hold (1s) triggered");

    if (settings.holdAction == HoldAction::CustomKey)
    {
        sendKeyCombination(settings.customHold);
        return true;
    }

    if (settings.holdAction == HoldAction::ToggleRecording)
    {
        Log::info("Triggering Game Bar Toggle Recording (Win+Alt+R)");
        sendKeyCombination(settings.gameBarToggle);
        return true;
    }

    // Default: Record Clip / Last 30s
    bool inSteam = isSteamGameForeground();
    if (settings.provider == CaptureProvider::SteamOnly ||
        (settings.provider == CaptureProvider::Auto && inSteam))
    {
        Log::info("Triggering Steam Game Recording Clip / Marker (Ctrl+F11)");
        sendKeyCombination(settings.steamClip);
    }
    else
    {
        Log::info("Triggering Windows Game Bar Record Last 30s (Win+Alt+G)");
        sendKeyCombination(settings.gameBarClip);
    }

    return true;
}

bool ActionDispatcher::executeHold2Action(const Settings &settings)
{
    if (!settings.enableShareButton || settings.hold2Action == HoldAction::Disabled) return false;

    Log::info("Share Button: Tier 2 Hold (2s) triggered");

    if (settings.hold2Action == HoldAction::CustomKey)
    {
        sendKeyCombination(settings.customHold2);
        return true;
    }

    if (settings.hold2Action == HoldAction::RecordClip)
    {
        bool inSteam = isSteamGameForeground();
        if (settings.provider == CaptureProvider::SteamOnly ||
            (settings.provider == CaptureProvider::Auto && inSteam))
        {
            sendKeyCombination(settings.steamClip);
        }
        else
        {
            sendKeyCombination(settings.gameBarClip);
        }
        return true;
    }

    // Default for Hold 2: Toggle Recording
    Log::info("Triggering Game Bar / Steam Toggle Recording (Win+Alt+R)");
    sendKeyCombination(settings.gameBarToggle);
    return true;
}
