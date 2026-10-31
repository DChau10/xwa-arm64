#pragma once

#include "settings.h"

class ActionDispatcher
{
public:
    static bool isSteamGameForeground();
    static void sendKeyCombination(const KeyBinding &binding);
    static bool executeTapAction(const Settings &settings);
    static bool executeHoldAction(const Settings &settings);
    static bool executeHold2Action(const Settings &settings);
};
