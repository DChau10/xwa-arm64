#pragma once

#include "mt76.h"
#include "../controller/controller.h"

#include <cstdint>
#include <array>
#include <atomic>
#include <thread>
#include <mutex>

// Microsoft's vendor ID
#define DONGLE_VID 0x045e

// Product IDs for all revisions of the official dongle (all use MediaTek MT7612U silicon)
#define DONGLE_PID_SLIM     0x02fe // Model 1790 (Slim 2nd gen, verified)
#define DONGLE_PID_BULKY    0x02e6 // Model 1713 (Bulky 1st gen, untested)
#define DONGLE_PID_SURFACE  0x091e // Microsoft Surface built-in transceiver (untested)

// Aliases for compatibility
#define DONGLE_PID_NEW      DONGLE_PID_SLIM
#define DONGLE_PID_OLD      DONGLE_PID_BULKY

/*
 * Handles received 802.11 packets
 * Delegates GIP (Game Input Protocol) packets to controllers
 */
class Dongle : public Mt76
{
public:
    Dongle(std::unique_ptr<UsbDevice> usbDevice);
    ~Dongle();

    using Mt76::setPairingStatus;

private:
    /* Packet handling */
    void handleControllerConnect(Bytes address);
    void handleControllerDisconnect(uint8_t wcid);
    void handleControllerPair(Bytes address, const Bytes &packet);
    void handleControllerPacket(uint8_t wcid, const Bytes &packet);
    void handleWlanPacket(const Bytes &packet);
    void handleBulkData(const Bytes &data);
    void readBulkPackets(uint8_t endpoint);

    std::vector<std::thread> threads;
    std::atomic<bool> stopThreads;

    std::mutex controllerMutex;
    std::array<std::unique_ptr<Controller>, MT_WCID_COUNT> controllers;
};
