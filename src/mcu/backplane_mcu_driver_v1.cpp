/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#include "backplane_mcu_driver.hpp"
#include "common.hpp"

#include <arpa/inet.h>

#include <phosphor-logging/log.hpp>

#include <cstring>
#include <regex>
using namespace phosphor::logging;

/* Backplane MCU protocol version 1 */
enum MCUProtocolV1
{
    OPC_GET_IDENT = 0x00,
    OPC_GET_PROT_VERSION = 0x01,
    OPC_GET_BOARD_TYPE = 0x02,
    OPC_GET_DISC_PRESENCE = 0x20,
    OPC_GET_DISC_FAILURES = 0x21,
    OPC_CLEAN_DISC_FAILURES = 0x22,
    OPC_DISC_LOCATE = 0x23,
    OPC_GET_DISC_TYPE = 0x24,
    OPC_GET_DISC_PRESENCE_CHANGED = 0x25,
    OPC_HOST_POWER = 0x60,
    OPC_GET_SGPIO_MAPPING = 0x61,
    OPC_GET_MCU_FW_VERSION = 0xF0,
    OPC_FLASH_ADDRESS = 0xFA,
    OPC_FLASH_DATA = 0xFD,
    OPC_FLASH_ERASE = 0xFE,
    OPC_REBOOT = 0xFF,
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
    std::string version(32, '\0');
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

    rtrim(version);
    return version;
}

std::string MCUProtoV1::getBoardType()
{
    std::string type(19, '\0');
    int res =
        dev->read_i2c_block_data(OPC_GET_BOARD_TYPE, type.size(),
                                 reinterpret_cast<unsigned char*>(type.data()));

    if (res < 0)
    {
        log<level::ERR>("Failed to read board type",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        return std::string();
    }

    rtrim(type);
    return type;
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

bool MCUProtoV1::isStateChanged(uint32_t& cache)
{
    bool ret;
    getDrivesPresence();
    getDrivesFailures();
    uint32_t newState = dPresence | (dFailures >> 8);
    ret = (newState != cache);
    cache = newState;
    if (ret)
    {
        return true;
    }

    int res = dev->read_byte_data(OPC_GET_DISC_PRESENCE_CHANGED);
    if (res < 0)
    {
        log<level::ERR>("Failed to read DISC_PRESENCE_CHANGED",
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

bool MCUProtoV1::ping()
{
    int res = dev->read_byte_data(OPC_GET_IDENT);
    if (res < 0)
    {
        return false;
    }
    return true;
}

void MCUProtoV1::reboot()
{
    int res = dev->write_byte(OPC_REBOOT);
    if (res < 0)
    {
        log<level::ERR>("Failed to send reboot command",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
}

void MCUProtoV1::eraseFlash()
{
    int res = dev->write_byte(OPC_FLASH_ERASE);
    if (res < 0)
    {
        log<level::ERR>("Failed to erase MCU Flash memory",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }
    flashOffset = 0;
}

void MCUProtoV1::writeFlash(const char* data, uint8_t length)
{
    int res;
    struct __attribute__((packed))
    {
        uint32_t offset;
        uint8_t length;
    } setLocationCommand = {.offset = htonl(flashOffset), .length = length};

    uint8_t buf[length];

    res = dev->write_i2c_blob(OPC_FLASH_ADDRESS, sizeof(setLocationCommand),
                              reinterpret_cast<uint8_t*>(&setLocationCommand));
    if (res < 0)
    {
        log<level::ERR>("Failed to set write region",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }

    res = dev->write_i2c_blob(
        OPC_FLASH_DATA, length,
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(data)));
    if (res < 0)
    {
        log<level::ERR>("Failed write data to flash",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    res = dev->read_i2c_blob(OPC_FLASH_DATA, length, buf);
    if (res < 0)
    {
        log<level::ERR>("Failed to read data from flash",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()),
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        throw std::runtime_error("Failed to communicate with MCU");
    }

    if (std::memcmp(data, buf, length))
    {
        log<level::ERR>("Verify error during fw update",
                        entry("I2C_DEV=%s", dev->getDevLabel().c_str()));
        throw std::runtime_error("Failed to write MCU Flash");
    }
    flashOffset += length;
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
