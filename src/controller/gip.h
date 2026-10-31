#pragma once

#include "../compat.h"

#include <cstdint>
#include <functional>

struct Frame;
class Bytes;

/*
 * Base class for GIP (Game Input Protocol) devices
 */
class GipDevice
{
public:
    using SendPacket = std::function<bool(const Bytes &data)>;

    bool handlePacket(const Bytes &packet);

protected:
    enum BatteryType
    {
        BATT_TYPE_CHARGING = 0x00,
        BATT_TYPE_ALKALINE = 0x01,
        BATT_TYPE_NIMH = 0x02,
    };

    enum BatteryLevel
    {
        BATT_LEVEL_EMPTY = 0x00,
        BATT_LEVEL_LOW = 0x01,
        BATT_LEVEL_MED = 0x02,
        BATT_LEVEL_FULL = 0x03,
    };

    // Controller input can be paused temporarily
    enum PowerMode
    {
        POWER_ON = 0x00,
        POWER_SLEEP = 0x01,
        POWER_OFF = 0x04,
    };

    enum LedMode
    {
        LED_OFF = 0x00,
        LED_ON = 0x01,
        LED_BLINK_FAST = 0x02,
        LED_BLINK_MED = 0x03,
        LED_BLINK_SLOW = 0x04,
        LED_FADE_SLOW = 0x08,
        LED_FADE_FAST = 0x09,
    };

    PACKED_STRUCT_BEGIN
    struct AnnounceData
    {
        uint8_t macAddress[6];
        uint16_t unknown;
        uint16_t vendorId;
        uint16_t productId;

        struct
        {
            uint16_t major;
            uint16_t minor;
            uint16_t build;
            uint16_t revision;
        } PACKED firmwareVersion, hardwareVersion;
    } PACKED;

    struct StatusData
    {
        uint32_t batteryLevel : 2;
        uint32_t batteryType : 2;
        uint32_t connectionInfo : 4;
        uint8_t unknown1;
        uint16_t unknown2;
    } PACKED;

    struct GuideButtonData
    {
        uint8_t pressed;
        uint8_t unknown;
    } PACKED;

    struct RumbleData
    {
        uint8_t unknown;
        uint8_t setRight : 1;
        uint8_t setLeft : 1;
        uint8_t setRightTrigger : 1;
        uint8_t setLeftTrigger : 1;
        uint8_t padding : 4;
        uint8_t leftTrigger;
        uint8_t rightTrigger;
        uint8_t left;
        uint8_t right;
        uint8_t duration;
        uint8_t delay;
        uint8_t repeat;
    } PACKED;

    struct LedModeData
    {
        uint8_t unknown;
        uint8_t mode;
        uint8_t brightness;
    } PACKED;

    struct SerialData
    {
        uint16_t unknown;
        char serialNumber[14];
    } PACKED;

    struct InputData
    {
        struct
        {
            uint32_t unknown : 2;
            uint32_t start : 1;
            uint32_t select : 1;
            uint32_t a : 1;
            uint32_t b : 1;
            uint32_t x : 1;
            uint32_t y : 1;
            uint32_t dpadUp : 1;
            uint32_t dpadDown : 1;
            uint32_t dpadLeft : 1;
            uint32_t dpadRight : 1;
            uint32_t bumperLeft : 1;
            uint32_t bumperRight : 1;
            uint32_t stickLeft : 1;
            uint32_t stickRight : 1;
        } PACKED buttons;

        uint16_t triggerLeft;
        uint16_t triggerRight;
        int16_t stickLeftX;
        int16_t stickLeftY;
        int16_t stickRightX;
        int16_t stickRightY;
    } PACKED;
    PACKED_STRUCT_END

    GipDevice(SendPacket sendPacket);
    virtual ~GipDevice() = default;

    virtual void deviceAnnounced(uint8_t id, const AnnounceData *announce) = 0;
    virtual void statusReceived(uint8_t id, const StatusData *status) = 0;
    virtual void guideButtonPressed(const GuideButtonData *button) = 0;
    virtual void serialNumberReceived(const SerialData *serial) = 0;
    virtual void inputReceived(const InputData *input) = 0;

    bool setPowerMode(uint8_t id, PowerMode mode);
    bool performRumble(RumbleData rumble);
    bool setLedMode(LedModeData mode);
    bool requestSerialNumber();

private:
    bool acknowledgePacket(Frame frame);
    uint8_t getSequence(bool accessory = false);

    uint8_t sequence = 0x01;
    uint8_t accessorySequence = 0x01;
    SendPacket sendPacket;
};
