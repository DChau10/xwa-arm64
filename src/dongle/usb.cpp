#include "usb.h"
#include "../utils/log.h"

#include <chrono>
#include <thread>

// Timeouts in milliseconds
#define USB_TIMEOUT_READ 1000
#define USB_TIMEOUT_WRITE 1000

UsbDevice::UsbDevice(
    libusb_device *device,
    Terminate terminate
) : terminate(terminate)
{
    Log::debug("Opening device...");

    int error = libusb_open(device, &handle);
    if (error)
    {
        throw UsbException("Error opening device", error);
    }

    // Note: We do NOT call libusb_reset_device here.
    // Under Windows WinUSB, resetting the device attempts to reset pipes and cycles the hub port.
    // If the device is in the middle of being disconnected or contact bouncing, port reset fails
    // at the host controller level, prompting Windows to report "Unknown USB Device (Port Reset Failed)".

    error = libusb_set_configuration(handle, 1);
    if (error && error != LIBUSB_ERROR_NOT_SUPPORTED && error != LIBUSB_ERROR_BUSY)
    {
        Log::debug("Set configuration notice: %s", libusb_error_name(error));
    }

    error = libusb_claim_interface(handle, 0);
    if (error)
    {
        throw UsbException("Error claiming interface", error);
    }
}

UsbDevice::~UsbDevice()
{
    disconnected = true;
    if (handle)
    {
        libusb_release_interface(handle, 0);
        libusb_close(handle);
        handle = nullptr;
    }
}

void UsbDevice::controlTransfer(ControlPacket packet, bool write)
{
    if (disconnected)
    {
        return;
    }

    uint8_t type = LIBUSB_REQUEST_TYPE_VENDOR;
    type |= write ? LIBUSB_ENDPOINT_OUT : LIBUSB_ENDPOINT_IN;

    int transferred = libusb_control_transfer(
        handle,
        type,
        packet.request,
        packet.value,
        packet.index,
        packet.data,
        packet.length,
        USB_TIMEOUT_WRITE
    );

    if (transferred < 0)
    {
        if (transferred == LIBUSB_ERROR_NO_DEVICE || transferred == LIBUSB_ERROR_IO)
        {
            disconnected = true;
            if (terminate)
            {
                terminate();
            }
        }
        throw UsbException("Error performing control transfer", transferred);
    }
}

int UsbDevice::bulkRead(
    uint8_t endpoint,
    FixedBytes<USB_MAX_BULK_TRANSFER_SIZE> &buffer
) {
    if (disconnected)
    {
        return -1;
    }

    int transferred = 0;

    int error = libusb_bulk_transfer(
        handle,
        LIBUSB_ENDPOINT_IN | endpoint,
        buffer.raw(),
        static_cast<int>(buffer.size()),
        &transferred,
        USB_TIMEOUT_READ
    );

    // Timeout is expected when no data is incoming
    if (error == LIBUSB_ERROR_TIMEOUT)
    {
        return 0;
    }

    // Device was unplugged / reset
    if (error == LIBUSB_ERROR_NO_DEVICE || error == LIBUSB_ERROR_IO)
    {
        disconnected = true;
        Log::info("USB Device disconnected");
        if (terminate)
        {
            terminate();
        }
        return -1;
    }

    if (error)
    {
        Log::error("Bulk read error: %s", libusb_error_name(error));
        // Any non-timeout error (stalled pipe, busy, etc.) means the session is
        // unrecoverable in place. Tear down and let the main loop re-acquire the
        // dongle rather than silently dying with a dead reader thread. This matches
        // upstream xow, which terminates on any bulk error.
        disconnected = true;
        if (terminate)
        {
            terminate();
        }
        return -1;
    }

    return transferred;
}

bool UsbDevice::bulkWrite(uint8_t endpoint, Bytes &data)
{
    if (disconnected)
    {
        return false;
    }

    int transferred = 0;

    int error = libusb_bulk_transfer(
        handle,
        LIBUSB_ENDPOINT_OUT | endpoint,
        data.raw(),
        static_cast<int>(data.size()),
        &transferred,
        USB_TIMEOUT_WRITE
    );

    if (error == LIBUSB_ERROR_NO_DEVICE || error == LIBUSB_ERROR_IO)
    {
        disconnected = true;
        if (terminate)
        {
            terminate();
        }
        return false;
    }

    if (error)
    {
        Log::error("Bulk write error: %s", libusb_error_name(error));
        return false;
    }

    return true;
}

UsbDeviceManager::UsbDeviceManager()
{
    int error = libusb_init(&context);
    if (error)
    {
        throw UsbException("Error initializing libusb", error);
    }
}

UsbDeviceManager::~UsbDeviceManager()
{
    if (context)
    {
        libusb_exit(context);
        context = nullptr;
    }
}

std::unique_ptr<UsbDevice> UsbDeviceManager::tryGetDevice(
    std::initializer_list<HardwareId> ids,
    UsbDevice::Terminate terminate
) {
    libusb_device **list = nullptr;
    ssize_t count = libusb_get_device_list(context, &list);

    if (count < 0)
    {
        return nullptr;
    }

    for (ssize_t i = 0; i < count; i++)
    {
        libusb_device_descriptor desc = {};
        if (libusb_get_device_descriptor(list[i], &desc) == 0)
        {
            for (const auto &id : ids)
            {
                if (desc.idVendor == id.vendorId && desc.idProduct == id.productId)
                {
                    libusb_device *matched = list[i];
                    libusb_ref_device(matched);
                    libusb_free_device_list(list, 1);

                    Log::info("Found Xbox Wireless Adapter (VID: 0x%04X, PID: 0x%04X)",
                        desc.idVendor, desc.idProduct);

                    try
                    {
                        return std::unique_ptr<UsbDevice>(new UsbDevice(matched, terminate));
                    }
                    catch (const std::exception &ex)
                    {
                        Log::debug("Found adapter but unable to open (WinUSB driver may be needed): %s", ex.what());
                        libusb_unref_device(matched);
                        return nullptr;
                    }
                }
            }
        }
    }

    libusb_free_device_list(list, 1);
    return nullptr;
}

std::unique_ptr<UsbDevice> UsbDeviceManager::getDevice(
    std::initializer_list<HardwareId> ids,
    UsbDevice::Terminate terminate
) {
    Log::info("Waiting for Xbox Wireless Adapter...");

    while (true)
    {
        std::unique_ptr<UsbDevice> dev = tryGetDevice(ids, terminate);
        if (dev)
        {
            return dev;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

UsbException::UsbException(
    std::string message,
    int error
) : std::runtime_error(message + ": " + libusb_error_name(error)) {}
