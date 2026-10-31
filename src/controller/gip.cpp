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

GipDevice::GipDevice(SendPacket sendPacket) : sendPacket(sendPacket) {}

bool GipDevice::handlePacket(const Bytes &packet)
{
    // Ignore invalid packets
    if (packet.size() < sizeof(Frame))
    {
        return true;
    }

    const Frame *frame = packet.toStruct<Frame>();
    size_t payloadSize = packet.size() - sizeof(Frame);

    // Drop packets with incomplete payload
    if (payloadSize < frame->length)
    {
        Log::debug("Incomplete packet received");

        return true;
    }

    // Acknowledge packets that request it
    if (frame->type == TYPE_ACK)
    {
        if (!acknowledgePacket(*frame))
        {
            Log::error("Failed to acknowledge packet");

            return false;
        }
    }

    switch (frame->command)
    {
        case CMD_ANNOUNCE:
            deviceAnnounced(
                frame->deviceId,
                packet.toStruct<AnnounceData>(sizeof(Frame))
            );
            break;
        case CMD_STATUS:
            statusReceived(
                frame->deviceId,
                packet.toStruct<StatusData>(sizeof(Frame))
            );
            break;
        case CMD_GUIDE_BTN:
            guideButtonPressed(
                packet.toStruct<GuideButtonData>(sizeof(Frame))
            );
            break;
        case CMD_SERIAL_NUM:
            serialNumberReceived(
                packet.toStruct<SerialData>(sizeof(Frame))
            );
            break;
        case CMD_INPUT:
            inputReceived(
                packet.toStruct<InputData>(sizeof(Frame))
            );
            break;
        default:
            break;
    }

    return true;
}

bool GipDevice::setPowerMode(uint8_t id, PowerMode mode)
{
    Frame frame = {};

    frame.command = CMD_POWER_MODE;
    frame.deviceId = id;
    frame.type = TYPE_COMMAND;
    frame.sequence = getSequence();
    frame.length = sizeof(mode);

    Bytes out;

    out.append(frame);
    out.append(mode);

    return sendPacket(out);
}

bool GipDevice::performRumble(RumbleData rumble)
{
    Frame frame = {};

    frame.command = CMD_RUMBLE;
    frame.type = TYPE_COMMAND;
    frame.sequence = getSequence();
    frame.length = sizeof(rumble);

    Bytes out;

    out.append(frame);
    out.append(rumble);

    return sendPacket(out);
}

bool GipDevice::setLedMode(LedModeData mode)
{
    Frame frame = {};

    frame.command = CMD_LED_MODE;
    frame.type = TYPE_COMMAND;
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
    frame.type = TYPE_REQUEST;
    frame.sequence = getSequence();
    frame.length = sizeof(uint8_t);

    Bytes out;

    out.append(frame);

    // Request full serial number
    out.append(static_cast<uint8_t>(0x04));

    return sendPacket(out);
}

bool GipDevice::acknowledgePacket(Frame frame)
{
    Frame outFrame = {};

    outFrame.command = CMD_ACKNOWLEDGE;
    outFrame.deviceId = frame.deviceId;
    outFrame.type = TYPE_COMMAND;
    outFrame.sequence = frame.sequence;
    outFrame.length = sizeof(uint32_t);

    Bytes out;

    out.append(outFrame);
    out.append(static_cast<uint32_t>(0));

    return sendPacket(out);
}

uint8_t GipDevice::getSequence(bool accessory)
{
    uint8_t &seq = accessory ? accessorySequence : sequence;

    // Sequence numbers wrap around
    return seq == 0xff ? seq = 1 : seq++;
}
