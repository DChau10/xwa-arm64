#include "gip.h"
#include "../compat.h"
#include "../utils/log.h"
#include "../utils/bytes.h"

enum FrameCommand
{
    CMD_ACKNOWLEDGE = 0x01,
    CMD_ANNOUNCE = 0x02,
    CMD_STATUS = 0x03,
    CMD_IDENTIFY = 0x04,
    CMD_POWER_MODE = 0x05,
    CMD_AUTHENTICATE = 0x06,
    CMD_GUIDE_BTN = 0x07,
    CMD_AUDIO_CONFIG = 0x08,
    CMD_RUMBLE = 0x09,
    CMD_LED_MODE = 0x0a,
    CMD_SERIAL_NUM = 0x1e,
    CMD_INPUT = 0x20,
    CMD_AUDIO_SAMPLES = 0x60,
};

// Different frame types
// Command: controller doesn't respond
// Request: controller responds with data
// Request (ACK): controller responds with ack + data
enum FrameType
{
    TYPE_COMMAND = 0x00,
    TYPE_ACK = 0x01,
    TYPE_REQUEST = 0x02,
};

PACKED_STRUCT_BEGIN
struct Frame
{
    uint8_t command;
    uint8_t deviceId : 4;
    uint8_t type : 4;
    uint8_t sequence;
    uint8_t length;
} PACKED;
PACKED_STRUCT_END

static_assert(sizeof(Frame) == 4, "Frame size must be 4");

GipDevice::GipDevice(SendPacket sendPacket) : sendPacket(sendPacket) {}

bool GipDevice::handlePacket(const Bytes &packet)
{
    // Ignore invalid packets
    if (packet.size() < sizeof(Frame))
    {
        return true;
    }

    const Frame *frame = packet.toStruct<Frame>();

    if ((frame->type & TYPE_ACK) && !acknowledgePacket(*frame))
    {
        Log::error("Failed to acknowledge packet");

        return false;
    }

    // Ignore packets from accessories
    if (frame->deviceId > 0)
    {
        return true;
    }

    const Bytes data(packet, sizeof(Frame));

    // Data is 32-bit aligned, check for minimum size
    if (
        frame->command == CMD_ANNOUNCE &&
        frame->length == sizeof(AnnounceData) &&
        data.size() >= sizeof(AnnounceData)
    ) {
        deviceAnnounced(
            frame->deviceId,
            data.toStruct<AnnounceData>()
        );
    }
    else if (
        frame->command == CMD_STATUS &&
        frame->length == sizeof(StatusData) &&
        data.size() >= sizeof(StatusData)
    ) {
        statusReceived(
            frame->deviceId,
            data.toStruct<StatusData>()
        );
    }
    else if (
        frame->command == CMD_GUIDE_BTN &&
        frame->length == sizeof(GuideButtonData) &&
        data.size() >= sizeof(GuideButtonData)
    ) {
        guideButtonPressed(data.toStruct<GuideButtonData>());
    }
    else if (
        frame->command == CMD_SERIAL_NUM &&
        frame->length == sizeof(SerialData) &&
        data.size() >= sizeof(SerialData)
    ) {
        serialNumberReceived(data.toStruct<SerialData>());
    }
    // Elite controllers and Xbox Series X|S send a larger input packet
    // The button remapping is done in hardware
    // The "non-remapped" input is appended to the packet
    else if (
        frame->command == CMD_INPUT &&
        frame->length >= sizeof(InputData) &&
        data.size() >= sizeof(InputData)
    ) {
        static size_t loggedLen = 0;
        if (loggedLen != data.size())
        {
            loggedLen = data.size();
            Log::info("CMD_INPUT: payload length = %zu bytes", data.size());
        }

        // Share button on Xbox Series X|S (Model 1914):
        // Dynamic packet layout in GIP:
        // When DLI is present (44 bytes total), share button is at byte 18 (44 - 26 = 18).
        // If DLI is absent, share button is at len - 18 (e.g. byte 26 on 44-byte packets, or len - 18).
        bool sharePressed = false;
        if (data.size() >= 26)
        {
            sharePressed = ((data[18] & 0x01) != 0) || ((data[data.size() - 18] & 0x01) != 0);
        }
        else if (data.size() > 18)
        {
            sharePressed = (data[18] & 0x01) != 0;
        }

        static bool lastShare = false;
        if (sharePressed != lastShare)
        {
            lastShare = sharePressed;
            Log::info(">>> Share Button STATE CHANGED: %s <<<", sharePressed ? "PRESSED (DOWN)" : "RELEASED (UP)");
        }

        // Diagnostic: detect if any trailing extra byte changes when pressing buttons
        static uint8_t lastExtra[32] = {};
        bool extraChanged = false;
        for (size_t i = 14; i < data.size() && i < 46; ++i)
        {
            if (data[i] != lastExtra[i - 14])
            {
                extraChanged = true;
                lastExtra[i - 14] = data[i];
            }
        }
        if (extraChanged)
        {
            std::string extraLog = "";
            for (size_t i = 14; i < data.size(); ++i)
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "[%zu]=0x%02x ", i, data[i]);
                extraLog += buf;
            }
            Log::info("Input extra bytes: %s", extraLog.c_str());
        }

        inputReceived(data.toStruct<InputData>(), sharePressed);
    }

    // Ignore any unknown packets
    return true;
}

bool GipDevice::setPowerMode(uint8_t id, PowerMode mode)
{
    Frame frame = {};

    frame.command = CMD_POWER_MODE;
    frame.deviceId = id;
    frame.type = TYPE_REQUEST;
    frame.sequence = getSequence();
    frame.length = sizeof(uint8_t);

    Bytes out;

    out.append(frame);
    out.append(static_cast<uint8_t>(mode));

    return sendPacket(out);
}

bool GipDevice::performRumble(RumbleData rumble)
{
    static_assert(sizeof(RumbleData) == 9, "RumbleData size must be 9");
    Frame frame = {};

    frame.command = CMD_RUMBLE;
    frame.type = TYPE_COMMAND;
    frame.sequence = getSequence();
    frame.length = sizeof(rumble);

    Bytes out;

    out.append(frame);
    out.append(rumble);

    Log::info("performRumble: motors=0x%02x, L=%u, R=%u, duration=%u, repeat=%u",
              rumble.motors, rumble.left, rumble.right, rumble.duration, rumble.repeat);

    return sendPacket(out);
}

bool GipDevice::stopRumble()
{
    RumbleData stop = {};
    stop.motors = 0x0f;
    stop.left = 0;
    stop.right = 0;
    stop.duration = 0xff;
    stop.delay = 0;
    stop.repeat = 0xeb;
    return performRumble(stop);
}

bool GipDevice::setLedMode(LedModeData mode)
{
    Frame frame = {};

    frame.command = CMD_LED_MODE;
    frame.type = TYPE_REQUEST;
    frame.sequence = getSequence();
    frame.length = sizeof(mode);

    Bytes out;

    out.append(frame);
    out.append(mode);

    return sendPacket(out);
}

bool GipDevice::requestSerialNumber()
{
    Frame frame = {};

    frame.command = CMD_SERIAL_NUM;
    frame.type = TYPE_REQUEST | TYPE_ACK;
    frame.sequence = getSequence();
    frame.length = sizeof(uint8_t);

    Bytes out;

    // The purpose of other values is still to be discovered
    out.append(frame);
    out.append(static_cast<uint8_t>(0x04));

    return sendPacket(out);
}

bool GipDevice::acknowledgePacket(Frame frame)
{
    Frame header = {};

    header.command = CMD_ACKNOWLEDGE;
    header.deviceId = frame.deviceId;
    header.type = TYPE_REQUEST;
    header.sequence = frame.sequence;
    header.length = sizeof(header) + 5;

    frame.type = TYPE_REQUEST;
    frame.sequence = frame.length;
    frame.length = 0;

    Bytes out;

    out.append(header);
    out.pad(1);
    out.append(frame);
    out.pad(4);

    return sendPacket(out);
}

uint8_t GipDevice::getSequence(bool accessory)
{
    if (accessory)
    {
        // Zero is an invalid sequence number
        if (accessorySequence == 0x00)
        {
            accessorySequence = 0x01;
        }

        return accessorySequence++;
    }

    if (sequence == 0x00)
    {
        sequence = 0x01;
    }

    return sequence++;
}
