/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#include "backplane_control.hpp"

#include "backplane_mcu_driver.hpp"
#include "common.hpp"
#include "common_i2c.hpp"
#include "dbus.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <phosphor-logging/log.hpp>

#include <cstring>

using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

static constexpr int nvmeVPDAddr = 0x53;
static std::string getNVMeSerialNumberFRU(i2cDev& dev,
                                          const unsigned char* buf);
static std::string getNVMeSerialNumberV1A(i2cDev& dev,
                                          const unsigned char* buf);

static std::string getNVMeSerialNumber(const std::string driveBus)
{
    if (driveBus.empty())
    {
        return "";
    }
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
    rtrim(sn);
    return sn;
}

static constexpr size_t fruBlockSize =
    8; // FRU areas are measured in 8-byte blocks
static constexpr size_t fruHeaderSize = 8;
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
    for (int jj = 0; jj < fruHeaderSize; jj++)
    {
        sum += blockData[jj];
    }
    sum = (256 - sum) & 0xFF;

    if (sum)
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
    refresh();
}

void BackplaneController::updateConfig(const BackplaneControllerConfig& config)
{
    if (cfg == config)
    {
        return;
    }
    cfg = config;
    refresh();
}

bool BackplaneController::refresh()
{
    const bool res = doRefresh();
    functional(res);
    return res;
}

bool BackplaneController::doRefresh()
{
    try
    {
        auto mcu = backplaneMCU(i2cBusDev, i2cAddr);
        if (!mcu)
        {
            return false;
        }

        if (firmwareVersion().empty())
        {
            const auto version = mcu->getFwVersion();
            if (!version.empty())
            {
                firmwareVersion(version);
            }
        }

        std::vector<std::tuple<std::string, std::string, DriveInterface, bool>>
            drivesState;
        if (!(mcu->isStateChanged(cachedState) || drives().empty()))
        {
            return true;
        }

        for (const auto& [chanIndex, chanName] : cfg.channels)
        {
            std::string sn;
            if (chanIndex < 0 ||
                chanIndex >= BackplaneMCUDriver::maxChannelsNumber)
            {
                log<level::ERR>("Wrong channels configuration",
                                entry("BUS=%s", i2cBusDev.c_str()),
                                entry("ADDR=%d", i2cAddr),
                                entry("CHANNEL_INDEX=%d", chanIndex));
                throw InternalFailure();
            }
            bool present = mcu->drivePresent(chanIndex);
            bool failure = mcu->driveFailured(chanIndex);
            DriveTypes driveType = mcu->driveType(chanIndex);
            DriveInterface driveIface = DriveInterface::Unknown;
            switch (driveType)
            {
                case DriveTypes::SATA_SAS:
                    driveIface = DriveInterface::SATA_SAS;
                    if (!present)
                    {
                        log<level::ERR>("MCU data inconsistency detected",
                                        entry("BUS=%s", i2cBusDev.c_str()),
                                        entry("ADDR=%d", i2cAddr));
                    }
                    break;
                case DriveTypes::NVMe:
                    driveIface = DriveInterface::NVMe;
                    if (!present)
                    {
                        log<level::ERR>("MCU data inconsistency detected",
                                        entry("BUS=%s", i2cBusDev.c_str()),
                                        entry("ADDR=%d", i2cAddr));
                    }
                    break;
                case DriveTypes::NoDisk:
                    driveIface = DriveInterface::NoDisk;
                    if (present)
                    {
                        log<level::ERR>("MCU data inconsistency detected",
                                        entry("BUS=%s", i2cBusDev.c_str()),
                                        entry("ADDR=%d", i2cAddr));
                        driveIface = DriveInterface::Unknown;
                    }
                    break;
                default:
                    driveIface = DriveInterface::Unknown;
            }

            if (cfg.haveDriveI2C && driveIface == DriveInterface::NVMe)
            {
                sn = readDriveSN(chanName);
            }

            drivesState.emplace_back(chanName, sn, driveIface, failure);
        }
        drives(drivesState);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

std::string BackplaneController::readDriveSN(const std::string& chanName)
{
    return getNVMeSerialNumber(getBusByChanName(chanName));
}

std::string
    BackplaneController::findChannelByDriveSN(const std::string& driveSN)
{
    if (!refresh())
    {
        throw InternalFailure();
    }
    std::vector<std::tuple<std::string, std::string, DriveInterface, bool>>
        drivesState = drives();
    for (const auto& [chanName, sn, driveIface, failure] : drivesState)
    {
        if (sn == driveSN)
        {
            // verify information still actual
            if (sn != readDriveSN(chanName))
            {
                // force to refresh on next query
                cachedState = ~cachedState;
                break;
            }
            return chanName;
        }
    }
    return "";
}

int BackplaneController::channelIndexByName(const std::string& chanName)
{
    if (chanName.empty())
    {
        return -1;
    }

    const auto it = std::find_if(
        cfg.channels.begin(), cfg.channels.end(),
        [&chanName](const auto& it) { return it.second == chanName; });

    if (it == cfg.channels.end())
    {
        log<level::ERR>(
            "Failed to lookup channel", entry("BUS=%s", i2cBusDev.c_str()),
            entry("ADDR=%d", i2cAddr), entry("CHANNEL=%s", chanName.c_str()));
        throw InternalFailure();
    }
    return it->first;
}

void BackplaneController::setDriveLocationLED(const std::string& chanName,
                                              bool assert)
{
    int chanIndex = channelIndexByName(chanName);
    if (chanIndex < 0 || chanIndex >= BackplaneMCUDriver::maxChannelsNumber)
    {
        log<level::ERR>(
            "Wrong channels configuration", entry("BUS=%s", i2cBusDev.c_str()),
            entry("ADDR=%d", i2cAddr), entry("CHANNEL_INDEX=%d", chanIndex));
        throw InternalFailure();
    }

    try
    {
        auto mcu = backplaneMCU(i2cBusDev, i2cAddr);
        mcu->setDriveLocationLED(chanIndex, assert);
    }
    catch (...)
    {
        functional(false);
        throw InternalFailure();
    }
}

bool BackplaneController::getDriveLocationLED(const std::string& chanName)
{
    bool result = false;
    int chanIndex = channelIndexByName(chanName);
    if (chanIndex < 0 || chanIndex >= BackplaneMCUDriver::maxChannelsNumber)
    {
        log<level::ERR>(
            "Wrong channels configuration", entry("BUS=%s", i2cBusDev.c_str()),
            entry("ADDR=%d", i2cAddr), entry("CHANNEL_INDEX=%d", chanIndex));
        throw InternalFailure();
    }
    try
    {
        auto mcu = backplaneMCU(i2cBusDev, i2cAddr);
        result = mcu->getDriveLocationLED(chanIndex);
    }
    catch (...)
    {
        functional(false);
        throw InternalFailure();
    }
    return result;
}

void BackplaneController::resetDriveLocationLEDs()
{
    try
    {
        auto mcu = backplaneMCU(i2cBusDev, i2cAddr);
        mcu->resetDriveLocationLEDs();
    }
    catch (...)
    {
        functional(false);
        throw InternalFailure();
    }
}

void BackplaneController::hostPowerChanged(bool powered)
{
    if (!cfg.softwarePowerGood)
    {
        return;
    }
    try
    {
        auto mcu = backplaneMCU(i2cBusDev, i2cAddr);
        mcu->setHostPowerState(powered);
    }
    catch (...)
    {
        functional(false);
    }
}
