/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#include "backplane_mcu_driver.hpp"
#include "common.hpp"

#include <phosphor-logging/log.hpp>

#include <cstring>
#include <regex>
using namespace phosphor::logging;

/* Backplane MCU protocol version 0 */
enum MCUProtocolV0
{
    OPC_GET_IDENT = 0x00,
    OPC_GET_VERSION = 0x01,
    OPC_GET_LAST_ERR = 0x3E,
    OPC_FLASH_ERASE = 0x3F,
    OPC_FLASH_WRITE = 0x40,
    OPC_REBOOT = 0x41,
    OPC_GET_DISC_PRESENCE = 0x42,
    OPC_GET_DISC_FAILURES = 0x43,
    OPC_CLEAN_DISC_FAILURES = 0x44,
    OPC_DISC_LOCATE_START = 0x45,
    OPC_DISC_LOCATE_STOP = 0x46,
    OPC_GET_DISC_TYPE = 0x47,
    OPC_GET_BOARD_TYPE = 0x48,
    OPC_HOST_POWER_ON = 0x68,
    OPC_HOST_POWER_OFF = 0x69,
    OPC_FLASH_READ = 0x80,
};

#define OPC_IDENT_RESP 0xBC

typedef enum
{
    NO_DISK = 0,
    SAS_SATA,
    NVME,
} DiskStatus_t;

uint8_t MCUProtoV0::ident()
{
    return OPC_IDENT_RESP;
}

std::string MCUProtoV0::getFwVersion()
{
    std::string version(60, '\0');
    int res =
        dev->read_i2c_blob(OPC_GET_VERSION, version.size(),
                           reinterpret_cast<unsigned char*>(version.data()));

    if (res < 0)
    {
        log<level::ERR>("Failed to read firmware version",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        return std::string();
    }

    rtrim(version);
    return version;
}

bool MCUProtoV0::drivePresent(int chanIndex)
{
    if (dPresence < 0)
    {
        getDrivesPresence();
    }
    return dPresence & (1 << chanIndex);
}

bool MCUProtoV0::driveFailured(int chanIndex)
{
    if (dFailures < 0)
    {
        getDrivesFailures();
    }
    return dFailures & (1 << chanIndex);
}

DriveTypes MCUProtoV0::driveType(int chanIndex)
{
    int res;
    res = dev->write_byte_data(OPC_GET_DISC_TYPE, chanIndex);
    if (res < 0)
    {
        log<level::ERR>("Failed to read DISC_TYPE",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
    res = dev->read_byte();
    if (res < 0)
    {
        log<level::ERR>("Failed to read DISC_TYPE",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
    switch (res)
    {
        case NO_DISK:
            return DriveTypes::NoDisk;
        case SAS_SATA:
            return DriveTypes::SATA_SAS;
        case NVME:
            return DriveTypes::NVMe;
        default:
            log<level::ERR>("Unexpected DISC_TYPE",
                            entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                            entry("RESULT=%d", res));
    }
    return DriveTypes::Unknown;
}

void MCUProtoV0::setDriveLocationLED(int chanIndex, bool assert)
{
    int res;
    const int cmd = assert ? OPC_DISC_LOCATE_START : OPC_DISC_LOCATE_STOP;
    res = dev->write_byte_data(cmd, chanIndex);
    if (res < 0)
    {
        log<level::ERR>("Failed to set DISC_LOCATE",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
}

bool MCUProtoV0::getDriveLocationLED(int chanIndex)
{
    log<level::ERR>(
        "getDriveLocationLED not implemented in MCU protocol Version 0");
    throw std::runtime_error("Operation not supported");
}

void MCUProtoV0::resetDriveLocationLEDs()
{
    for (int chanIndex = 0; chanIndex < maxChannelsNumber; chanIndex++)
    {
        setDriveLocationLED(chanIndex, false);
    }
}

void MCUProtoV0::setHostPowerState(bool powered)
{
    const int cmd = powered ? OPC_HOST_POWER_ON : OPC_HOST_POWER_OFF;
    int res = dev->write_byte(cmd);
    if (res < 0)
    {
        log<level::ERR>("Failed to update power state",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
}

bool MCUProtoV0::isStateChanged(uint32_t& cache)
{
    bool res;
    getDrivesPresence();
    getDrivesFailures();
    uint32_t newState = dPresence | (dFailures >> 8);
    res = (newState != cache);
    cache = newState;
    return res;
}

void MCUProtoV0::getDrivesPresence()
{
    int res;
    res = dev->read_byte_data(OPC_GET_DISC_PRESENCE);
    if (res < 0)
    {
        log<level::ERR>("Failed to read DISC_PRESENCE",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
    dPresence = res;
}

void MCUProtoV0::getDrivesFailures()
{
    int res;
    res = dev->read_byte_data(OPC_GET_DISC_FAILURES);
    if (res < 0)
    {
        log<level::ERR>("Failed to read DISC_FAILURES",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
    dFailures = res;
}
