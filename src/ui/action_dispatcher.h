#pragma once

#include "settings.h"

class ActionDispatcher
{
public:
    static bool isSteamGameForeground();
    static void sendKeyCombination(const KeyBinding &binding);
    static void executeTapAction(const Settings &settings);
    static void executeHoldAction(const Settings &settings);
};
