#include "vigem_gamepad.h"
#include "../utils/log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ViGEm/Client.h>

#pragma comment(lib, "setupapi.lib")

static void CALLBACK vigemNotificationCallback(
    PVIGEM_CLIENT client,
    PVIGEM_TARGET target,
    UCHAR largeMotor,
    UCHAR smallMotor,
    UCHAR ledNumber,
    LPVOID userData
)
{
    auto* gamepad = static_cast<ViGEmGamepad*>(userData);
    if (gamepad)
    {
        gamepad->onNotification(largeMotor, smallMotor);
    }
}

ViGEmGamepad::ViGEmGamepad()
{
    client = vigem_alloc();
    if (!client)
    {
        Log::error("Failed to allocate ViGEm client");
        return;
    }

    VIGEM_ERROR error = vigem_connect(client);
    if (!VIGEM_SUCCESS(error))
    {
        Log::error("Failed to connect to ViGEmBus: 0x%08X (Is ViGEmBus installed?)", error);
        vigem_free(client);
        client = nullptr;
    }
}

ViGEmGamepad::~ViGEmGamepad()
{
    disconnect();

    if (client)
    {
        vigem_disconnect(client);
        vigem_free(client);
        client = nullptr;
    }
}

bool ViGEmGamepad::connect(uint16_t vendorId, uint16_t productId)
{
    if (!client)
    {
        Log::error("Cannot connect virtual gamepad: ViGEm client unavailable");
        return false;
    }

    if (connected)
    {
        return true;
    }

    target = vigem_target_x360_alloc();
    if (!target)
    {
        Log::error("Failed to allocate virtual Xbox 360 controller target");
        return false;
    }

    VIGEM_ERROR error = vigem_target_add(client, target);
    if (!VIGEM_SUCCESS(error))
    {
        Log::error("Failed to plug in virtual controller: 0x%08X", error);
        vigem_target_free(target);
        target = nullptr;
        return false;
    }

    // Register rumble force-feedback notification callback
    vigem_target_x360_register_notification(client, target, &vigemNotificationCallback, this);

    connected = true;
    Log::info("Virtual Xbox 360 controller plugged in successfully (ViGEmBus)");
    return true;
}

void ViGEmGamepad::disconnect()
{
    if (!connected || !client || !target)
    {
        return;
    }

    vigem_target_x360_unregister_notification(target);
    vigem_target_remove(client, target);
    vigem_target_free(target);
    target = nullptr;
    connected = false;

    Log::info("Virtual Xbox 360 controller unplugged");
}

void ViGEmGamepad::updateState(const GamepadState &state)
{
    if (!connected || !client || !target)
    {
        return;
    }

    XUSB_REPORT report = {};

    // Digital Buttons bitmask
    if (state.dpadUp) report.wButtons |= XUSB_GAMEPAD_DPAD_UP;
    if (state.dpadDown) report.wButtons |= XUSB_GAMEPAD_DPAD_DOWN;
    if (state.dpadLeft) report.wButtons |= XUSB_GAMEPAD_DPAD_LEFT;
    if (state.dpadRight) report.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT;
    if (state.start) report.wButtons |= XUSB_GAMEPAD_START;
    if (state.back) report.wButtons |= XUSB_GAMEPAD_BACK;
    if (state.stickLeft) report.wButtons |= XUSB_GAMEPAD_LEFT_THUMB;
    if (state.stickRight) report.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB;
    if (state.bumperLeft) report.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (state.bumperRight) report.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    if (state.guide) report.wButtons |= XUSB_GAMEPAD_GUIDE;
    if (state.a) report.wButtons |= XUSB_GAMEPAD_A;
    if (state.b) report.wButtons |= XUSB_GAMEPAD_B;
    if (state.x) report.wButtons |= XUSB_GAMEPAD_X;
    if (state.y) report.wButtons |= XUSB_GAMEPAD_Y;

    // Triggers: Map GIP 10-bit range (0-1023) to XUSB 8-bit range (0-255)
    report.bLeftTrigger = static_cast<BYTE>(state.triggerLeft / 4);
    report.bRightTrigger = static_cast<BYTE>(state.triggerRight / 4);

    // Sticks: 16-bit signed range (-32768 to 32767)
    report.sThumbLX = state.thumbLX;
    report.sThumbLY = state.thumbLY;
    report.sThumbRX = state.thumbRX;
    report.sThumbRY = state.thumbRY;

    vigem_target_x360_send_report(client, target, report);
}

void ViGEmGamepad::setRumbleCallback(RumbleCallback callback)
{
    std::lock_guard<std::mutex> lock(callbackMutex);
    rumbleCallback = std::move(callback);
}

void ViGEmGamepad::onNotification(uint8_t largeMotor, uint8_t smallMotor)
{
    std::lock_guard<std::mutex> lock(callbackMutex);
    if (rumbleCallback)
    {
        rumbleCallback(largeMotor, smallMotor);
    }
}

bool ViGEmGamepad::isConnected() const
{
    return connected;
}
