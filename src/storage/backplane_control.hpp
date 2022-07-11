/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#pragma once

#include "com/yadro/HWManager/BackplaneMCU/server.hpp"
#include "common_swupd.hpp"

#include <sdeventplus/source/child.hpp>
#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Software/Activation/server.hpp>
#include <xyz/openbmc_project/Software/ExtendedVersion/server.hpp>
#include <xyz/openbmc_project/Software/Version/server.hpp>
#include <xyz/openbmc_project/State/Decorator/OperationalStatus/server.hpp>

using BackplaneMCUServer = sdbusplus::server::object_t<
    sdbusplus::com::yadro::HWManager::server::BackplaneMCU,
    sdbusplus::xyz::openbmc_project::State::Decorator::server::
        OperationalStatus>;
using SoftwareVersionServer = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::Association::server::Definitions,
    sdbusplus::xyz::openbmc_project::Software::server::Activation,
    sdbusplus::xyz::openbmc_project::Software::server::ExtendedVersion,
    sdbusplus::xyz::openbmc_project::Software::server::Version>;
struct BackplaneControllerConfig
{
    std::map<int, std::string>
        channels;           //!< map MCU channel index to drive slot names
    bool haveDriveI2C;      //!< try to lookup for drive i2c buses
    bool softwarePowerGood; //!< whether we have to send host power state
                            //!< information from BMC to MCU

    bool operator==(const BackplaneControllerConfig& right) const
    {
        if (channels != right.channels)
            return false;
        if (haveDriveI2C != right.haveDriveI2C)
            return false;
        if (softwarePowerGood != right.softwarePowerGood)
            return false;
        return true;
    }
};

class BackplaneController :
    BackplaneMCUServer,
    SoftwareVersionServer,
    public FirmwareUpdateble
{
  public:
    BackplaneController(sdbusplus::bus::bus& bus, int i2cBus, int i2cAddr,
                        std::string name,
                        const BackplaneControllerConfig& config,
                        std::string inventoryItem);

    void updateConfig(const BackplaneControllerConfig& config);
    bool refresh();
    std::string findChannelByDriveSN(const std::string& driveSN);
    void setDriveLocationLED(const std::string& chanName, bool assert);
    bool getDriveLocationLED(const std::string& chanName);
    void resetDriveLocationLEDs();
    void hostPowerChanged(bool powered);
    std::string getInventory()
    {
        return inventory;
    }
    std::string getType()
    {
        return extendedVersion();
    }
    bool updateImage(std::filesystem::path imagePath, std::string imageVersion,
                     std::string dbusObject,
                     std::shared_ptr<SoftwareObject> updater);
    bool isUpdating();

  private:
    std::string i2cBusDev;
    int i2cAddr;
    BackplaneControllerConfig cfg;
    std::optional<sdeventplus::source::Child> updaterWatcher;
    std::string inventory;
    uint32_t cachedState = 0; //!< cached value of MCU channels state (presence,
                              //!< failures)

    bool doRefresh();
    std::string readDriveSN(const std::string& chanName);
    int channelIndexByName(const std::string& chanName);
};
