#include "controller.h"
#include "../gamepad/vigem_gamepad.h"
#include "../utils/log.h"

#include <chrono>

#define DEVICE_ID_CONTROLLER 0
#define RUMBLE_MAX_POWER 100
#define RUMBLE_DELAY std::chrono::milliseconds(10)

Controller::Controller(
    SendPacket sendPacket
) : GipDevice(sendPacket),
    stopRumbleThread(false)
{
    // Instantiate ViGEm Virtual Gamepad
    gamepad = std::make_unique<ViGEmGamepad>();
    gamepad->setRumbleCallback([this](uint8_t largeMotor, uint8_t smallMotor) {
        onRumbleReceived(largeMotor, smallMotor);
    });
}

Controller::~Controller()
{
    stopRumbleThread = true;
    rumbleCondition.notify_one();

    if (rumbleThread.joinable())
    {
        rumbleThread.join();
    }

    if (gamepad)
    {
        gamepad->disconnect();
    }

    if (!setPowerMode(DEVICE_ID_CONTROLLER, POWER_OFF))
    {
        Log::error("Failed to turn off controller");
    }
}

void Controller::deviceAnnounced(uint8_t id, const AnnounceData *announce)
{
    Log::info("Device announced, vendor: %04x, product id: %04x", announce->vendorId, announce->productId);
    Log::debug(
        "Firmware version: %d.%d.%d.%d",
        announce->firmwareVersion.major,
        announce->firmwareVersion.minor,
        announce->firmwareVersion.build,
        announce->firmwareVersion.revision
    );

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
    const std::string number(
        serial->serialNumber,
        sizeof(serial->serialNumber)
    );

    Log::info("Controller Serial number: %s", number.c_str());
}

void Controller::inputReceived(const InputData *input)
{
    currentState.start = input->buttons.start;
    currentState.back = input->buttons.select;
    currentState.a = input->buttons.a;
    currentState.b = input->buttons.b;
    currentState.x = input->buttons.x;
    currentState.y = input->buttons.y;
    currentState.bumperLeft = input->buttons.bumperLeft;
    currentState.bumperRight = input->buttons.bumperRight;
    currentState.stickLeft = input->buttons.stickLeft;
    currentState.stickRight = input->buttons.stickRight;

    currentState.dpadUp = input->buttons.dpadUp;
    currentState.dpadDown = input->buttons.dpadDown;
    currentState.dpadLeft = input->buttons.dpadLeft;
    currentState.dpadRight = input->buttons.dpadRight;

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
}

void Controller::initInput(const AnnounceData *announce)
{
    LedModeData ledMode = {};

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

    // Start rumble consumer thread
    rumbleThread = std::thread(&Controller::processRumble, this);
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

    rumble.setRight = true;
    rumble.setLeft = true;

    // Map 0-255 motor value to 0-100 rumble power
    rumble.left = static_cast<uint8_t>(largeMotor * RUMBLE_MAX_POWER / 255);
    rumble.right = static_cast<uint8_t>(smallMotor * RUMBLE_MAX_POWER / 255);

    // Duration in multiples of 10ms (20 = 200ms)
    rumble.duration = 20;
    rumble.delay = 0;
    rumble.repeat = 0;

    rumbleBuffer.put(rumble);
    rumbleCondition.notify_one();
}
