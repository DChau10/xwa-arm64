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
    rumbleCondition.notify_all();

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

void Controller::inputReceived(const InputData *input)
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
    rumbleCondition.notify_all();
}
