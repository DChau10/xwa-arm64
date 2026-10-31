#pragma once

#include "gip.h"
#include "../gamepad/gamepad.h"
#include "../utils/buffer.h"

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>

/*
 * Forwards gamepad events to virtual input device (ViGEmBus)
 * Passes force feedback rumble effects back to physical controller
 */
class Controller : public GipDevice
{
public:
    Controller(SendPacket sendPacket);
    ~Controller();

private:
    /* GIP events */
    void deviceAnnounced(uint8_t id, const AnnounceData *announce) override;
    void statusReceived(uint8_t id, const StatusData *status) override;
    void guideButtonPressed(const GuideButtonData *button) override;
    void serialNumberReceived(const SerialData *serial) override;
    void inputReceived(const InputData *input) override;

    /* Device initialization */
    void initInput(const AnnounceData *announce);

    /* Rumble buffer consumer */
    void processRumble();

    /* Rumble feedback received from PC game */
    void onRumbleReceived(uint8_t largeMotor, uint8_t smallMotor);

    std::unique_ptr<IVirtualGamepad> gamepad;
    GamepadState currentState;

    std::atomic<bool> stopRumbleThread;
    std::thread rumbleThread;
    std::mutex rumbleMutex;
    std::condition_variable rumbleCondition;
    Buffer<RumbleData> rumbleBuffer;

    uint8_t batteryLevel = 0xff;
    bool initialized = false;
};
