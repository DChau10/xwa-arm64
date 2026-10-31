#pragma once

#include "../utils/bytes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <stdexcept>

#if defined(__has_include)
    #if __has_include(<libusb-1.0/libusb.h>)
        #include <libusb-1.0/libusb.h>
    #else
        #include <libusb.h>
    #endif
#else
    #include <libusb.h>
#endif

#define USB_MAX_BULK_TRANSFER_SIZE 512

/*
 * Base class for interfacing with USB devices
 * Provides control/bulk transfer capabilities
 */
class UsbDevice
{
public:
    using Terminate = std::function<void()>;

    struct ControlPacket
    {
        uint8_t request;
        uint16_t value;
        uint16_t index;
        uint8_t *data;
        uint16_t length;
    };

    UsbDevice(libusb_device *device, Terminate terminate);
    virtual ~UsbDevice();

    void controlTransfer(ControlPacket packet, bool write);
    int bulkRead(
        uint8_t endpoint,
        FixedBytes<USB_MAX_BULK_TRANSFER_SIZE> &buffer
    );
    bool bulkWrite(uint8_t endpoint, Bytes &data);

private:
    libusb_device_handle *handle = nullptr;
    Terminate terminate;
};

/*
 * Provides access to USB devices
 * Handles device enumeration and polling
 */
class UsbDeviceManager
{
public:
    struct HardwareId
    {
        uint16_t vendorId, productId;
    };

    UsbDeviceManager();
    ~UsbDeviceManager();

    std::unique_ptr<UsbDevice> getDevice(
        std::initializer_list<HardwareId> ids,
        UsbDevice::Terminate terminate
    );

private:
    libusb_context *context = nullptr;
};

class UsbException : public std::runtime_error
{
public:
    UsbException(std::string message, int error);
};
