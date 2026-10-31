#pragma once

#include "gamepad.h"

#include <memory>
#include <atomic>
#include <mutex>

// Forward declarations for ViGEm opaque types
struct _VIGEM_CLIENT_T;
typedef struct _VIGEM_CLIENT_T* PVIGEM_CLIENT;
struct _VIGEM_TARGET_T;
typedef struct _VIGEM_TARGET_T* PVIGEM_TARGET;

class ViGEmGamepad : public IVirtualGamepad
{
public:
    ViGEmGamepad();
    ~ViGEmGamepad() override;

    bool connect(uint16_t vendorId, uint16_t productId) override;
    void disconnect() override;
    void updateState(const GamepadState &state) override;
    void setRumbleCallback(RumbleCallback callback) override;
    bool isConnected() const override;

    // Internal static callback invoked by ViGEmBus
    void onNotification(uint8_t largeMotor, uint8_t smallMotor);

private:
    PVIGEM_CLIENT client = nullptr;
    PVIGEM_TARGET target = nullptr;
    std::atomic<bool> connected{false};
    RumbleCallback rumbleCallback;
    std::mutex callbackMutex;
};
