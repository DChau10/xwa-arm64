#include "controller.h"
#include "../ui/settings.h"
#include "../ui/action_dispatcher.h"
#include "../ui/tray.h"
#include "../gamepad/vigem_gamepad.h"
#include "../utils/log.h"

#include <chrono>

#define DEVICE_ID_CONTROLLER 0
#define RUMBLE_MAX_POWER 100
#define RUMBLE_DELAY std::chrono::milliseconds(10)

Controller::Controller(
    SendPacket sendPacket
) : GipDevice(sendPacket),
    stopRumbleThread(false),
    stopFeedbackThread(false)
{
    // Instantiate ViGEm Virtual Gamepad
    gamepad = std::make_unique<ViGEmGamepad>();
    gamepad->setRumbleCallback([this](uint8_t largeMotor, uint8_t smallMotor) {
        onRumbleReceived(largeMotor, smallMotor);
    });

    // Start background haptic & LED choreography worker
    feedbackThread = std::thread(&Controller::feedbackWorker, this);
}

Controller::~Controller()
{
    stopRumbleThread = true;
    rumbleCondition.notify_all();

    if (rumbleThread.joinable())
    {
        rumbleThread.join();
    }

    stopFeedbackThread = true;
    feedbackCondition.notify_all();

    if (feedbackThread.joinable())
    {
        feedbackThread.join();
    }

    if (gamepad)
    {
        gamepad->disconnect();
    }

    if (!setPowerMode(DEVICE_ID_CONTROLLER, POWER_OFF))
    {
        Log::error("Failed to turn off controller");
    }

    SystemTray::getInstance().updateStatus(true, false, "none");
}

void Controller::triggerFeedback(HapticPattern pattern)
{
    {
        std::lock_guard<std::mutex> lock(feedbackMutex);
        queuedFeedback = pattern;
    }
    feedbackCondition.notify_all();
}

void Controller::feedbackWorker()
{
    auto pulseMotors = [this](uint8_t left, uint8_t right, int durationMs) {
        RumbleData r = {};
        r.motors = 0x0f;
        r.left = left;
        r.right = right;
        r.duration = 0xff;
        r.delay = 0;
        r.repeat = 0xeb;
        performRumble(r);

        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));

        stopRumble();
    };

    while (!stopFeedbackThread)
    {
        HapticPattern pattern = HapticPattern::None;
        {
            std::unique_lock<std::mutex> lock(feedbackMutex);
            if (!isShareHeld && queuedFeedback == HapticPattern::None)
            {
                feedbackCondition.wait(lock, [this] {
                    return stopFeedbackThread || isShareHeld || queuedFeedback != HapticPattern::None;
                });
            }

            if (stopFeedbackThread) break;

            if (queuedFeedback != HapticPattern::None)
            {
                pattern = queuedFeedback;
                queuedFeedback = HapticPattern::None;
            }
        }

        // 1. Process queued confirmation patterns
        if (pattern != HapticPattern::None)
        {
            if (getGlobalSettings().hapticMode == HapticMode::Simple)
            {
                if (pattern == HapticPattern::SuccessTap ||
                    pattern == HapticPattern::SuccessTier1 ||
                    pattern == HapticPattern::SuccessTier2)
                {
                    pulseMotors(50, 50, 100);
                }
                continue;
            }

            switch (pattern)
            {
            case HapticPattern::SuccessTap:
            {
                LedModeData led = {};
                led.mode = LED_BLINK_FAST;
                led.brightness = 0x20;
                setLedMode(led);

                pulseMotors(0, 70, 50);

                std::this_thread::sleep_for(std::chrono::milliseconds(60));
                led.mode = LED_ON;
                led.brightness = 0x14;
                setLedMode(led);
                break;
            }
            case HapticPattern::SuccessTier1:
            {
                LedModeData led = {};
                led.mode = LED_BLINK_FAST;
                led.brightness = 0x20;
                setLedMode(led);

                pulseMotors(70, 60, 60);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                pulseMotors(85, 75, 70);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                led.mode = LED_ON;
                led.brightness = 0x14;
                setLedMode(led);
                break;
            }
            case HapticPattern::SuccessTier2:
            {
                LedModeData led = {};
                led.mode = LED_BLINK_FAST;
                led.brightness = 0x20;
                setLedMode(led);

                for (int i = 0; i < 3; ++i)
                {
                    pulseMotors(static_cast<uint8_t>(75 + i * 10), static_cast<uint8_t>(70 + i * 10), 60);
                    std::this_thread::sleep_for(std::chrono::milliseconds(80));
                }

                led.mode = LED_ON;
                led.brightness = 0x14;
                setLedMode(led);
                break;
            }
            case HapticPattern::FailureOrCancel:
            {
                pulseMotors(50, 20, 100);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                pulseMotors(20, 10, 80);

                LedModeData led = {};
                led.mode = LED_ON;
                led.brightness = 0x14;
                setLedMode(led);
                break;
            }
            default:
                break;
            }
            continue;
        }

        // 2. Continuous time-based ramp while Share button is held down
        if (isShareHeld)
        {
            const Settings &s = getGlobalSettings();
            if (!s.enableShareButton || s.hapticMode != HapticMode::Advanced)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - sharePressStartTime).count();

            if (elapsedMs >= s.cancelDurationMs)
            {
                if (!cancelTriggered)
                {
                    cancelTriggered = true;
                    Log::info("Share Button: Held >= %dms, action cancelled/aborted", s.cancelDurationMs);
                    pulseMotors(50, 20, 100);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    pulseMotors(20, 10, 80);
                    stopRumble();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            if (cancelTriggered)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            if (elapsedMs >= s.hold2DurationMs)
            {
                if (!tier2NotchTriggered)
                {
                    tier2NotchTriggered = true;
                    Log::info("Share Button: Tier 2 Detent Reached (2.0s)");
                    LedModeData led = {};
                    led.mode = LED_BLINK_FAST;
                    led.brightness = 0x20;
                    setLedMode(led);

                    pulseMotors(85, 85, 80);

                    led.mode = LED_ON;
                    led.brightness = 0x14;
                    setLedMode(led);
                }
                else
                {
                    // Plateau vibration indicating Tier 2 is locked
                    RumbleData r = {};
                    r.motors = 0x0f;
                    r.left = 35;
                    r.right = 35;
                    r.duration = 0xff;
                    r.repeat = 0xeb;
                    performRumble(r);
                }
            }
            else if (elapsedMs >= s.holdDurationMs)
            {
                if (!tier1NotchTriggered)
                {
                    tier1NotchTriggered = true;
                    Log::info("Share Button: Tier 1 Detent Reached (1.0s)");
                    LedModeData led = {};
                    led.mode = LED_BLINK_FAST;
                    led.brightness = 0x20;
                    setLedMode(led);

                    pulseMotors(25, 80, 60);

                    led.mode = LED_ON;
                    led.brightness = 0x14;
                    setLedMode(led);
                }
                else if (elapsedMs > 1080)
                {
                    // Tier 2 ascending ramp (1.0s to 2.0s): Dual motors rising 35% -> 80%
                    float progress = static_cast<float>(elapsedMs - 1080) / static_cast<float>(s.hold2DurationMs - 1080);
                    if (progress > 1.0f) progress = 1.0f;

                    uint8_t rPower = static_cast<uint8_t>(35 + progress * 45.0f);
                    uint8_t lPower = static_cast<uint8_t>(20 + progress * 55.0f);

                    RumbleData r = {};
                    r.motors = 0x0f;
                    r.left = lPower;
                    r.right = rPower;
                    r.duration = 0xff;
                    r.repeat = 0xeb;
                    performRumble(r);
                }
            }
            else if (elapsedMs >= 350)
            {
                // Tier 1 ascending ramp (350ms to 1000ms): Right small motor rising 15% -> 45%
                float progress = static_cast<float>(elapsedMs - 350) / static_cast<float>(s.holdDurationMs - 350);
                if (progress > 1.0f) progress = 1.0f;

                uint8_t power = static_cast<uint8_t>(15 + progress * 30.0f);

                RumbleData r = {};
                r.motors = 0x0f;
                r.left = 0;
                r.right = power;
                r.duration = 0xff;
                r.repeat = 0xeb;
                performRumble(r);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

void Controller::deviceAnnounced(uint8_t id, const AnnounceData *announce)
{
    if (id != DEVICE_ID_CONTROLLER)
    {
        return;
    }

    Log::info("Device announced, vendor: %04x, product id: %04x", announce->vendorId, announce->productId);
    Log::debug(
        "Firmware version: %d.%d.%d.%d",
        announce->firmwareVersion.major,
        announce->firmwareVersion.minor,
        announce->firmwareVersion.build,
        announce->firmwareVersion.revision
    );

    if (initialized)
    {
        return;
    }

    initialized = true;
    initInput(announce);
}

void Controller::statusReceived(uint8_t id, const StatusData *status)
{
    const std::string levels[] = { "empty", "low", "medium", "full" };

    uint8_t type = status->batteryType;
    uint8_t level = status->batteryLevel;

    if (type == BATT_TYPE_CHARGING || level == batteryLevel)
    {
        return;
    }

    if (level < 4)
    {
        Log::info("Battery level: %s", levels[level].c_str());
        SystemTray::getInstance().updateStatus(true, true, levels[level]);
    }

    batteryLevel = level;
}

void Controller::guideButtonPressed(const GuideButtonData *button)
{
    currentState.guide = button->pressed;
    if (gamepad && gamepad->isConnected())
    {
        gamepad->updateState(currentState);
    }
}

void Controller::serialNumberReceived(const SerialData *serial)
{
    size_t len = 0;
    while (len < sizeof(serial->serialNumber) && serial->serialNumber[len] != '\0')
    {
        len++;
    }

    const std::string number(serial->serialNumber, len);
    Log::info("Controller Serial number: %s", number.c_str());
}

void Controller::inputReceived(const InputData *input, bool sharePressed)
{
    static bool firstPacketLogged = false;
    if (!firstPacketLogged)
    {
        Log::info("Controller input stream active (first input packet received)");
        firstPacketLogged = true;
    }

    uint16_t b = input->buttons.raw;
    currentState.start = (b & (1 << 2)) != 0;
    currentState.back = (b & (1 << 3)) != 0;
    currentState.a = (b & (1 << 4)) != 0;
    currentState.b = (b & (1 << 5)) != 0;
    currentState.x = (b & (1 << 6)) != 0;
    currentState.y = (b & (1 << 7)) != 0;

    currentState.dpadUp = (b & (1 << 8)) != 0;
    currentState.dpadDown = (b & (1 << 9)) != 0;
    currentState.dpadLeft = (b & (1 << 10)) != 0;
    currentState.dpadRight = (b & (1 << 11)) != 0;

    currentState.bumperLeft = (b & (1 << 12)) != 0;
    currentState.bumperRight = (b & (1 << 13)) != 0;
    currentState.stickLeft = (b & (1 << 14)) != 0;
    currentState.stickRight = (b & (1 << 15)) != 0;

    // Analog sticks
    currentState.thumbLX = input->stickLeftX;
    currentState.thumbLY = input->stickLeftY;
    currentState.thumbRX = input->stickRightX;
    currentState.thumbRY = input->stickRightY;

    // Analog triggers (0..1023)
    currentState.triggerLeft = input->triggerLeft;
    currentState.triggerRight = input->triggerRight;

    if (gamepad && gamepad->isConnected())
    {
        gamepad->updateState(currentState);
    }

    // Share button multi-tiered haptic progress bar & state machine
    auto now = std::chrono::steady_clock::now();
    const Settings &settings = getGlobalSettings();

    if (!settings.enableShareButton)
    {
        return;
    }

    if (sharePressed && !shareWasPressed)
    {
        // Rising edge: button just pressed down
        shareWasPressed = true;
        isShareHeld = true;
        tier1NotchTriggered = false;
        tier2NotchTriggered = false;
        cancelTriggered = false;
        sharePressStartTime = now;
        feedbackCondition.notify_all();
    }
    else if (!sharePressed && shareWasPressed)
    {
        // Falling edge: button released
        shareWasPressed = false;
        isShareHeld = false;
        feedbackCondition.notify_all();
        stopRumble();

        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - sharePressStartTime).count();

        if (cancelTriggered)
        {
            Log::info("Share Button: Released after cancel threshold (%lldms)", elapsedMs);
            return;
        }

        if (elapsedMs >= settings.hold2DurationMs)
        {
            Log::info("Share Button: Tier 2 Release (%lldms)", elapsedMs);
            bool ok = ActionDispatcher::executeHold2Action(settings);
            if (settings.hapticMode != HapticMode::Off)
            {
                triggerFeedback(ok ? HapticPattern::SuccessTier2 : HapticPattern::FailureOrCancel);
            }
        }
        else if (elapsedMs >= settings.holdDurationMs)
        {
            Log::info("Share Button: Tier 1 Release (%lldms)", elapsedMs);
            bool ok = ActionDispatcher::executeHoldAction(settings);
            if (settings.hapticMode != HapticMode::Off)
            {
                triggerFeedback(ok ? HapticPattern::SuccessTier1 : HapticPattern::FailureOrCancel);
            }
        }
        else
        {
            Log::info("Share Button: Tap Release (%lldms)", elapsedMs);
            bool ok = ActionDispatcher::executeTapAction(settings);
            if (settings.hapticMode != HapticMode::Off)
            {
                triggerFeedback(ok ? HapticPattern::SuccessTap : HapticPattern::FailureOrCancel);
            }
        }
    }
}

void Controller::initInput(const AnnounceData *announce)
{
    LedModeData ledMode = {};

    // Test welcome vibration to confirm hardware rumble communication
    Log::info("Controller connected - triggering test rumble pulse");
    RumbleData welcome = {};
    welcome.motors = 0x0f;
    welcome.left = 45;
    welcome.right = 55;
    welcome.duration = 0xff;
    welcome.repeat = 0xeb;
    performRumble(welcome);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    stopRumble();

    // Dim the LED a little bit, like the original driver (brightness 0x00 - 0x20)
    ledMode.mode = LED_ON;
    ledMode.brightness = 0x14;

    if (!setPowerMode(DEVICE_ID_CONTROLLER, POWER_ON))
    {
        Log::error("Failed to set initial power mode");
        return;
    }

    if (!setLedMode(ledMode))
    {
        Log::error("Failed to set initial LED mode");
        return;
    }

    if (!requestSerialNumber())
    {
        Log::error("Failed to request serial number");
        return;
    }

    // Connect the virtual gamepad (ViGEm)
    if (!gamepad->connect(announce->vendorId, announce->productId))
    {
        Log::error("Failed to connect virtual gamepad");
        return;
    }

    // Start rumble consumer thread only if not already running
    if (!rumbleThread.joinable())
    {
        stopRumbleThread = false;
        rumbleThread = std::thread(&Controller::processRumble, this);
    }
}

void Controller::processRumble()
{
    RumbleData rumble = {};
    std::unique_lock<std::mutex> lock(rumbleMutex);

    while (!stopRumbleThread)
    {
        rumbleCondition.wait(lock);

        while (rumbleBuffer.get(rumble))
        {
            performRumble(rumble);
            std::this_thread::sleep_for(RUMBLE_DELAY);
        }
    }
}

void Controller::onRumbleReceived(uint8_t largeMotor, uint8_t smallMotor)
{
    RumbleData rumble = {};
    rumble.motors = 0x0f;

    // Map 0-255 motor value to 0-100 rumble power
    rumble.left = static_cast<uint8_t>(largeMotor * RUMBLE_MAX_POWER / 255);
    rumble.right = static_cast<uint8_t>(smallMotor * RUMBLE_MAX_POWER / 255);
    rumble.duration = 0xff;
    rumble.delay = 0;
    rumble.repeat = 0xeb;

    rumbleBuffer.put(rumble);
    rumbleCondition.notify_all();
}
