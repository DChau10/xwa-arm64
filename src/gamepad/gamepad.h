#pragma once

#include <cstdint>
#include <functional>

// Standardized normalized gamepad input state
struct GamepadState
{
    // Digital buttons
    bool a = false;
    bool b = false;
    bool x = false;
    bool y = false;
    bool start = false;
    bool back = false;
    bool guide = false;
    bool bumperLeft = false;
    bool bumperRight = false;
    bool stickLeft = false;
    bool stickRight = false;
    bool dpadUp = false;
    bool dpadDown = false;
    bool dpadLeft = false;
    bool dpadRight = false;

    // Analog triggers (0 - 1023 from GIP)
    uint16_t triggerLeft = 0;
    uint16_t triggerRight = 0;

    // Analog thumbsticks (-32768 to 32767)
    int16_t thumbLX = 0;
    int16_t thumbLY = 0;
    int16_t thumbRX = 0;
    int16_t thumbRY = 0;
};

// Force feedback callback: (largeMotor: 0-255, smallMotor: 0-255)
using RumbleCallback = std::function<void(uint8_t largeMotor, uint8_t smallMotor)>;

/*
 * Abstract interface for virtual gamepads (ViGEmBus or Synthetic GIP)
 */
class IVirtualGamepad
{
public:
    virtual ~IVirtualGamepad() = default;

    virtual bool connect(uint16_t vendorId, uint16_t productId) = 0;
    virtual void disconnect() = 0;
    virtual void updateState(const GamepadState &state) = 0;
    virtual void setRumbleCallback(RumbleCallback callback) = 0;
    virtual bool isConnected() const = 0;
};
