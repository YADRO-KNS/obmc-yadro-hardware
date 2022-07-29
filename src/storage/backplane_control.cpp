/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#include "backplane_control.hpp"

#include "common.hpp"
#include "common_i2c.hpp"
#include "dbus.hpp"

#include <phosphor-logging/log.hpp>

using namespace phosphor::logging;

/* Backplane MCU protocol */
typedef enum
{
    OPC_GET_IDENT = 0x00, // who am i,
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
} OpCodeTypeDef;

#define OPC_IDENT_RESP 0xBC

typedef enum
{
    NO_DISK = 0,
    SAS_SATA,
    NVME,
} DiskStatus_t;

static constexpr int nvmeVPDAddr = 0x53;
static std::string getNVMeSerialNumberFRU(i2cDev& dev,
                                          const unsigned char* buf);
static std::string getNVMeSerialNumberV1A(i2cDev& dev,
                                          const unsigned char* buf);

static std::string getNVMeSerialNumber(const std::string driveBus)
{
    i2cDev dev(driveBus, nvmeVPDAddr);
    if (!dev.isOk())
    {
        return "";
    }
    int res;
    unsigned char buf[i2cDev::i2cBlockSize] = {0};

    res = dev.write_byte(0);
    if (res < 0)
    {
        log<level::ERR>("Failed to communicate with drive I2C",
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        return "";
    }
    res = dev.read_i2c_block_data(0, sizeof(buf), buf);
    if (res < 0)
    {
        log<level::ERR>("Failed to read drive VPD area",
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        return "";
    }

    std::string sn = getNVMeSerialNumberFRU(dev, buf);
    if (sn.empty())
    {
        sn = getNVMeSerialNumberV1A(dev, buf);
    }
    sn = rtrim(sn);
    return sn;
}

static constexpr size_t fruBlockSize =
    8; // FRU areas are measured in 8-byte blocks
static constexpr size_t fruAreaProductByte = 4;
static constexpr size_t fruProductSNFieldNumber = 5;

enum FRUDataEncoding
{
    binary = 0x0,
    bcdPlus = 0x1,
    sixBitASCII = 0x2,
    languageDependent = 0x3,
};

static bool fruValidateHeader(const unsigned char* blockData)
{
    // ipmi spec format version number is currently at 1, verify it
    if (blockData[0] != 0x1)
    {
        return false;
    }

    // verify pad is set to 0
    if (blockData[6] != 0x0)
    {
        return false;
    }

    // validate checksum
    size_t sum = 0;
    for (int jj = 0; jj < 7; jj++)
    {
        sum += blockData[jj];
    }
    sum = (256 - sum) & 0xFF;

    if (sum != blockData[7])
    {
        return false;
    }
    return true;
}

// Calculate new checksum for fru info area
static uint8_t fruCalculateChecksum(unsigned char* data, size_t len)
{
    constexpr int checksumMod = 256;
    constexpr uint8_t modVal = 0xFF;
    int sum = 0;
    for (size_t index = 0; index < len; index++)
    {
        sum += data[index];
    }
    int checksum = (checksumMod - sum) & modVal;
    return static_cast<uint8_t>(checksum);
}

static std::string getNVMeSerialNumberFRU(i2cDev& dev, const unsigned char* buf)
{
    static_assert(i2cDev::i2cBlockSize >= fruBlockSize);
    int res;

    // Try parse FRU header
    if (!fruValidateHeader(buf) || !buf[fruAreaProductByte])
    {
        return "";
    }

    const size_t productAreaOffset = buf[fruAreaProductByte] * fruBlockSize;
    res = dev.read_byte_data(productAreaOffset + 1);
    if (res < 0)
    {
        log<level::ERR>("Failed to read drive FRU product area size",
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        return "";
    }
    const size_t productAreaSize = res * fruBlockSize;

    int dataAddr = 0;
    unsigned char data[256] = {0};
    unsigned char* bufPtr = data;
    do
    {
        const int dataSize = productAreaSize - dataAddr;
        const int readBytes =
            (dataSize > i2cDev::i2cBlockSize) ? i2cDev::i2cBlockSize : dataSize;
        res = dev.read_i2c_block_data(productAreaOffset + dataAddr, readBytes,
                                      bufPtr);
        if (res < 0)
        {
            log<level::ERR>("Failed to read drive FRU product area data",
                            entry("RESULT=%d", res),
                            entry("REASON=%s", std::strerror(-res)));
            return "";
        }
        dataAddr += readBytes;
        bufPtr += readBytes;
    } while (dataAddr < productAreaSize);

    if (fruCalculateChecksum(data, productAreaSize) != 0)
    {
        log<level::ERR>("Drive FRU product area checksum error",
                        entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        return "";
    }

    // skip fields other then SN
    size_t offset = 3;
    for (size_t index = 0; index < fruProductSNFieldNumber - 1; index++)
    {

        offset += 1 + (data[offset] & 0x3F);
    }
    const size_t snLen = data[offset] & 0x3F;
    const size_t snType = (data[offset] >> 6) & 0x03;

    if (snType != FRUDataEncoding::languageDependent)
    {
        log<level::ERR>(
            "Only 8-bit ASCII supported for drive FRU Serial Number field",
            entry("RESULT=%d", res), entry("REASON=%s", std::strerror(-res)));
        return "";
    }

    const char* snPtr = reinterpret_cast<char*>(data + offset + 1);
    return std::string{snPtr, snLen};
}

constexpr size_t v1aSNFieldOffset = 5;
constexpr size_t v1aSNFieldSize = 20;

static std::string getNVMeSerialNumberV1A(i2cDev& dev, const unsigned char* buf)
{
    static_assert(i2cDev::i2cBlockSize >= v1aSNFieldOffset + v1aSNFieldSize);

    if (!((buf[0] == 0x02) && (buf[1] == 0x08) && (buf[2] == 0x01)))
    {
        return "";
    }

    const char* snPtr = reinterpret_cast<const char*>(buf);
    return std::string{snPtr + v1aSNFieldOffset, v1aSNFieldSize};
}

BackplaneController::BackplaneController(
    sdbusplus::bus::bus& bus, int i2cBus, int i2cAddr,
    const BackplaneControllerConfig& config) :
    BackplaneMCUServer(
        bus, dbusEscape(std::string(dbus::stormgr::path) + "/backplane/MCU_" +
                        std::to_string(i2cBus) + "_" + std::to_string(i2cAddr))
                 .c_str()),
    OperationalStatusServer(
        bus, dbusEscape(std::string(dbus::stormgr::path) + "/backplane/MCU_" +
                        std::to_string(i2cBus) + "_" + std::to_string(i2cAddr))
                 .c_str()),
    i2cBusDev("/dev/i2c-" + std::to_string(i2cBus)), i2cAddr(i2cAddr),
    cfg(config), cachedState(0)
{
    mcuReady();
}

void BackplaneController::updateConfig(const BackplaneControllerConfig& config)
{
    if (cfg == config)
    {
        return;
    }
    cfg = config;
    const bool res = mcuInit();
    functional(res);
}

bool BackplaneController::mcuReady()
{
    if (functional())
    {
        return true;
    }
    const bool res = mcuInit();
    functional(res);
    return res;
}

bool BackplaneController::mcuInit()
{
    static const std::regex isPrintableRegex("^[[:print:]]+$",
                                             std::regex::optimize);
    i2cDev dev(i2cBusDev, i2cAddr);
    if (!dev.isOk())
    {
        return false;
    }

    std::string value;
    int res;

    // Read MCU ident
    res = dev.read_byte_data(OPC_GET_IDENT);
    if (res != OPC_IDENT_RESP)
    {
        std::string reason;
        if (res < 0)
        {
            reason = std::strerror(-res);
        }
        else
        {
            reason = "Unexpected IDENT answer";
        }
        log<level::ERR>("Failed to read IDENT",
                        entry("BUS=%s", i2cBusDev.c_str()),
                        entry("ADDR=%d", i2cAddr), entry("RESULT=%d", res),
                        entry("REASON=%s", reason.c_str()));
        return false;
    }

    // Read MCU version
    unsigned char data[60];
    memset(data, 0, sizeof(data));
    value = std::string();
    res = dev.read_i2c_blob(OPC_GET_VERSION, sizeof(data), data);

    if (res < 0)
    {
        log<level::ERR>("Failed to read firmware version",
                        entry("BUS=%s", i2cBusDev.c_str()),
                        entry("ADDR=%d", i2cAddr), entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
    }
    else
    {
        value = std::string{reinterpret_cast<char*>(data), sizeof(data)};
        value = rtrim(value);
        if (!std::regex_match(value, isPrintableRegex))
        {
            log<level::INFO>(
                "MCU Firmware version contains non-printable characters",
                entry("BUS=%s", i2cBusDev.c_str()), entry("ADDR=%d", i2cAddr),
                entry("VALUE=%s", value.c_str()));
        }
        firmwareVersion(value);
    }

    uint8_t dPresence;
    uint8_t dFailures;
    std::vector<std::tuple<std::string, std::string, DriveInterface, bool>>
        drivesState;

    // Read channels data
    res = dev.read_byte_data(OPC_GET_DISC_PRESENCE);
    if (res < 0)
    {
        log<level::ERR>("Failed to read DISC_PRESENCE",
                        entry("BUS=%s", i2cBusDev.c_str()),
                        entry("ADDR=%d", i2cAddr), entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        return false;
    }
    dPresence = res;
    res = dev.read_byte_data(OPC_GET_DISC_FAILURES);
    if (res < 0)
    {
        log<level::ERR>("Failed to read DISC_FAILURES",
                        entry("BUS=%s", i2cBusDev.c_str()),
                        entry("ADDR=%d", i2cAddr), entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        return false;
    }
    dFailures = res;

    int state = dPresence | (dFailures >> 8);
    if (cachedState == state)
    {
        return true;
    }
    for (const auto& [chanIndex, chanName] : cfg.channels)
    {
        std::string sn;
        if (chanIndex >= 8)
        {
            log<level::ERR>("Wrong channels configuration",
                            entry("BUS=%s", i2cBusDev.c_str()),
                            entry("ADDR=%d", i2cAddr),
                            entry("CHANNEL_INDEX=%d", chanIndex));
            continue;
        }
        bool present = dPresence & (1 << chanIndex);
        bool failure = dFailures & (1 << chanIndex);
        DriveInterface driveIface = DriveInterface::Unknown;
        if (!present)
        {
            driveIface = DriveInterface::NoDisk;
        }
        else
        {

            res = dev.write_byte_data(OPC_GET_DISC_TYPE, chanIndex);
            if (res < 0)
            {
                log<level::ERR>("Failed to read DISC_TYPE",
                                entry("BUS=%s", i2cBusDev.c_str()),
                                entry("ADDR=%d", i2cAddr),
                                entry("RESULT=%d", res),
                                entry("REASON=%s", std::strerror(-res)));
                return false;
            }
            res = dev.read_byte();
            if (res < 0)
            {
                log<level::ERR>("Failed to read DISC_TYPE",
                                entry("BUS=%s", i2cBusDev.c_str()),
                                entry("ADDR=%d", i2cAddr),
                                entry("RESULT=%d", res),
                                entry("REASON=%s", std::strerror(-res)));
                return false;
            }
            switch (res)
            {
                case NO_DISK:
                    driveIface = DriveInterface::NoDisk;
                    break;
                case SAS_SATA:
                    driveIface = DriveInterface::SATA_SAS;
                    break;
                case NVME:
                    driveIface = DriveInterface::NVMe;
                    break;
                default:
                    log<level::ERR>("Unexpected DISC_TYPE",
                                    entry("BUS=%s", i2cBusDev.c_str()),
                                    entry("ADDR=%d", i2cAddr),
                                    entry("RESULT=%d", res));
                    continue;
            }
        }

        if (cfg.haveDriveI2C && driveIface == DriveInterface::NVMe)
        {
            const std::string driveBus = getBusByChanName(chanName);
            if (!driveBus.empty())
            {
                sn = getNVMeSerialNumber(driveBus);
            }
        }

        drivesState.emplace_back(chanName, sn, driveIface, failure);
    }
    drives(drivesState);
    cachedState = state;
    return true;
}

bool BackplaneController::setDriveLocationLED(const std::string& driveSN,
                                              const bool assert)
{
    std::vector<std::tuple<std::string, std::string, DriveInterface, bool>>
        drivesState = drives();
    for (const auto& [chanName, sn, driveIface, failure] : drivesState)
    {
        if (sn == driveSN)
        {
            const auto it = std::find_if(
                cfg.channels.begin(), cfg.channels.end(),
                [&chanName](const auto& it) { return it.second == chanName; });

            if (it == cfg.channels.end())
            {
                log<level::ERR>("Failed to lookup channel",
                                entry("BUS=%s", i2cBusDev.c_str()),
                                entry("ADDR=%d", i2cAddr),
                                entry("CHANNEL=%s", chanName.c_str()));
                break;
            }
            return setDriveLocationLED(it->first, assert);
        }
    }
    return false;
}

bool BackplaneController::setDriveLocationLED(const int chanIndex,
                                              const bool assert)
{
    if (!mcuReady())
    {
        return false;
    }
    i2cDev dev(i2cBusDev, i2cAddr);
    if (!dev.isOk())
    {
        return false;
    }

    const int cmd = assert ? OPC_DISC_LOCATE_START : OPC_DISC_LOCATE_STOP;
    int res = dev.write_byte_data(cmd, chanIndex);
    if (res < 0)
    {
        log<level::ERR>("Failed to turn LED ON/OFF",
                        entry("BUS=%s", i2cBusDev.c_str()),
                        entry("ADDR=%d", i2cAddr), entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        functional(false);
        return false;
    }

    return true;
}

void BackplaneController::hostPowerChanged(bool powered)
{
    if (!cfg.softwarePowerGood || !mcuReady())
    {
        return;
    }
    i2cDev dev(i2cBusDev, i2cAddr);
    if (!dev.isOk())
    {
        return;
    }

    const int cmd = powered ? OPC_HOST_POWER_ON : OPC_HOST_POWER_OFF;
    int res = dev.write_byte(cmd);
    if (res < 0)
    {
        log<level::ERR>("Failed to update power state",
                        entry("BUS=%s", i2cBusDev.c_str()),
                        entry("ADDR=%d", i2cAddr), entry("RESULT=%d", res),
                        entry("REASON=%s", std::strerror(-res)));
        functional(false);
    }
}
