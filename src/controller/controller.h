#pragma once

#include "gip.h"
#include "../gamepad/gamepad.h"
#include "../utils/buffer.h"

#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>

enum class HapticPattern
{
    None = 0,
    NotchTier1,
    NotchTier2,
    SuccessTap,
    SuccessTier1,
    SuccessTier2,
    FailureOrCancel
};

/*
 * Forwards gamepad events to virtual input device (ViGEmBus)
 * Passes force feedback rumble effects back to physical controller
 * Manages multi-tiered haptic progress bar and LED feedback for Share button
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
    void inputReceived(const InputData *input, bool sharePressed = false) override;

    /* Device initialization */
    void initInput(const AnnounceData *announce);

    /* Rumble buffer consumer */
    void processRumble();

    /* Rumble feedback received from PC game */
    void onRumbleReceived(uint8_t largeMotor, uint8_t smallMotor);

    /* Haptic and LED feedback worker */
    void triggerFeedback(HapticPattern pattern);
    void feedbackWorker();

    std::unique_ptr<IVirtualGamepad> gamepad;
    GamepadState currentState;

    std::atomic<bool> stopRumbleThread;
    std::thread rumbleThread;
    std::mutex rumbleMutex;
    std::condition_variable rumbleCondition;
    Buffer<RumbleData> rumbleBuffer;

    /* Share button haptic & LED background worker */
    std::thread feedbackThread;
    std::mutex feedbackMutex;
    std::condition_variable feedbackCondition;
    std::atomic<bool> stopFeedbackThread{false};
    HapticPattern queuedFeedback = HapticPattern::None;

    uint8_t batteryLevel = 0xff;
    bool initialized = false;

    /* Share button multi-tier state tracking */
    std::atomic<bool> shareWasPressed{false};
    std::atomic<bool> isShareHeld{false};
    std::atomic<bool> tier1NotchTriggered{false};
    std::atomic<bool> tier2NotchTriggered{false};
    std::chrono::steady_clock::time_point sharePressStartTime;
};
