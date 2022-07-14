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

/* Backplane MCU protocol version 1 */
enum MCUProtocolV1
{
    OPC_GET_IDENT = 0x00,
    OPC_GET_VERSION = 0x01,
    OPC_GET_BOARD_TYPE = 0x02,
    OPC_GET_DISC_PRESENCE = 0x20,
    OPC_GET_DISC_FAILURES = 0x21,
    OPC_CLEAN_DISC_FAILURES = 0x22,
    OPC_DISC_LOCATE = 0x23,
    OPC_GET_DISC_TYPE = 0x24,
    OPC_GET_DISC_SWAP = 0x25,
    OPC_HOST_POWER = 0x60,
    OPC_GET_MCU_FW_VERSION = 0xF0,
    OPC_FLASH_ADDRESS = 0xFA,
    OPC_FLASH_DATA = 0xFD,
    OPC_FLASH_ERASE = 0xFE,
    OPC_REBOOT = 0x41,
};

#define OPC_IDENT_RESP 0xA8

typedef enum
{
    NO_DISK = 0,
    SAS_SATA,
    NVME,
} DiskStatus_t;

uint8_t MCUProtoV1::ident()
{
    return OPC_IDENT_RESP;
}

std::string MCUProtoV1::getFwVersion()
{
    static const std::regex isPrintableRegex("^[[:print:]]+$",
                                             std::regex::optimize);
    std::string version(22, '\0');
    int res = dev->read_i2c_block_data(
        OPC_GET_MCU_FW_VERSION, version.size(),
        reinterpret_cast<unsigned char*>(version.data()));

    if (res < 0)
    {
        log<level::ERR>("Failed to read firmware version",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        return std::string();
    }
    version = rtrim(version);
    if (!std::regex_match(version, isPrintableRegex))
    {
        log<level::INFO>(
            "MCU Firmware version contains non-printable characters",
            entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
            entry("VALUE=%s", version.c_str()));
    }

    return version;
}

bool MCUProtoV1::drivePresent(int chanIndex)
{
    if (dPresence < 0)
    {
        getDrivesPresence();
    }
    return dPresence & (1 << chanIndex);
}

bool MCUProtoV1::driveFailured(int chanIndex)
{
    if (dFailures < 0)
    {
        getDrivesFailures();
    }
    return dFailures & (1 << chanIndex);
}

DriveTypes MCUProtoV1::driveType(int chanIndex)
{
    if (dTypes < 0)
    {
        getDrivesType();
    }

    int tyoe = (dTypes >> (chanIndex * 2)) & 0x3;
    switch (tyoe)
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
                            entry("TYPE=%d", tyoe));
    }
    return DriveTypes::Unknown;
}

void MCUProtoV1::setDriveLocationLED(int chanIndex, bool assert)
{
    const uint8_t curLocationLEDs = getDrivesLocate();
    uint8_t locationLEDs = curLocationLEDs;
    if (assert)
    {
        locationLEDs |= 1 << chanIndex;
    }
    else
    {
        locationLEDs &= ~(1 << chanIndex);
    }
    if (locationLEDs == curLocationLEDs)
    {
        return;
    }

    int res = dev->write_byte_data(OPC_DISC_LOCATE, locationLEDs);
    if (res < 0)
    {
        log<level::ERR>("Failed to set DISC_LOCATE",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
}

bool MCUProtoV1::getDriveLocationLED(int chanIndex)
{
    const uint8_t locationLEDs = getDrivesLocate();
    return locationLEDs & (1 << chanIndex);
}

void MCUProtoV1::resetDriveLocationLEDs()
{
    int res = dev->write_byte_data(OPC_DISC_LOCATE, 0);
    if (res < 0)
    {
        log<level::ERR>("Failed to reset DISC_LOCATE",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
}

void MCUProtoV1::setHostPowerState(bool powered)
{
    int res = dev->write_byte_data(OPC_HOST_POWER, powered ? 1 : 0);
    if (res < 0)
    {
        log<level::ERR>("Failed to update power state",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
}

bool MCUProtoV1::ifStateChanged(uint32_t& cache)
{
    bool ret;
    getDrivesPresence();
    getDrivesFailures();
    uint32_t newState = dPresence | (dFailures >> 8);
    ret = newState == cache;
    cache = newState;

    int res = dev->read_byte_data(OPC_GET_DISC_SWAP);
    if (res < 0)
    {
        log<level::ERR>("Failed to read DISC_SWAP",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
    if (res > 0)
    {
        ret = true;
    }
    return ret;
}

void MCUProtoV1::getDrivesPresence()
{
    int res = dev->read_byte_data(OPC_GET_DISC_PRESENCE);
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

void MCUProtoV1::getDrivesFailures()
{
    int res = dev->read_byte_data(OPC_GET_DISC_FAILURES);
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

void MCUProtoV1::getDrivesType()
{
    int res = dev->read_word_data(OPC_GET_DISC_TYPE);
    if (res < 0)
    {
        log<level::ERR>("Failed to read DISC_TYPES",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
    dTypes = res;
}

uint8_t MCUProtoV1::getDrivesLocate()
{
    int res = dev->read_byte_data(OPC_DISC_LOCATE);
    if (res < 0)
    {
        log<level::ERR>("Failed to read DISC_LOCATE",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
    return res;
}
